/////////////////////////////////////////////////////////////
// MOISTURESENSORI2C.CPP - Soil moisture sensor (one ADS1115 channel)
/////////////////////////////////////////////////////////////

#include "MoistureSensorI2C.h"

MoistureSensorI2C::MoistureSensorI2C(const QString &id, const QString &unit,
                                     const QString &name,
                                     std::shared_ptr<Ads1115Bus> bus,
                                     int channel, int adcDry, int adcWet,
                                     bool rawOnly)
    : Sensor(id, unit, name),
      m_bus(std::move(bus)), m_channel(channel),
      m_adcDry(adcDry), m_adcWet(adcWet), m_rawOnly(rawOnly) {
    if (!rawOnly)
        setFullScale(100.0);   // moisture is reported as 0-100 %
    // rawOnly leaves fullScale at its Sensor default: the bus's fsVoltage
    // isn't exposed here, and nothing in this codebase reads fullScale
    // for anything load-bearing, so guessing a value isn't worth doing.
}

MoistureSensorI2C::~MoistureSensorI2C() = default;

bool MoistureSensorI2C::initialize() {
    if (!m_bus) {
        qWarning() << "MoistureSensorI2C" << id()
                   << ": no ADS1115 bus — check the \"ads1115\" block in"
                      " config.json";
        setAvailable(false);
        return false;
    }
    if (m_channel < 0 || m_channel > 3) {
        qWarning() << "MoistureSensorI2C" << id()
                   << ": channel must be 0-3, got" << m_channel;
        setAvailable(false);
        return false;
    }
    // A calibration that isn't dry-above-wet would invert or divide by
    // zero in rawToMoisturePercent() — same guard as MoistureSensor.
    // Not applicable in raw mode: there is no dry/wet calibration to validate.
    if (!m_rawOnly && m_adcDry <= m_adcWet) {
        qWarning() << "MoistureSensorI2C" << id() << ": adcDry" << m_adcDry
                   << "must be greater than adcWet" << m_adcWet;
        setAvailable(false);
        return false;
    }

    // Idempotent: whichever channel initializes first opens the bus.
    const bool ok = m_bus->initialize();
    setAvailable(ok);
    return ok;
}

double MoistureSensorI2C::rawToMoisturePercent(int raw) const {
    const double pct = 100.0 * (m_adcDry - raw) / (m_adcDry - m_adcWet);
    return qBound(0.0, pct, 100.0);
}

double MoistureSensorI2C::measure() {
    if (!m_bus)
        return -1;

    const int raw = m_bus->read(m_channel);
    if (raw < 0)
        return -1;            // honest gap rather than a fake reading

    if (m_rawOnly) {
        const double volts = m_bus->rawToVoltage(raw);
        qDebug() << "MoistureSensorI2C" << id() << ": raw =" << raw
                 << "voltage =" << volts << "V";
        return volts;
    }

    const double moisture = rawToMoisturePercent(raw);

    qDebug() << "MoistureSensorI2C" << id() << ": raw =" << raw
             << "moisture =" << moisture << "%";

    // HealthMonitor examines this value before display formatting. Rounding
    // a valid 0.2% (or 99.8%) to a whole percent creates a false boundary
    // reading and can incorrectly mark a communicating probe disconnected.
    return moisture;
}
