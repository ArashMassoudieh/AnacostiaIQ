/////////////////////////////////////////////////////////////
// MOISTURESENSOR.H - Soil moisture sensor (one ADC0804 channel)
/////////////////////////////////////////////////////////////

#ifndef MOISTURESENSOR_H
#define MOISTURESENSOR_H

#include "Sensor.h"
#include "AdcBus.h"
#include <QString>
#include <QDebug>
#include <memory>

class MoistureSensor : public Sensor {
public:
    MoistureSensor(const QString &id, const QString &unit, const QString &name,
                   std::shared_ptr<AdcBus> bus, int dataPin,
                   int adcDry, int adcWet, bool rejectAdcRails = true);
    ~MoistureSensor() override;

    bool   initialize() override;
    double measure() override;

private:
    double rawToMoisturePercent(int raw) const;

    std::shared_ptr<AdcBus> m_bus;
    int m_dataPin = -1;
    int m_adcDry = 105;
    int m_adcWet = 32;

    // A disconnected/floating CD4014 input is biased low by AdcBus, so
    // an absent ADC/probe commonly shifts 0x00. 0xFF is the symmetric
    // stuck-high rail. Rejecting both prevents either electrical fault
    // from becoming a plausible 100%/0% moisture measurement. This is
    // configurable because a future installation may intentionally use
    // the full ADC rail as part of a calibrated measurement range.
    bool m_rejectAdcRails = true;
};

#endif // MOISTURESENSOR_H
