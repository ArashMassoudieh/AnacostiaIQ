/////////////////////////////////////////////////////////////
// ADCBUS.CPP - Shared ADC0804 + CD4014 acquisition bus
//
//  The line-level sequence below mirrors the validated standalone
//  program in Automated_Multi-ADC_sensing_1/main.cpp:
//
//    1. Pulse WR             -> every ADC starts converting
//    2. Wait conversionDelay -> conversion settles (no INTR is wired;
//                               the CD4014 sits between the ADC and
//                               the Pi, so completion can't be sensed)
//    3. P/S HIGH, pulse CLOCK, P/S LOW
//                            -> each CD4014 latches its ADC's byte
//                               and switches to serial mode
//    4. Eight times: sample every data line, then pulse CLOCK
//                            -> Q8 presents MSB first, so each byte
//                               shifts out most-significant bit first
/////////////////////////////////////////////////////////////

#include "AdcBus.h"
#include <QDebug>

#ifdef RasPi
#include <thread>
#include <chrono>
#include <cstdint>
#endif

AdcBus::AdcBus(const QString &chip, int wrPin, int psPin, int clockPin,
               int conversionDelayMs, int pulseWidthUs, int cacheMs)
    : m_chip(chip),
      m_wrPin(wrPin), m_psPin(psPin), m_clockPin(clockPin),
      m_conversionDelayMs(conversionDelayMs),
      m_pulseWidthUs(pulseWidthUs),
      m_cacheMs(cacheMs) {
}

AdcBus::~AdcBus() {
    cleanup();
}

void AdcBus::addChannel(int dataPin) {
    if (m_initialized) {
        qWarning() << "AdcBus: addChannel(" << dataPin
                   << ") after initialize() — ignored";
        return;
    }
    if (dataPin < 0) {
        qWarning() << "AdcBus: ignoring invalid data pin" << dataPin;
        return;
    }
    if (m_dataPins.contains(dataPin)) {
        qWarning() << "AdcBus: data pin" << dataPin
                   << "registered twice — ignoring the duplicate";
        return;
    }
    m_dataPins.append(dataPin);
}

bool AdcBus::initialize() {
    if (m_initialized)
        return m_available;
    m_initialized = true;

    if (m_dataPins.isEmpty()) {
        qWarning() << "AdcBus: no channels registered";
        m_available = false;
        return false;
    }
    if (m_wrPin < 0 || m_psPin < 0 || m_clockPin < 0) {
        qWarning() << "AdcBus: wrPin/psPin/clockPin not configured "
                      "— check the \"adc\" block in config.json";
        m_available = false;
        return false;
    }

#ifdef RasPi
    try {
        m_chipObj = std::make_unique<gpiod::chip>(m_chip.toStdString());

        // WR, P/S and CLOCK go out together: one request, one consumer.
        gpiod::line::offsets ctrlLines = {
            static_cast<unsigned int>(m_wrPin),
            static_cast<unsigned int>(m_psPin),
            static_cast<unsigned int>(m_clockPin)
        };

        m_ctrlReq = std::make_unique<gpiod::line_request>(
            m_chipObj->prepare_request()
                .set_consumer("anacostiaiq-adc-ctrl")
                .add_line_settings(
                    ctrlLines,
                    gpiod::line_settings()
                        .set_direction(gpiod::line::direction::OUTPUT))
                .do_request());

        // Every channel's CD4014 Q8 output, requested in one batch so
        // the eight shift steps can sample them all per clock.
        gpiod::line::offsets dataLines;
        for (int pin : m_dataPins)
            dataLines.push_back(static_cast<unsigned int>(pin));

        m_dataReq = std::make_unique<gpiod::line_request>(
            m_chipObj->prepare_request()
                .set_consumer("anacostiaiq-adc-data")
                .add_line_settings(
                    dataLines,
                    gpiod::line_settings()
                        .set_direction(gpiod::line::direction::INPUT))
                .do_request());

        // Idle state: all three control lines low.
        m_ctrlReq->set_value(static_cast<unsigned int>(m_wrPin),
                             gpiod::line::value::INACTIVE);
        m_ctrlReq->set_value(static_cast<unsigned int>(m_psPin),
                             gpiod::line::value::INACTIVE);
        m_ctrlReq->set_value(static_cast<unsigned int>(m_clockPin),
                             gpiod::line::value::INACTIVE);

        qDebug() << "AdcBus: ready on" << m_chip
                 << "| WR" << m_wrPin << "P/S" << m_psPin
                 << "CLOCK" << m_clockPin
                 << "| channels" << m_dataPins;

        m_available = true;
        return true;
    }
    catch (const std::exception &e) {
        qWarning() << "AdcBus: GPIO init failed:" << e.what();
        cleanup();
        m_available = false;
        return false;
    }
#else
    qWarning() << "AdcBus: no GPIO on this system — ADC channels unavailable";
    m_available = false;
    return false;
#endif
}

