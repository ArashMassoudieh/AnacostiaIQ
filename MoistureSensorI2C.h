/////////////////////////////////////////////////////////////
// MOISTURESENSORI2C.H - Soil moisture sensor (one ADS1115 channel)
//
//  New Build replacement for MoistureSensor/AdcBus: a moisture probe
//  is one single-ended channel on a shared ADS1115 (see Ads1115Bus).
//  Same calibration idea as the ADC0804 path (raw counts between a
//  dry-air and a wet/submerged reading map to 0-100%), but the raw
//  range is the ADS1115's signed 16-bit conversion result instead of
//  ADC0804's 0-255 byte — the old adcDry/adcWet values do NOT carry
//  over and must be recalibrated for this bus.
/////////////////////////////////////////////////////////////

#ifndef MOISTURESENSORI2C_H
#define MOISTURESENSORI2C_H

#include "Sensor.h"
#include "Ads1115Bus.h"
#include <QString>
#include <memory>

class MoistureSensorI2C : public Sensor {
public:
    MoistureSensorI2C(const QString &id, const QString &unit,
                      const QString &name, std::shared_ptr<Ads1115Bus> bus,
                      int channel, int adcDry, int adcWet);
    ~MoistureSensorI2C() override;

    bool   initialize() override;
    double measure() override;

private:
    double rawToMoisturePercent(int raw) const;

    std::shared_ptr<Ads1115Bus> m_bus;
    int m_channel = -1;   // ADS1115 single-ended input, A0-A3

    // Raw ADS1115 counts for a probe in dry air and in water/wet soil.
    // Dry reads higher than wet, so adcDry > adcWet — same convention
    // as MoistureSensor, but these are ADS1115-scale counts, not
    // ADC0804-scale ones.
    int m_adcDry;
    int m_adcWet;
};

#endif // MOISTURESENSORI2C_H
