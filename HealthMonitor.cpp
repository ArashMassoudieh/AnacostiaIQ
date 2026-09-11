#include "HealthMonitor.h"

#include "DatabaseWriter.h"
#include "Sensor.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStorageInfo>

HealthMonitor::HealthMonitor(DatabaseWriter *writer, QObject *parent)
    : QObject(parent), m_writer(writer)
{
    m_timer.setSingleShot(false);
    connect(&m_timer, &QTimer::timeout, this, &HealthMonitor::evaluate);
}

void HealthMonitor::setSensors(const QVector<Sensor *> &sensors)
{
    m_sensors = sensors;
}

void HealthMonitor::start(int intervalSeconds, int heartbeatSeconds)
{
    m_heartbeatSeconds = qMax(30, heartbeatSeconds);
    m_timer.start(qMax(5, intervalSeconds) * 1000);
    QTimer::singleShot(0, this, &HealthMonitor::evaluate);
}

void HealthMonitor::stop()
{
    m_timer.stop();
}

void HealthMonitor::evaluateNow()
{
    evaluate();
}

QString HealthMonitor::levelName(Level level)
{
    switch (level) {
    case Healthy:  return "healthy";
    case Degraded: return "degraded";
    case Offline:  return "offline";
    case Critical: return "critical";
    }
    return "unknown";
}

HealthMonitor::Level HealthMonitor::worst(Level a, Level b)
{
    return static_cast<Level>(qMax(static_cast<int>(a), static_cast<int>(b)));
}

void HealthMonitor::evaluate()
{
    const QDateTime now = QDateTime::currentDateTime();
    Level overall = Healthy;

    updateComponent("application", Healthy, "monitor_running", now);

    for (Sensor *sensor : m_sensors) {
        if (!sensor)
            continue;

        Level level = Healthy;
        QString reason = "ok";

        if (!sensor->isAvailable()) {
            level = Offline;
            reason = "sensor_unavailable";
        } else if (sensor->recoveryPending()) {
            level = Degraded;
            reason = QString("recovery_validation_%1_of_2")
                         .arg(sensor->recoverySuccesses());
        } else if (sensor->consecutiveFailures() >= 2) {
            level = Degraded;
            reason = QString("%1_consecutive_failures")
                         .arg(sensor->consecutiveFailures());
        } else if (!sensor->lastValidReading().isValid()) {
            // Opening a GPIO/UART/ADC successfully does not prove the physical
            // sensor or its wire is healthy. Stay degraded until the first real
            // valid reading arrives.
            level = Degraded;
            reason = "awaiting_first_valid_reading";
        } else {
            const int base = qMax(1, sensor->pollIntervalSeconds());
            const qint64 age = sensor->lastValidReading().secsTo(now);
            if (age > qMax(60, base * 3)) {
                level = Offline;
                reason = QString("stale_%1s").arg(age);
            }
        }

        // A moisture value exactly at a calibration rail can be real, but if
        // it stays bit-for-bit unchanged for several complete readings it is
        // suspicious enough to flag as degraded for field inspection.
        if (level == Healthy &&
            sensor->id().contains("moisture", Qt::CaseInsensitive) &&
            sensor->hasLastValue() &&
            (sensor->lastValue() <= 0.01 || sensor->lastValue() >= 99.99) &&
            sensor->identicalValidReadings() >= 5) {
            level = Degraded;
            reason = QString("stuck_at_boundary_%1")
                         .arg(sensor->lastValue(), 0, 'f', 2);
        }

        updateComponent("sensor_" + sensor->id(), level, reason, now);
        overall = worst(overall, level);
    }

    if (m_writer) {
        Level cloudLevel = Healthy;
        QString cloudReason = "ok";
        if (m_writer->consecutiveFailures() >= 3) {
            cloudLevel = Offline;
            cloudReason = "api_unreachable";
        } else if (m_writer->consecutiveFailures() > 0) {
            cloudLevel = Degraded;
            cloudReason = "api_retrying";
        }
        updateComponent("cloud", cloudLevel, cloudReason, now);
        overall = worst(overall, cloudLevel);

        const int queued = m_writer->pendingCount();
        Level queueLevel = Healthy;
        QString queueReason = QString("%1_pending").arg(queued);
        if (queued >= QUEUE_CRITICAL)
            queueLevel = Critical;
        else if (queued >= QUEUE_WARN)
            queueLevel = Degraded;
        updateComponent("upload_queue", queueLevel, queueReason, now);
        overall = worst(overall, queueLevel);
    }

    QStorageInfo storage(QDir::rootPath());
    if (storage.isValid() && storage.isReady()) {
        const qint64 available = storage.bytesAvailable();
        Level diskLevel = Healthy;
        const QString diskReason =
            QString("%1_mb_free").arg(available / (1024 * 1024));
        if (available <= DISK_CRITICAL_BYTES)
            diskLevel = Critical;
        else if (available <= DISK_WARN_BYTES)
            diskLevel = Degraded;
        updateComponent("storage", diskLevel, diskReason, now);
        overall = worst(overall, diskLevel);
    }

    const double temp = cpuTemperatureC();
    if (temp >= 0.0) {
        Level cpuLevel = Healthy;
        const QString cpuReason = QString::number(temp, 'f', 1) + "C";
        if (temp >= CPU_CRITICAL_C)
            cpuLevel = Critical;
        else if (temp >= CPU_WARN_C)
            cpuLevel = Degraded;
        updateComponent("cpu_temperature", cpuLevel, cpuReason, now);
        overall = worst(overall, cpuLevel);
    }

    updateComponent("overall", overall, levelName(overall), now);
    writeSnapshot(now);
}

