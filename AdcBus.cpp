/////////////////////////////////////////////////////////////
// ADCBUS.CPP - Shared ADC0804 + CD4014 acquisition bus
//
//  The line-level sequence below mirrors All_inclusive_sensor_program/
//  ADC.cpp — the standalone program that reads correctly on the Pi:
//
//    0. POWER HIGH           -> energise the bank (optional line; the
//                               working rig feeds the bank from 5V and
//                               leaves powerPin at -1)
//    1. WR HIGH for wrHighUs, then LOW
//                            -> every ADC starts converting
//    2. Wait conversionDelay -> conversion settles (no INTR is wired;
//                               the CD4014 sits between the ADC and
//                               the Pi, so completion can't be sensed)
//    3. P/S HIGH, settle, pulse CLOCK, P/S LOW, settle
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
               int powerPin, int wrHighUs, int conversionDelayMs,
               int pulseWidthUs, int settleUs, int cacheMs)
    : m_chip(chip),
      m_wrPin(wrPin), m_psPin(psPin), m_clockPin(clockPin),
      m_powerPin(powerPin),
      m_wrHighUs(wrHighUs),
      m_conversionDelayMs(conversionDelayMs),
      m_pulseWidthUs(pulseWidthUs),
      m_settleUs(settleUs),
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
        // The supply-enable line joins them when configured — it's an
        // output on the same chip, and one request keeps the ordering
        // (power first) trivial.
        gpiod::line::offsets ctrlLines = {
            static_cast<unsigned int>(m_wrPin),
            static_cast<unsigned int>(m_psPin),
            static_cast<unsigned int>(m_clockPin)
        };
        if (m_powerPin >= 0)
            ctrlLines.push_back(static_cast<unsigned int>(m_powerPin));

        m_ctrlReq = std::make_unique<gpiod::line_request>(
            m_chipObj->prepare_request()
                .set_consumer("anacostiaiq-adc-ctrl")
                .add_line_settings(
                    ctrlLines,
                    gpiod::line_settings()
                        .set_direction(gpiod::line::direction::OUTPUT))
                .do_request());

        // Power the bank before anything else touches it, and leave it
        // on for the life of the bus.
        if (m_powerPin >= 0)
            m_ctrlReq->set_value(static_cast<unsigned int>(m_powerPin),
                                 gpiod::line::value::ACTIVE);

        // Every channel's CD4014 Q8 output, requested in one batch so
        // the eight shift steps can sample them all per clock.
        gpiod::line::offsets dataLines;
        for (int pin : m_dataPins)
            dataLines.push_back(static_cast<unsigned int>(pin));

        // PULL_DOWN matches the working program: the CD4014 drives Q8
        // hard, but between a request and the first latch the line is
        // undriven, and a floating input reads as noise.
        m_dataReq = std::make_unique<gpiod::line_request>(
            m_chipObj->prepare_request()
                .set_consumer("anacostiaiq-adc-data")
                .add_line_settings(
                    dataLines,
                    gpiod::line_settings()
                        .set_direction(gpiod::line::direction::INPUT)
                        .set_bias(gpiod::line::bias::PULL_DOWN))
                .do_request());

        // Idle state: all three control lines low, with the same settle
        // before CLOCK that the working program uses.
        m_ctrlReq->set_value(static_cast<unsigned int>(m_wrPin),
                             gpiod::line::value::INACTIVE);
        m_ctrlReq->set_value(static_cast<unsigned int>(m_psPin),
                             gpiod::line::value::INACTIVE);
        std::this_thread::sleep_for(std::chrono::microseconds(m_settleUs));
        m_ctrlReq->set_value(static_cast<unsigned int>(m_clockPin),
                             gpiod::line::value::INACTIVE);

        qDebug() << "AdcBus: ready on" << m_chip
                 << "| WR" << m_wrPin << "P/S" << m_psPin
                 << "CLOCK" << m_clockPin
                 << "POWER" << (m_powerPin >= 0 ? QString::number(m_powerPin)
                                                : QString("none"))
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
// Drive CLOCK high for pulseWidthUs, then low for the same, leaving
// it low. Both the latch pulse and the eight shift pulses use it.
// WR is not pulsed through here — it has its own, much longer high
// time (wrHighUs) in the working program.
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
        m_ctrlReq->set_value(wr, gpiod::line::value::ACTIVE);
        std::this_thread::sleep_for(std::chrono::microseconds(m_wrHighUs));
        m_ctrlReq->set_value(wr, gpiod::line::value::INACTIVE);
        std::this_thread::sleep_for(
            std::chrono::milliseconds(m_conversionDelayMs));

        // ── Latch each byte into its CD4014, back to serial ─
        m_ctrlReq->set_value(ps, gpiod::line::value::ACTIVE);
        std::this_thread::sleep_for(std::chrono::microseconds(m_settleUs));
        pulse(*m_ctrlReq, clock);
        m_ctrlReq->set_value(ps, gpiod::line::value::INACTIVE);
        std::this_thread::sleep_for(std::chrono::microseconds(m_settleUs));

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
