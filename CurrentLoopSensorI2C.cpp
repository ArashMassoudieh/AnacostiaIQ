/////////////////////////////////////////////////////////////
// CURRENTLOOPSENSORI2C.CPP - 4-20 mA transmitter on ADS1115
/////////////////////////////////////////////////////////////

#include "CurrentLoopSensorI2C.h"

#include <QDebug>
#include <QtGlobal>

CurrentLoopSensorI2C::CurrentLoopSensorI2C(
        const QString &id, const QString &unit, const QString &name,
        std::shared_ptr<Ads1115Bus> bus, int channel, double shuntOhms,
        double validMinMa, double validMaxMa, bool scaleOutput,
        double currentMinMa, double currentMaxMa, double outputMin,
        double outputMax)
    : Sensor(id, unit, name), m_bus(std::move(bus)), m_channel(channel),
      m_shuntOhms(shuntOhms), m_validMinMa(validMinMa),
      m_validMaxMa(validMaxMa), m_scaleOutput(scaleOutput),
      m_currentMinMa(currentMinMa), m_currentMaxMa(currentMaxMa),
      m_outputMin(outputMin), m_outputMax(outputMax) {
    setFullScale(m_scaleOutput ? m_outputMax : m_validMaxMa);
}

bool CurrentLoopSensorI2C::initialize() {
    if (!m_bus) {
        qWarning() << "CurrentLoopSensorI2C" << id()
                   << ": no ADS1115 bus — check the \"ads1115\" block";
        setAvailable(false);
        return false;
    }
    if (m_channel < 0 || m_channel > 3) {
        qWarning() << "CurrentLoopSensorI2C" << id()
                   << ": channel must be 0-3, got" << m_channel;
        setAvailable(false);
        return false;
    }
    if (m_shuntOhms <= 0.0 || m_validMinMa < 0.0 ||
        m_validMaxMa <= m_validMinMa) {
        qWarning() << "CurrentLoopSensorI2C" << id()
                   << ": invalid shunt or current validation range";
        setAvailable(false);
        return false;
    }
    if (m_scaleOutput &&
        (m_currentMaxMa <= m_currentMinMa || m_outputMax <= m_outputMin)) {
        qWarning() << "CurrentLoopSensorI2C" << id()
                   << ": invalid current/output scaling range";
        setAvailable(false);
        return false;
    }

    const bool ok = m_bus->initialize();
    setAvailable(ok);
    return ok;
}

double CurrentLoopSensorI2C::measure() {
    if (!m_bus)
        return -1.0;

    const int raw = m_bus->read(m_channel);
    if (raw < 0)
        return -1.0;

    const double volts = m_bus->rawToVoltage(raw);
    const double currentMa = volts * 1000.0 / m_shuntOhms;

    // NAMUR-style tolerance around the nominal 4-20 mA span catches an open
    // loop, failed supply, and gross overrange instead of uploading a plausible
    // but false engineering value. Limits remain configurable per transmitter.
    if (currentMa < m_validMinMa || currentMa > m_validMaxMa) {
        qWarning() << "CurrentLoopSensorI2C" << id()
                   << ": loop current outside valid range:"
                   << currentMa << "mA (raw" << raw << "," << volts << "V)";
        return -1.0;
    }

    if (!m_scaleOutput) {
        qDebug() << "CurrentLoopSensorI2C" << id() << ": raw =" << raw
                 << "voltage =" << volts << "V current =" << currentMa << "mA";
        return currentMa;
    }

    // Deliberately NOT clamped to [currentMinMa, currentMaxMa] before this
    // division: for an empirically calibrated probe (as opposed to a
    // nominal 4-20 mA spec), silently pinning an out-of-span reading to the
    // calibration endpoint would hide a real over/under-range condition.
    // The formula is linearly extrapolated instead, and a warning is logged
    // so an extrapolated value is diagnosable rather than indistinguishable
    // from an interpolated one. validMinMa/validMaxMa above remain a hard
    // reject -- that's open-loop/failed-transmitter detection on the
    // circuit itself, a different concept from the calibration span.
    const double fraction = (currentMa - m_currentMinMa) /
                            (m_currentMaxMa - m_currentMinMa);
    const double output = m_outputMin + fraction * (m_outputMax - m_outputMin);

    if (fraction < 0.0 || fraction > 1.0) {
        qWarning() << "CurrentLoopSensorI2C" << id()
                   << ": reading extrapolates beyond the calibrated span ("
                   << currentMa << "mA vs calibrated" << m_currentMinMa << "-"
                   << m_currentMaxMa << "mA) -- output" << output << unit()
                   << "is extrapolated, not interpolated";
    }

    qDebug() << "CurrentLoopSensorI2C" << id() << ": raw =" << raw
             << "voltage =" << volts << "V current =" << currentMa
             << "mA output =" << output << unit();
    return output;
}