void HealthMonitor::updateComponent(const QString &id, Level level,
                                    const QString &reason,
                                    const QDateTime &now)
{
    ComponentState &state = m_states[id];
    // Only a LEVEL transition is alarm-worthy. Reasons such as queue depth,
    // free space, temperature, and stale age can change every evaluation.
    const bool changed = !state.initialized || state.level != level;

    if (changed) {
        state.level = level;
        state.changedAt = now;
        state.initialized = true;
        qInfo().noquote() << QString("Health %1: %2 (%3)")
                                 .arg(id, levelName(level), reason);
    }
    state.reason = reason;

    publishIfNeeded(id, state, now, changed);
}

void HealthMonitor::publishIfNeeded(const QString &id, ComponentState &state,
                                    const QDateTime &now, bool force)
{
    if (!m_writer)
        return;

    // Component telemetry is transition-only. Only application and overall
    // station state need a periodic heartbeat for remote liveness detection.
    // This keeps health monitoring from amplifying an existing offline queue.
    const bool heartbeatComponent = (id == "application" || id == "overall");
    const bool heartbeatDue = heartbeatComponent &&
        (!state.lastPublishedAt.isValid() ||
         state.lastPublishedAt.secsTo(now) >= m_heartbeatSeconds);

    if (!force && !heartbeatDue)
        return;

    m_writer->sendReading("health_" + id,
                          static_cast<int>(state.level), "state", now);
    state.lastPublishedAt = now;
}

QString HealthMonitor::snapshotPath() const
{
    QString root = qEnvironmentVariable("XDG_STATE_HOME");
    if (root.isEmpty())
        root = QDir::homePath() + "/.local/state";
    const QString dir = root + "/anacostiaiq";
    QDir().mkpath(dir);
    return dir + "/health.json";
}

void HealthMonitor::writeSnapshot(const QDateTime &now)
{
    QJsonObject components;
    for (auto it = m_states.cbegin(); it != m_states.cend(); ++it) {
        QJsonObject c;
        c["level"] = static_cast<int>(it.value().level);
        c["status"] = levelName(it.value().level);
        c["reason"] = it.value().reason;
        if (it.value().changedAt.isValid())
            c["changed_at"] = it.value().changedAt.toString(Qt::ISODate);
        components[it.key()] = c;
    }

    QJsonObject root;
    root["timestamp"] = now.toString(Qt::ISODate);
    root["components"] = components;

    QSaveFile file(snapshotPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "HealthMonitor: cannot write" << snapshotPath();
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit())
        qWarning() << "HealthMonitor: cannot commit" << snapshotPath();
}

double HealthMonitor::cpuTemperatureC()
{
    QFile file("/sys/class/thermal/thermal_zone0/temp");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return -1.0;

    bool ok = false;
    const double milliC =
        QString::fromLatin1(file.readAll()).trimmed().toDouble(&ok);
    return ok ? milliC / 1000.0 : -1.0;
}
