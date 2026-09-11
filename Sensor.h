/////////////////////////////////////////////////////////////
// SENSOR.H - Abstract sensor interface
//
//  Base class for every sensor in the system. Subclasses
//  implement the hardware-specific parts; the rest of the app
//  only ever talks to a Sensor*.
//
//  Reading contract:
//    measure() takes one raw sample and returns its value, or a
//    negative number (-1) to signal "no valid reading" — no GPIO,
//    sensor missing/not responding, or out of range. Subclasses
//    implement it.
//
//    takeReading() is what the app calls: it averages
//    samplesPerReading() samples to damp the noise a single sample
//    carries. Use isValid() to test a reading, or isAvailable() to
//    test the sensor. (It is not called read() so that it can't
//    hide POSIX read() inside a subclass — MaxbotixSensor talks to
//    a serial fd.)
//
//  Recovery contract:
//    On Raspberry Pi, an unavailable sensor is retried every 30 s.
//    Three consecutive completely-invalid readings also mark an
//    otherwise-initialized sensor unavailable, allowing a temporarily
//    disconnected GPIO/UART device to rejoin without restarting the app.
/////////////////////////////////////////////////////////////

#ifndef SENSOR_H
#define SENSOR_H

#include <QObject>
#include <QString>
#include <QDebug>
#include <QTimer>
#include <QDateTime>

#include <chrono>
#include <thread>

class Sensor : public QObject {
    Q_OBJECT

public:
    // id   — stable identifier sent to the DB as "sensor_id"
    // unit — measurement unit sent to the DB (e.g. "cm", "%", "C")
    // name — human-friendly label for the UI (defaults to id)
    explicit Sensor(const QString &id, const QString &unit,
                    const QString &name = QString(),
                    QObject *parent = nullptr)
        : QObject(parent),
          m_id(id),
          m_unit(unit),
          m_name(name.isEmpty() ? id : name) {
#ifdef RasPi
        m_recoveryTimer.setInterval(RECOVERY_INTERVAL_MS);
        m_recoveryTimer.setSingleShot(false);
        connect(&m_recoveryTimer, &QTimer::timeout, this, [this]() {
            if (m_available) {
                m_recoveryTimer.stop();
                return;
            }

            cleanup();
            qInfo() << "Sensor" << m_id << ": attempting recovery";
            if (initialize()) {
                m_consecutiveFailures = 0;
                qInfo() << "Sensor" << m_id << ": recovered";
                m_recoveryTimer.stop();
            }
        });
#endif
    }

    ~Sensor() override = default;

    virtual bool initialize() = 0;
    virtual double measure() = 0;

    double takeReading() {
        const int n = m_samplesPerReading > 0 ? m_samplesPerReading : 1;

        double sum   = 0.0;
        int    valid = 0;

        for (int i = 0; i < n; ++i) {
            if (i > 0)
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(INTER_SAMPLE_MS));

            const double v = measure();
            if (isValid(v)) {
                sum += v;
                ++valid;
            }
        }

        if (valid == 0) {
            ++m_consecutiveFailures;
            m_lastFailure = QDateTime::currentDateTime();
#ifdef RasPi
            if (m_available && m_consecutiveFailures >= FAILURE_LIMIT) {
                qWarning() << "Sensor" << m_id << ":"
                           << m_consecutiveFailures
                           << "consecutive readings failed — scheduling recovery";
                setAvailable(false);
            }
#endif
            return -1;
        }

        m_consecutiveFailures = 0;
        m_lastValidReading = QDateTime::currentDateTime();

        if (valid < n)
            qWarning() << "Sensor" << m_id << ": averaged" << valid
                       << "of" << n << "samples —" << (n - valid) << "failed";

        return sum / valid;
    }

    virtual void cleanup() {}

    bool    isAvailable() const { return m_available; }
    QString id() const          { return m_id; }
    QString unit() const        { return m_unit; }
    QString displayName() const { return m_name; }

    // Health-monitoring accessors. These deliberately expose observation
    // state, not hardware-specific implementation details.
    int consecutiveFailures() const { return m_consecutiveFailures; }
    QDateTime lastValidReading() const { return m_lastValidReading; }
    QDateTime lastFailure() const { return m_lastFailure; }

    int  pollIntervalSeconds() const     { return m_intervalSeconds; }
    void setPollIntervalSeconds(int s)   { m_intervalSeconds = s; }

    int  samplesPerReading() const       { return m_samplesPerReading; }
    void setSamplesPerReading(int n)     { m_samplesPerReading = n > 0 ? n : 1; }

    double fullScale() const { return m_fullScale; }

    static bool isValid(double reading) { return reading >= 0.0; }

protected:
    void setAvailable(bool a) {
        m_available = a;
        if (a) {
            m_consecutiveFailures = 0;
#ifdef RasPi
            m_recoveryTimer.stop();
#endif
        } else {
            m_lastFailure = QDateTime::currentDateTime();
#ifdef RasPi
            if (!m_recoveryTimer.isActive())
                m_recoveryTimer.start();
#endif
        }
    }

    void setFullScale(double fs) { m_fullScale = fs; }

private:
    static constexpr int INTER_SAMPLE_MS = 60;
    static constexpr int RECOVERY_INTERVAL_MS = 30 * 1000;
    static constexpr int FAILURE_LIMIT = 3;

    QString m_id;
    QString m_unit;
    QString m_name;
    bool    m_available = false;
    double  m_fullScale = 0.0;
    int     m_intervalSeconds = 0;
    int     m_samplesPerReading = 1;
    int     m_consecutiveFailures = 0;
    QDateTime m_lastValidReading;
    QDateTime m_lastFailure;
#ifdef RasPi
    QTimer  m_recoveryTimer;
#endif
};

#endif // SENSOR_H
