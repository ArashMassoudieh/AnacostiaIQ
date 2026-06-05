/////////////////////////////////////////////////////////////
// MAXBOTIXSENSOR.H - MaxBotix MB7389-100 ultrasonic sensor
//
//  Reads range over UART serial (9600 baud, 8N1) as ASCII frames
//  of the form "Rxxxx\r" where xxxx is the range in millimetres.
//  Unlike the HC-SR04 (trigger/echo GPIO timing), this sensor
//  streams readings continuously; measure() grabs the next valid
//  frame and converts it to a water depth.
//
//  This sensor reports DEPTH directly: depth = total_length −
//  measured_distance, both expressed in the configured unit.
//  total_length and the reported unit come from config.
/////////////////////////////////////////////////////////////

#ifndef MAXBOTIXSENSOR_H
#define MAXBOTIXSENSOR_H

#include "Sensor.h"
#include <QString>

class MaxbotixSensor : public Sensor {
    Q_OBJECT

public:
    // device      — serial port, e.g. "/dev/ttyAMA0"
    // unit        — "mm", "cm", or "in"; drives both the reported unit
    //               AND the conversion applied to the raw mm reading
    // totalLength — full pipe/standpipe length in the configured unit;
    //               depth = totalLength − measuredDistance
    MaxbotixSensor(const QString &id, const QString &unit,
                   const QString &name, const QString &device,
                   double totalLength);
    ~MaxbotixSensor() override;

    bool   initialize() override;   // Open + configure UART; false if unavailable
    void   cleanup() override;      // Close the serial port
    double measure() override;      // Water depth in configured unit, or -1

private:
    // Convert a raw millimetre range into the configured unit.
    double mmToUnit(int rangeMm) const;

    QString m_device;        // serial device path
    double  m_totalLength;   // in configured unit
    int     m_fd = -1;       // open file descriptor (-1 = closed)

    static constexpr int MIN_RANGE_MM = 500;    // sensor min (below = too close)
    static constexpr int MAX_RANGE_MM = 5000;   // sensor max (at/above = no target)
    static constexpr int READ_TIMEOUT_MS = 1000; // give up after this per measure()
};

#endif // MAXBOTIXSENSOR_H
