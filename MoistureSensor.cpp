/////////////////////////////////////////////////////////////
// MOISTURESENSOR.CPP - Soil moisture sensor (ADC0804 over GPIO)
//
//  The ADC0804 is an 8-bit parallel ADC driven by bit-banged
//  GPIO via libgpiod (NOT SPI). Handshake per read:
//    1. Pulse WR low->high to start a conversion
//    2. Wait for INTR to go low (conversion complete)
//    3. Drive RD low, latch the 8 data lines, RD high
//
//  Pins/calibration come from config.json via the factory.
/////////////////////////////////////////////////////////////
#include "MoistureSensor.h"
#include <cmath>
#ifdef RasPi
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#endif

#ifdef RasPi
#include <thread>
#include <chrono>
#endif

MoistureSensor::MoistureSensor(const QString &id, const QString &unit,
                               const QString &name, const QString &chip,
                               const QVector<int> &dataPins,
                               int wrPin, int rdPin, int intrPin,
                               int adcDry, int adcWet)
    : Sensor(id, unit, name),
    m_chip(chip), m_dataPins(dataPins),
    m_wrPin(wrPin), m_rdPin(rdPin), m_intrPin(intrPin),
    m_adcDry(adcDry), m_adcWet(adcWet) {
}

MoistureSensor::~MoistureSensor() {
    cleanup();
}

bool MoistureSensor::initialize() {
#ifdef RasPi
    if (m_dataPins.size() != 8) {
        qWarning() << "MoistureSensor: dataPins must list exactly 8 lines, got"
                   << m_dataPins.size();
        setAvailable(false);
        return false;
    }
    if (m_wrPin < 0 || m_rdPin < 0 || m_intrPin < 0) {
        qWarning() << "MoistureSensor: wrPin/rdPin/intrPin not configured";
        setAvailable(false);
        return false;
    }

    try {
        m_chipObj = std::make_unique<gpiod::chip>(m_chip.toStdString());

        gpiod::line::offsets data_lines = {
            static_cast<unsigned int>(m_dataPins[0]),
            static_cast<unsigned int>(m_dataPins[1]),
            static_cast<unsigned int>(m_dataPins[2]),
            static_cast<unsigned int>(m_dataPins[3]),
            static_cast<unsigned int>(m_dataPins[4]),
            static_cast<unsigned int>(m_dataPins[5]),
            static_cast<unsigned int>(m_dataPins[6]),
            static_cast<unsigned int>(m_dataPins[7])
        };

        m_dataReq = std::make_unique<gpiod::line_request>(
            m_chipObj->prepare_request()
                .set_consumer("moisture-data")
                .add_line_settings(
                    data_lines,
                    gpiod::line_settings()
                        .set_direction(gpiod::line::direction::INPUT))
                .do_request());

        m_wrReq = std::make_unique<gpiod::line_request>(
            m_chipObj->prepare_request()
                .set_consumer("moisture-wr")
                .add_line_settings(
                    static_cast<unsigned int>(m_wrPin),
                    gpiod::line_settings()
                        .set_direction(gpiod::line::direction::OUTPUT))
                .do_request());

        m_rdReq = std::make_unique<gpiod::line_request>(
            m_chipObj->prepare_request()
                .set_consumer("moisture-rd")
                .add_line_settings(
                    static_cast<unsigned int>(m_rdPin),
                    gpiod::line_settings()
                        .set_direction(gpiod::line::direction::OUTPUT))
                .do_request());

        m_intrReq = std::make_unique<gpiod::line_request>(
            m_chipObj->prepare_request()
                .set_consumer("moisture-intr")
                .add_line_settings(
                    static_cast<unsigned int>(m_intrPin),
                    gpiod::line_settings()
                        .set_direction(gpiod::line::direction::INPUT))
                .do_request());

        // Idle both control lines high.
        m_wrReq->set_value(static_cast<unsigned int>(m_wrPin),
                           gpiod::line::value::ACTIVE);
        m_rdReq->set_value(static_cast<unsigned int>(m_rdPin),
                           gpiod::line::value::ACTIVE);

        setAvailable(true);
        return true;
    } catch (const std::exception &e) {
        qWarning() << "MoistureSensor: GPIO init failed:" << e.what();
        cleanup();
        setAvailable(false);
        return false;
    }
#else
    qWarning() << "MoistureSensor: no GPIO on this system "
                  "— moisture sensor unavailable";
    setAvailable(false);
    return false;
#endif
}

void MoistureSensor::cleanup() {
#ifdef RasPi
    m_intrReq.reset();
    m_rdReq.reset();
    m_wrReq.reset();
    m_dataReq.reset();
    m_chipObj.reset();
#endif
}

int MoistureSensor::readChannel() {
#ifdef RasPi
    if (!m_dataReq) return -1;

    const unsigned int wr   = static_cast<unsigned int>(m_wrPin);
    const unsigned int rd   = static_cast<unsigned int>(m_rdPin);
    const unsigned int intr = static_cast<unsigned int>(m_intrPin);

    // Start conversion: pulse WR low then high.
    m_wrReq->set_value(wr, gpiod::line::value::INACTIVE);
    std::this_thread::sleep_for(std::chrono::microseconds(2));
    m_wrReq->set_value(wr, gpiod::line::value::ACTIVE);

    // Wait for INTR low = conversion done. Bounded so a stuck INTR
    // can't hang the monitoring tick.
    int guard = 0;
    while (m_intrReq->get_value(intr) == gpiod::line::value::ACTIVE) {
        std::this_thread::sleep_for(std::chrono::microseconds(10));
        if (++guard > 10000) {   // ~100 ms ceiling
            qWarning() << "MoistureSensor: INTR never went low — ADC not responding";
            return -1;
        }
    }

    // Latch the byte: RD low, read 8 lines, RD high.
    m_rdReq->set_value(rd, gpiod::line::value::INACTIVE);

    uint8_t value = 0;
    for (int bit = 0; bit < 8; ++bit) {
        if (m_dataReq->get_value(static_cast<unsigned int>(m_dataPins[bit]))
            == gpiod::line::value::ACTIVE)
            value |= (1u << bit);
    }

    m_rdReq->set_value(rd, gpiod::line::value::ACTIVE);

    return value;   // 0–255
#else
    return -1;
#endif
}

double MoistureSensor::rawToMoisturePercent(int raw) const {
    // ADC0804 range 0–255, calibration from config.json.
    double pct = 100.0 * (m_adcDry - raw) / (m_adcDry - m_adcWet);
    return qBound(0.0, pct, 100.0);
}

double MoistureSensor::measure() {
#ifdef RasPi
    int raw = readChannel();
    if (raw < 0) return -1;            // honest gap rather than fake reading

    double voltage  = (raw * 5.0) / 255.0;
    double moisture = rawToMoisturePercent(raw);
    qDebug() << "MoistureSensor: ADC =" << raw
             << "voltage =" << voltage
             << "moisture =" << moisture << "%";
    return std::round(moisture);
#else
    return -1;
#endif
}
