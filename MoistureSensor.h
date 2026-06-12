/////////////////////////////////////////////////////////////
// MOISTURESENSOR.H - Soil moisture sensor (ADC0804 over GPIO)
//
//  8-bit parallel ADC (ADC0804) driven by bit-banged GPIO via
//  libgpiod. NOT SPI. Pin assignments and calibration are passed
//  in from config.json via the factory.
/////////////////////////////////////////////////////////////
#ifndef MOISTURESENSOR_H
#define MOISTURESENSOR_H
#include "Sensor.h"
#include <QString>
#include <QVector>
#include <QDebug>

#ifdef RasPi
#include <gpiod.hpp>
#include <memory>
#endif

class MoistureSensor : public Sensor {
public:
    MoistureSensor(const QString &id, const QString &unit, const QString &name,
                   const QString &chip, const QVector<int> &dataPins,
                   int wrPin, int rdPin, int intrPin,
                   int adcDry, int adcWet);
    ~MoistureSensor() override;

    bool   initialize() override;
    double measure() override;

private:
    void   cleanup();
    int    readChannel();                      // returns 0–255, or -1 on failure
    double rawToMoisturePercent(int raw) const;

    QString       m_chip;
    QVector<int>  m_dataPins;   // [DB0..DB7]
    int           m_wrPin   = -1;
    int           m_rdPin   = -1;
    int           m_intrPin = -1;
    int           m_adcDry  = 645;
    int           m_adcWet  = 160;

#ifdef RasPi
    std::unique_ptr<gpiod::chip>          m_chipObj;
    std::unique_ptr<gpiod::line_request>  m_dataReq;
    std::unique_ptr<gpiod::line_request>  m_wrReq;
    std::unique_ptr<gpiod::line_request>  m_rdReq;
    std::unique_ptr<gpiod::line_request>  m_intrReq;
#endif
};
#endif // MOISTURESENSOR_H
