/////////////////////////////////////////////////////////////
// SENSOR.H - Abstract sensor interface
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
#include <cmath>

class Sensor : public QObject {
    Q_OBJECT

public:
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
            if (m_available && !m_recoveryPending) {
                m_recoveryTimer.stop();
                return;
            }

            cleanup();
            qInfo() << "Sensor" << m_id << ": attempting recovery";
            if (initialize()) {
                // Hardware initialization only proves that descriptors/GPIO can
                // be opened. Do not call the sensor recovered until real data
                // have been received successfully.
                m_recoveryPending = true;
                m_recoverySuccesses = 0;
                m_consecutiveFailures = 0;
                qInfo() << "Sensor" << m_id
                        << ": hardware reinitialized; awaiting valid readings";
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

        double sum = 0.0;
        int valid = 0;

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
            m_identicalValidReadings = 0;
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

        const double reading = sum / valid;
        m_consecutiveFailures = 0;
        m_lastValidReading = QDateTime::currentDateTime();

        // Keep a small generic stuck-value statistic. HealthMonitor decides
        // whether an unchanged value is suspicious for a particular sensor.
        if (m_lastValueValid && std::fabs(reading - m_lastValue) < 1e-9)
            ++m_identicalValidReadings;
        else
            m_identicalValidReadings = 1;
        m_lastValue = reading;
        m_lastValueValid = true;

        if (m_recoveryPending) {
            ++m_recoverySuccesses;
            if (m_recoverySuccesses >= RECOVERY_SUCCESS_LIMIT) {
                m_recoveryPending = false;
                qInfo() << "Sensor" << m_id
                        << ": recovered after"
                        << m_recoverySuccesses << "valid reading(s)";
            }
        }

        if (valid < n)
            qWarning() << "Sensor" << m_id << ": averaged" << valid
                       << "of" << n << "samples —" << (n - valid) << "failed";

        return reading;
    }

    virtual void cleanup() {}

    bool    isAvailable() const { return m_available; }
    QString id() const          { return m_id; }
    QString unit() const        { return m_unit; }
    QString displayName() const { return m_name; }

    int consecutiveFailures() const { return m_consecutiveFailures; }
    QDateTime lastValidReading() const { return m_lastValidReading; }
    QDateTime lastFailure() const { return m_lastFailure; }
    bool recoveryPending() const { return m_recoveryPending; }
    int recoverySuccesses() const { return m_recoverySuccesses; }
    bool hasLastValue() const { return m_lastValueValid; }
    double lastValue() const { return m_lastValue; }
    int identicalValidReadings() const { return m_identicalValidReadings; }

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
            // initialize() may be called by the recovery timer. The timer
            // callback sets m_recoveryPending immediately after initialize()
            // returns, so stopping here is safe and prevents duplicate retries.
            m_recoveryTimer.stop();
#endif
        } else {
            m_lastFailure = QDateTime::currentDateTime();
            m_recoveryPending = false;
            m_recoverySuccesses = 0;
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
    static constexpr int RECOVERY_SUCCESS_LIMIT = 2;

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

    bool   m_recoveryPending = false;
    int    m_recoverySuccesses = 0;
    bool   m_lastValueValid = false;
    double m_lastValue = 0.0;
    int    m_identicalValidReadings = 0;
#ifdef RasPi
    QTimer  m_recoveryTimer;
#endif
};

#endif // SENSOR_H
