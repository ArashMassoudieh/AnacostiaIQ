/////////////////////////////////////////////////////////////
// MAXBOTIXSENSOR.H - MaxBotix MB7389-100 ultrasonic sensor
//
//  Reads range over UART serial (9600 baud, 8N1) as ASCII frames
//  of the form "Rxxxx\r" where xxxx is the range in millimetres.
//
//  This unit requires an RX/trigger GPIO pulse (LOW 145ms, HIGH
//  145ms) before each reading — confirmed by Sean Morgenstern's
//  reference implementation (All_inclusive_sensor_program/MB7389.cpp,
//  TRIGGER_PIN = GPIO25). Without it the sensor never transmits, no
//  matter how sound the UART wiring is. triggerPin is optional (-1
//  disables it) for any unit that genuinely free-runs without one.
//
//  This sensor reports DEPTH directly: depth = total_length −
//  measured_distance, both expressed in the configured unit.
//  total_length and the reported unit come from config.
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
    // device      — serial port, e.g. "/dev/serial0"
    // unit        — "mm", "cm", or "in"; drives both the reported unit
    //               AND the conversion applied to the raw mm reading
    // totalLength — full pipe/standpipe length in the configured unit;
    //               depth = totalLength − measuredDistance
    // triggerPin  — BCM GPIO line toggled before each read, or -1 to
    //               skip triggering entirely (passive/free-run mode)
    // chip        — gpiochip device path, e.g. "/dev/gpiochip0";
    //               only used when triggerPin >= 0
    MaxbotixSensor(const QString &id, const QString &unit,
                   const QString &name, const QString &device,
                   double totalLength, int triggerPin = -1,
                   const QString &chip = QStringLiteral("/dev/gpiochip0"));
    ~MaxbotixSensor() override;

    bool   initialize() override;   // Open UART (+ trigger GPIO); false if unavailable
    void   cleanup() override;      // Close the serial port and release GPIO
    double measure() override;      // Water depth in configured unit, or -1

private:
    // Convert a raw millimetre range into the configured unit.
    double mmToUnit(int rangeMm) const;

    QString m_device;        // serial device path
    double  m_totalLength;   // in configured unit
    int     m_fd = -1;       // open file descriptor (-1 = closed)

    int     m_triggerPin;    // BCM GPIO line, or -1 if unused
    QString m_chipPath;

#ifdef RasPi
    std::unique_ptr<gpiod::chip>         m_chip;
    std::unique_ptr<gpiod::line_request> m_triggerReq;
#endif

    static constexpr int MIN_RANGE_MM = 500;    // sensor min (below = too close)
    static constexpr int MAX_RANGE_MM = 5000;   // sensor max (at/above = no target)
    static constexpr int READ_TIMEOUT_MS = 1000; // give up after this per measure()
    static constexpr int TRIGGER_PULSE_MS = 145; // matches the proven reference timing
};

#endif // MAXBOTIXSENSOR_H
