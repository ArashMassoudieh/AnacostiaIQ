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

void HealthMonitor::setStationIdentity(const QString &id, const QString &name)
{
    QString clean = id.trimmed().toLower();
    clean.replace('-', '_');
    clean.replace(' ', '_');
    if (!clean.isEmpty())
        m_stationId = clean;
    if (!name.trimmed().isEmpty())
        m_stationName = name.trimmed();
}

void HealthMonitor::setThresholds(const Thresholds &thresholds)
{
    m_thresholds = thresholds;
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
            // Reopening the descriptor/GPIO after an offline failure is not a
            // recovery. Keep the alarm OFFLINE until real data return. One
            // successful reading is enough to show progress as DEGRADED; two
            // consecutive successful readings clear recoveryPending and allow
            // the normal HEALTHY state. This prevents offline/degraded alarm
            // flapping every time a dead sensor is periodically reinitialized.
            if (sensor->recoverySuccesses() == 0) {
                level = Offline;
                reason = "recovery_validation_0_of_2";
            } else {
                level = Degraded;
                reason = QString("recovery_validation_%1_of_2")
                             .arg(sensor->recoverySuccesses());
            }
        } else if (sensor->consecutiveFailures() >= m_thresholds.sensorDegradedFailures) {
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
            if (age > qMax(m_thresholds.sensorStaleMinimumSeconds,
                           base * m_thresholds.sensorStalePollFactor)) {
                level = Offline;
                reason = QString("stale_%1s").arg(age);
            }
        }

        // Explicit physical-disconnect detection for configured moisture
        // probes. This is intentionally independent of the ADC transport
        // (legacy GPIO ADC or ADS1115): a disconnected/open probe can still
        // produce a syntactically valid calibrated boundary value of 0 or
        // 100 %. One/two repeated boundary readings are suspicious; three or
        // more indicate a likely disconnected/open sensor.
        if ((level == Healthy || level == Degraded) &&
            sensor->id().contains("moisture", Qt::CaseInsensitive) &&
            sensor->hasLastValue() &&
            (sensor->lastValue() <= 0.01 || sensor->lastValue() >= 99.99)) {
            const int rails = sensor->identicalValidReadings();
            if (rails >= m_thresholds.moistureOfflineBoundaryReadings) {
                level = Offline;
                reason = QString("sensor_disconnected_boundary_%1")
                             .arg(sensor->lastValue(), 0, 'f', 2);
            } else if (rails >= m_thresholds.moistureDegradedBoundaryReadings) {
                level = Degraded;
                reason = QString("suspect_boundary_%1_%2_of_%3")
                             .arg(sensor->lastValue(), 0, 'f', 2)
                             .arg(rails)
                             .arg(m_thresholds.moistureOfflineBoundaryReadings);
            }
        }

        updateComponent("sensor_" + sensor->id(), level, reason, now);
        overall = worst(overall, level);
    }

    if (m_writer) {
        Level cloudLevel = Healthy;
        QString cloudReason = "ok";
        if (m_writer->consecutiveFailures() >= m_thresholds.cloudOfflineFailures) {
            cloudLevel = Offline;
            cloudReason = "api_unreachable";
        } else if (m_writer->consecutiveFailures() >= m_thresholds.cloudDegradedFailures) {
            cloudLevel = Degraded;
            cloudReason = "api_retrying";
        }
        updateComponent("cloud", cloudLevel, cloudReason, now);
        overall = worst(overall, cloudLevel);

        const int queued = m_writer->pendingCount();
        Level queueLevel = Healthy;
        QString queueReason = QString("%1_pending").arg(queued);
        if (queued >= m_thresholds.queueCritical)
            queueLevel = Critical;
        else if (queued >= m_thresholds.queueDegraded)
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
        if (available <= m_thresholds.diskCriticalBytes)
            diskLevel = Critical;
        else if (available <= m_thresholds.diskDegradedBytes)
            diskLevel = Degraded;
        updateComponent("storage", diskLevel, diskReason, now);
        overall = worst(overall, diskLevel);
    }

    const double temp = cpuTemperatureC();
    if (temp >= 0.0) {
        Level cpuLevel = Healthy;
        const QString cpuReason = QString::number(temp, 'f', 1) + "C";
        if (temp >= m_thresholds.cpuCriticalC)
            cpuLevel = Critical;
        else if (temp >= m_thresholds.cpuDegradedC)
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
        qInfo().noquote() << QString("Health %1/%2: %3 (%4)")
                                 .arg(m_stationId, id, levelName(level), reason);
    }
    state.reason = reason;

    publishIfNeeded(id, state, now, changed);
}

void HealthMonitor::publishIfNeeded(const QString &id, ComponentState &state,
                                    const QDateTime &now, bool force)
{
    if (!m_writer)
        return;

    // Publish every component immediately on a level transition and refresh
    // it periodically as a heartbeat. The remote health dashboard reads these
    // component records independently; transition-only telemetry eventually
    // made a continuously healthy component appear UNKNOWN after the portal's
    // lookback window expired. The heartbeat interval remains configurable and
    // health records retain priority in DatabaseWriter's persistent queue.
    const bool heartbeatDue =
        !state.lastPublishedAt.isValid() ||
        state.lastPublishedAt.secsTo(now) >= m_heartbeatSeconds;

    if (!force && !heartbeatDue)
        return;

    // The API already preserves this text field. For health telemetry it
    // carries the current human-readable reason while the numeric value
    // remains the stable 0..3 state code. Older readers that ignore the field
    // continue to work unchanged.
    m_writer->sendReading("health_" + m_stationId + "_" + id,
                          static_cast<int>(state.level), state.reason, now);
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
    root["station_id"] = m_stationId;
    root["station_name"] = m_stationName;
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