void AdcBus::cleanup() {
#ifdef RasPi
    m_dataReq.reset();
    m_ctrlReq.reset();
    m_chipObj.reset();
#endif
}

#ifdef RasPi
// Drive a control line high for pulseWidthUs, then low for the same,
// leaving it low. WR, P/S-load and CLOCK all use this shape.
void AdcBus::pulse(gpiod::line_request &req, unsigned int pin) {
    req.set_value(pin, gpiod::line::value::ACTIVE);
    std::this_thread::sleep_for(std::chrono::microseconds(m_pulseWidthUs));
    req.set_value(pin, gpiod::line::value::INACTIVE);
    std::this_thread::sleep_for(std::chrono::microseconds(m_pulseWidthUs));
}
#endif

bool AdcBus::convertAndShift() {
#ifdef RasPi
    if (!m_ctrlReq || !m_dataReq)
        return false;

    const unsigned int wr    = static_cast<unsigned int>(m_wrPin);
    const unsigned int ps    = static_cast<unsigned int>(m_psPin);
    const unsigned int clock = static_cast<unsigned int>(m_clockPin);

    try {
        // ── Start a conversion on every ADC ────────────────
        pulse(*m_ctrlReq, wr);
        std::this_thread::sleep_for(
            std::chrono::milliseconds(m_conversionDelayMs));

        // ── Latch each byte into its CD4014, back to serial ─
        m_ctrlReq->set_value(ps, gpiod::line::value::ACTIVE);
        pulse(*m_ctrlReq, clock);
        m_ctrlReq->set_value(ps, gpiod::line::value::INACTIVE);

        // ── Shift all channels out together, MSB first ─────
        QHash<int, int> acc;
        for (int pin : m_dataPins)
            acc.insert(pin, 0);

        for (int bit = 0; bit < 8; ++bit) {
            for (int pin : m_dataPins) {
                const int b =
                    (m_dataReq->get_value(static_cast<unsigned int>(pin))
                     == gpiod::line::value::ACTIVE) ? 1 : 0;
                acc[pin] = ((acc[pin] << 1) | b) & 0xFF;
            }
            pulse(*m_ctrlReq, clock);
        }

        m_values = acc;
        m_haveSample = true;
        m_sampleAge.restart();
        return true;
    }
    catch (const std::exception &e) {
        qWarning() << "AdcBus: acquisition failed:" << e.what();
        m_haveSample = false;
        return false;
    }
#else
    return false;
#endif
}

int AdcBus::read(int dataPin) {
    if (!m_available)
        return -1;

    if (!m_dataPins.contains(dataPin)) {
        qWarning() << "AdcBus: data pin" << dataPin << "is not a bus channel";
        return -1;
    }

    const bool cached = m_haveSample
                        && m_cacheMs > 0
                        && m_sampleAge.isValid()
                        && m_sampleAge.elapsed() < m_cacheMs;

    if (!cached && !convertAndShift())
        return -1;

    return m_values.value(dataPin, -1);
}
