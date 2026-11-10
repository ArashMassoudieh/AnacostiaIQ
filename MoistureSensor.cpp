/////////////////////////////////////////////////////////////
// MOISTURESENSOR.CPP - Soil moisture sensor (one ADC0804 channel)
/////////////////////////////////////////////////////////////

#include "MoistureSensor.h"
#include <cmath>

MoistureSensor::MoistureSensor(const QString &id, const QString &unit,
                               const QString &name,
                               std::shared_ptr<AdcBus> bus, int dataPin,
                               int adcDry, int adcWet, bool rejectAdcRails,
                               bool rawOnly)
    : Sensor(id, unit, name),
      m_bus(std::move(bus)), m_dataPin(dataPin),
      m_adcDry(adcDry), m_adcWet(adcWet),
      m_rejectAdcRails(rejectAdcRails), m_rawOnly(rawOnly) {
    setFullScale(rawOnly ? 5.0 : 100.0);   // ADC0804 reference is 5V
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
    // Not applicable in raw mode: there is no dry/wet calibration to validate.
    if (!m_rawOnly && m_adcDry <= m_adcWet) {
        qWarning() << "MoistureSensor" << id() << ": adcDry" << m_adcDry
                   << "must be greater than adcWet" << m_adcWet;
        setAvailable(false);
        return false;
    }

    const bool ok = m_bus->initialize();
    setAvailable(ok);
    return ok;
}

double MoistureSensor::rawToMoisturePercent(int raw) const {
    const double pct = 100.0 * (m_adcDry - raw) / (m_adcDry - m_adcWet);
    return qBound(0.0, pct, 100.0);
}

double MoistureSensor::measure() {
    if (!m_bus) {
        qWarning() << "MoistureSensor" << id() << ": no ADC bus";
        return -1;
    }

    const int raw = m_bus->read(m_dataPin);
    if (raw < 0)
        return -1;

    // AdcBus intentionally biases an undriven CD4014 Q8 line low. If the
    // converter/probe is absent, the eight shifted bits therefore become
    // 0x00 and the old code silently mapped that to 100% moisture. A line
    // stuck high has the analogous 0xFF signature. Neither rail is accepted
    // as a real sample by default; returning -1 lets Sensor/HealthMonitor
    // record an honest failed sample and eventually mark the sensor offline.
    if (m_rejectAdcRails && (raw == 0 || raw == 255)) {
        qWarning() << "MoistureSensor" << id()
                   << ": ADC rail value" << raw
                   << "— treating channel as unavailable";
        return -1;
    }

    const double voltage = (raw * 5.0) / 255.0;

    if (m_rawOnly) {
        qDebug() << "MoistureSensor" << id() << ": ADC =" << raw
                 << "voltage =" << voltage << "V";
        return voltage;
    }

    const double moisture = rawToMoisturePercent(raw);

    qDebug() << "MoistureSensor" << id() << ": ADC =" << raw
             << "voltage =" << voltage
             << "moisture =" << moisture << "%";

    return std::round(moisture);
}
