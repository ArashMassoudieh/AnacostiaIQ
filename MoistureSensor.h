/////////////////////////////////////////////////////////////
// MOISTURESENSOR.H - Soil moisture sensor (one ADC0804 channel)
//
//  A moisture probe is one channel on a shared AdcBus: an ADC0804
//  whose byte is shifted out through a CD4014. The bus owns the
//  GPIO lines (they're shared with the other converters); this class
//  contributes only its data pin and its own dry/wet calibration.
//
//  Everything comes from config.json — the "adc" block describes the
//  bus, each sensor entry names its dataPin, adcDry and adcWet.
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
                   int adcDry, int adcWet);
    ~MoistureSensor() override;

    bool   initialize() override;
    double measure() override;

private:
    double rawToMoisturePercent(int raw) const;

    std::shared_ptr<AdcBus> m_bus;
    int m_dataPin = -1;   // this probe's CD4014 Q8 line

    // Raw ADC counts (0-255) for a probe in dry air and in water.
    // Dry reads higher than wet, so adcDry > adcWet.
    int m_adcDry = 105;
    int m_adcWet = 32;
};

#endif // MOISTURESENSOR_H
