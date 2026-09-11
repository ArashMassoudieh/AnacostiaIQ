/////////////////////////////////////////////////////////////
// MAXBOTIXSENSOR.H - MaxBotix MB7389-100 ultrasonic sensor
//
//  The field-proven MB7389 wiring uses two Pi-side signals:
//    * sensor serial output -> Pi UART RX (/dev/serial0, 9600 8N1)
//    * Pi GPIO trigger       -> sensor RX/control input
//
//  For each sample the control line is held LOW for 145 ms, then
//  driven HIGH and allowed 145 ms for a fresh measurement before
//  the serial "Rxxxx\r" frame is parsed (xxxx = range in mm).
//
//  This sensor reports DEPTH directly: depth = totalLength -
//  measuredDistance, both expressed in the configured unit.
/////////////////////////////////////////////////////////////

#ifndef MAXBOTIXSENSOR_H
#define MAXBOTIXSENSOR_H

#include "Sensor.h"
#include <QString>
#include <memory>

#ifdef RasPi
#include <gpiod.hpp>
#endif

class MaxbotixSensor : public Sensor {
    Q_OBJECT

public:
    // device      - serial port, e.g. "/dev/serial0"
    // chip        - GPIO chip containing triggerPin
    // triggerPin  - Pi GPIO connected to the MB7389 RX/control input
    // unit        - "mm", "cm", or "in"; drives both the reported unit
    //               and conversion applied to the raw mm reading
    // totalLength - full pipe/standpipe length in the configured unit;
    //               depth = totalLength - measuredDistance
    MaxbotixSensor(const QString &id, const QString &unit,
                   const QString &name, const QString &device,
                   double totalLength, const QString &chip,
                   int triggerPin);
    ~MaxbotixSensor() override;

    bool   initialize() override;
    void   cleanup() override;
    double measure() override;

private:
    double mmToUnit(int rangeMm) const;

    QString m_device;
    double  m_totalLength;
    QString m_chip;
    int     m_triggerPin;
    int     m_fd = -1;

#ifdef RasPi
    std::unique_ptr<gpiod::chip> m_gpioChip;
    std::unique_ptr<gpiod::line_request> m_trigger;
#endif

    static constexpr int MIN_RANGE_MM = 500;
    static constexpr int MAX_RANGE_MM = 5000;
    static constexpr int READ_TIMEOUT_MS = 1000;
    static constexpr int TRIGGER_HOLD_MS = 145;
    static constexpr int MEASUREMENT_WAIT_MS = 145;
};

#endif // MAXBOTIXSENSOR_H
