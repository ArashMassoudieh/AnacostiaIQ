/////////////////////////////////////////////////////////////
// CURRENTLOOPSENSORI2C.H - 4-20 mA transmitter on ADS1115
/////////////////////////////////////////////////////////////

#ifndef CURRENTLOOPSENSORI2C_H
#define CURRENTLOOPSENSORI2C_H

#include "Ads1115Bus.h"
#include "Sensor.h"

#include <memory>

class CurrentLoopSensorI2C : public Sensor {
public:
    CurrentLoopSensorI2C(const QString &id, const QString &unit,
                         const QString &name,
                         std::shared_ptr<Ads1115Bus> bus,
                         int channel, double shuntOhms,
                         double validMinMa, double validMaxMa,
                         bool scaleOutput, double currentMinMa,
                         double currentMaxMa, double outputMin,
                         double outputMax);

    bool initialize() override;
    double measure() override;

private:
    std::shared_ptr<Ads1115Bus> m_bus;
    int m_channel = -1;
    double m_shuntOhms = 0.0;
    double m_validMinMa = 3.5;
    double m_validMaxMa = 21.5;
    bool m_scaleOutput = false;
    double m_currentMinMa = 4.0;
    double m_currentMaxMa = 20.0;
    double m_outputMin = 0.0;
    double m_outputMax = 0.0;
};

#endif // CURRENTLOOPSENSORI2C_H
