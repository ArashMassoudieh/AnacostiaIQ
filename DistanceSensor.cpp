/////////////////////////////////////////////////////////////
// DISTANCESENSOR.CPP - HC-SR04 ultrasonic sensor (libgpiod)
//
//  Pin connections (Pi -> HC-SR04):
//    5V      -> VCC
//    TRIG    <- GPIO trigPin  (output)
//    ECHO    -> GPIO echoPin  (input)
//    GND     -> GND
//
//  Uses libgpiod v2 C++ API (gpiod.hpp), matching the validated
//  standalone test program.
/////////////////////////////////////////////////////////////

#include "DistanceSensor.h"
#include <QDebug>

#ifdef RasPi
#include <thread>
#include <chrono>
using namespace std::chrono;
#endif

DistanceSensor::DistanceSensor(const QString &id, const QString &unit,
                               const QString &name, int trigPin, int echoPin,
                               double totalLength, const QString &chip)
    : Sensor(id, unit, name),
      m_trigPin(trigPin), m_echoPin(echoPin),
      m_totalLength(totalLength), m_chipPath(chip) {
    setFullScale(totalLength);
}

DistanceSensor::~DistanceSensor() {
    cleanup();
}

double DistanceSensor::inchesToUnit(double inches) const {
    const QString u = unit().toLower();
    if (u == "mm") return inches * 25.4;
    if (u == "cm") return inches * 2.54;
    return inches;                 // "in" (or unknown) -> inches
}

#ifdef RasPi
namespace {
// Poll a GPIO line until it reaches the target value, or timeout.
bool waitForState(gpiod::line_request &req, unsigned int line,
                  gpiod::line::value target, milliseconds timeout) {
    auto start = steady_clock::now();
    while (req.get_value(line) != target) {
        if (steady_clock::now() - start > timeout)
            return false;
        std::this_thread::sleep_for(microseconds(10));  // ease CPU
    }
    return true;
}
} // namespace
#endif

bool DistanceSensor::initialize() {
#ifdef RasPi
    try {
        m_chip = std::make_unique<gpiod::chip>(m_chipPath.toStdString());

        // TRIG as output
        m_trigReq = std::make_unique<gpiod::line_request>(
            m_chip->prepare_request()
                .set_consumer("anacostiaiq-trig")
                .add_line_settings(
                    m_trigPin,
                    gpiod::line_settings().set_direction(
                        gpiod::line::direction::OUTPUT))
                .do_request());

        // ECHO as input
        m_echoReq = std::make_unique<gpiod::line_request>(
            m_chip->prepare_request()
                .set_consumer("anacostiaiq-echo")
                .add_line_settings(
                    m_echoPin,
                    gpiod::line_settings().set_direction(
                        gpiod::line::direction::INPUT))
                .do_request());

        setAvailable(true);
        return true;
    }
    catch (const std::exception &e) {
        qWarning() << "DistanceSensor: GPIO init failed:" << e.what();
        cleanup();
        setAvailable(false);
        return false;
    }
#else
    // No GPIO on this system (e.g. desktop/dev machine)
    qWarning() << "DistanceSensor: no GPIO on this system "
                  "— distance sensor unavailable";
    setAvailable(false);
    return false;
#endif
}

void DistanceSensor::cleanup() {
#ifdef RasPi
    m_echoReq.reset();
    m_trigReq.reset();
    m_chip.reset();
#endif
}

double DistanceSensor::measure() {
#ifdef RasPi
    if (!m_trigReq || !m_echoReq)
        return -1;

    try {
        // ── Trigger pulse: 10 µs HIGH ──────────────────────
        m_trigReq->set_value(m_trigPin, gpiod::line::value::INACTIVE);
        std::this_thread::sleep_for(microseconds(2));
        m_trigReq->set_value(m_trigPin, gpiod::line::value::ACTIVE);
        std::this_thread::sleep_for(microseconds(10));
        m_trigReq->set_value(m_trigPin, gpiod::line::value::INACTIVE);

        // ── Wait for ECHO HIGH (pulse start) ───────────────
        if (!waitForState(*m_echoReq, m_echoPin,
                          gpiod::line::value::ACTIVE,
                          milliseconds(ECHO_TIMEOUT_MS))) {
            qWarning() << "DistanceSensor: timeout waiting for ECHO HIGH"
                       << "— sensor may be disconnected";
            return -1;
        }
        auto start = steady_clock::now();

        // ── Wait for ECHO LOW (pulse end) ──────────────────
        if (!waitForState(*m_echoReq, m_echoPin,
                          gpiod::line::value::INACTIVE,
                          milliseconds(ECHO_TIMEOUT_MS))) {
            qWarning() << "DistanceSensor: timeout waiting for ECHO LOW"
                       << "— sensor may be malfunctioning";
            return -1;
        }
        auto end = steady_clock::now();

        // ── Distance from pulse duration ───────────────────
        auto durationUs = duration_cast<microseconds>(end - start).count();
        double twoWayInches = durationUs * SOUND_SPEED_IN_PER_US;
        double measuredInches = twoWayInches / 2.0;

        // Convert to configured unit, then to depth
        double measured = inchesToUnit(measuredInches);
        double depth = m_totalLength - measured;
        if (depth < 0)
            depth = 0;
        return depth;
    }
    catch (const std::exception &e) {
        qWarning() << "DistanceSensor: read failed:" << e.what();
        return -1;
    }
#else
    return -1;
#endif
}
