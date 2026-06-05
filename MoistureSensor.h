/////////////////////////////////////////////////////////////
// MOISTURESENSOR.H - Soil moisture sensor (MCP3008 over SPI)
/////////////////////////////////////////////////////////////

#ifndef MOISTURESENSOR_H
#define MOISTURESENSOR_H

#include "Sensor.h"
#include <QThread>
#ifdef RasPi
#include <wiringPi.h>
#include <wiringPiSPI.h>
#endif

// Analog soil-moisture sensor read through an MCP3008 ADC over SPI.
// spiChannel selects the Pi chip-select (0=CE0, 1=CE1); adcChannel
// selects the MCP3008 input (0–7). Reports moisture as a percent.
class MoistureSensor : public Sensor {
    Q_OBJECT

public:
    MoistureSensor(const QString &id, const QString &unit,
                   const QString &name, int spiChannel, int adcChannel);
    ~MoistureSensor() override;

    bool   initialize() override;   // Setup SPI; false if unavailable
    void   cleanup() override;
    double measure() override;      // Moisture %, or -1 if unavailable

private:
    int    readChannel(int channel);
    double rawToMoisturePercent(int raw);

    int m_spiChannel;   // Pi SPI chip-select (0 or 1)
    int m_adcChannel;   // MCP3008 input channel (0–7)
};

#endif // MOISTURESENSOR_H
