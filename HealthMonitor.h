#ifndef HEALTHMONITOR_H
#define HEALTHMONITOR_H

#include <QObject>
#include <QDateTime>
#include <QMap>
#include <QString>
#include <QTimer>
#include <QVector>

class DatabaseWriter;
class Sensor;

// Field-station health telemetry.
// Numeric state codes are deliberately simple so they can travel through the
// existing /sensor API without requiring a server migration:
//   0 = healthy, 1 = degraded, 2 = offline, 3 = critical.
// A human-readable snapshot is also written locally to
// ~/.local/state/anacostiaiq/health.json.
class HealthMonitor : public QObject
{
    Q_OBJECT

public:
    struct Thresholds {
        int sensorDegradedFailures = 2;
        int sensorStaleMinimumSeconds = 60;
        int sensorStalePollFactor = 3;
        int moistureDegradedBoundaryReadings = 1;
        int moistureOfflineBoundaryReadings = 3;
        int cloudDegradedFailures = 1;
        int cloudOfflineFailures = 3;
        int queueDegraded = 500;
        int queueCritical = 2000;
        qint64 diskDegradedBytes = 500LL * 1024LL * 1024LL;
        qint64 diskCriticalBytes = 100LL * 1024LL * 1024LL;
        double cpuDegradedC = 70.0;
        double cpuCriticalC = 80.0;
    };

    enum Level {
        Healthy  = 0,
        Degraded = 1,
        Offline  = 2,
        Critical = 3
    };
    Q_ENUM(Level)

    explicit HealthMonitor(DatabaseWriter *writer, QObject *parent = nullptr);

    void setSensors(const QVector<Sensor *> &sensors);
    void setStationIdentity(const QString &id, const QString &name = QString());
    void setThresholds(const Thresholds &thresholds);
    void start(int intervalSeconds = 30, int heartbeatSeconds = 300);
    void stop();
    void evaluateNow();

    static QString levelName(Level level);

private:
    struct ComponentState {
        Level level = Healthy;
        QString reason = "ok";
        QDateTime changedAt;
        QDateTime lastPublishedAt;
        bool initialized = false;
    };

    void evaluate();
    void updateComponent(const QString &id, Level level, const QString &reason,
                         const QDateTime &now);
    void publishIfNeeded(const QString &id, ComponentState &state,
                         const QDateTime &now, bool force = false);
    void writeSnapshot(const QDateTime &now);
    QString snapshotPath() const;

    static double cpuTemperatureC();
    static Level worst(Level a, Level b);

    DatabaseWriter *m_writer = nullptr;
    QVector<Sensor *> m_sensors;
    QMap<QString, ComponentState> m_states;
    QTimer m_timer;
    int m_heartbeatSeconds = 300;
    QString m_stationId = "station_01";
    QString m_stationName = "AnacostiaIQ Station";
    Thresholds m_thresholds;
};

#endif // HEALTHMONITOR_H
