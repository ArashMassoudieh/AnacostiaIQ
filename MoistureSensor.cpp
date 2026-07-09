/////////////////////////////////////////////////////////////
// MOISTURESENSOR.CPP - Soil moisture sensor (one ADC0804 channel)
//
//  The read itself lives in AdcBus, which drives the shared WR /
//  P/S / CLOCK lines and shifts a byte out of every CD4014 at once.
//  Here we pick this probe's byte off the bus and turn it into a
//  moisture percentage using the probe's own calibration.
/////////////////////////////////////////////////////////////

#include "MoistureSensor.h"
#include <cmath>

MoistureSensor::MoistureSensor(const QString &id, const QString &unit,
                               const QString &name,
                               std::shared_ptr<AdcBus> bus, int dataPin,
                               int adcDry, int adcWet)
    : Sensor(id, unit, name),
      m_bus(std::move(bus)), m_dataPin(dataPin),
      m_adcDry(adcDry), m_adcWet(adcWet) {
    setFullScale(100.0);   // moisture is reported as 0-100 %
}

MoistureSensor::~MoistureSensor() = default;

bool MoistureSensor::initialize() {
    if (!m_bus) {
        qWarning() << "MoistureSensor" << id()
                   << ": no ADC bus — check the \"adc\" block in config.json";
        setAvailable(false);
        return false;
    }
    if (m_dataPin < 0) {
        qWarning() << "MoistureSensor" << id() << ": dataPin not configured";
        setAvailable(false);
        return false;
    }
    // A calibration that isn't dry-above-wet would invert or divide by
    // zero in rawToMoisturePercent().
    if (m_adcDry <= m_adcWet) {
        qWarning() << "MoistureSensor" << id() << ": adcDry" << m_adcDry
                   << "must be greater than adcWet" << m_adcWet;
        setAvailable(false);
        return false;
    }

    // Idempotent: whichever channel initializes first brings the bus up.
    const bool ok = m_bus->initialize();
    setAvailable(ok);
    return ok;
}

double MoistureSensor::rawToMoisturePercent(int raw) const {
    // ADC0804 spans 0-255; the probe's own dry/wet counts map that
    // onto 0-100 %. Readings outside the calibration clamp.
    const double pct = 100.0 * (m_adcDry - raw) / (m_adcDry - m_adcWet);
    return qBound(0.0, pct, 100.0);
}

double MoistureSensor::measure() {
    if (!m_bus)
        return -1;

    const int raw = m_bus->read(m_dataPin);
    if (raw < 0)
        return -1;            // honest gap rather than a fake reading

    const double voltage  = (raw * 5.0) / 255.0;
    const double moisture = rawToMoisturePercent(raw);

    qDebug() << "MoistureSensor" << id() << ": ADC =" << raw
             << "voltage =" << voltage
             << "moisture =" << moisture << "%";

    return std::round(moisture);
}
