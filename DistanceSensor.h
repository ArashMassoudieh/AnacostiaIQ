/////////////////////////////////////////////////////////////
// DISTANCESENSOR.H - HC-SR04 ultrasonic sensor (libgpiod)
//
//  Trigger/echo timing over GPIO using libgpiod (v2 C++ API).
//  Reports water DEPTH directly: depth = totalLength − measured,
//  both in the configured unit ("mm", "cm", or "in"). Pins are
//  BCM (GPIO) line offsets, supplied from config.
/////////////////////////////////////////////////////////////

#ifndef DISTANCESENSOR_H
#define DISTANCESENSOR_H

#include "Sensor.h"
#include <QString>
#include <memory>
#ifdef RasPi
#include <gpiod.hpp>
#endif

class DistanceSensor : public Sensor {
    Q_OBJECT

public:
    // unit        — "mm", "cm", or "in"; drives both the reported unit
    //               and the conversion applied to the measured distance
    // trigPin/echoPin — BCM GPIO line offsets
    // totalLength — full pipe length in the configured unit;
    //               depth = totalLength − measuredDistance
    // chip        — gpiochip device path, e.g. "/dev/gpiochip0"
    DistanceSensor(const QString &id, const QString &unit,
                   const QString &name, int trigPin, int echoPin,
                   double totalLength, const QString &chip);
    ~DistanceSensor() override;

    bool   initialize() override;   // Request GPIO lines; false if unavailable
    void   cleanup() override;      // Release the lines
    double measure() override;      // Water depth in configured unit, or -1

private:
    // Convert a raw one-way distance (inches, as computed from timing)
    // into the configured unit.
    double inchesToUnit(double inches) const;

    int     m_trigPin;
    int     m_echoPin;
    double  m_totalLength;   // in configured unit
    QString m_chipPath;

#ifdef RasPi
    std::unique_ptr<gpiod::chip>         m_chip;
    std::unique_ptr<gpiod::line_request> m_trigReq;
    std::unique_ptr<gpiod::line_request> m_echoReq;
#endif

    // Speed of sound: 0.0135 inches per microsecond (round trip math
    // halves this). Matches the validated test program.
    static constexpr double SOUND_SPEED_IN_PER_US = 0.0135;
    static constexpr int ECHO_TIMEOUT_MS = 50;   // per edge, as in test code
};

#endif // DISTANCESENSOR_H
