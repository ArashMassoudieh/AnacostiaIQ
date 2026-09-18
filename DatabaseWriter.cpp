/////////////////////////////////////////////////////////////
// DATABASEWRITER.CPP - Database Writer Implementation
/////////////////////////////////////////////////////////////

#include "DatabaseWriter.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVariant>

namespace {
QByteArray queueRecordKey(const QJsonObject &json)
{
    // Queue identity is the complete payload, not just sensor_id/timestamp.
    // This suppresses identical forecast records fetched again after a restart
    // while still allowing a provider to publish a revised value for the same
    // forecast timestamp.
    return QJsonDocument(json).toJson(QJsonDocument::Compact);
}
}

DatabaseWriter::DatabaseWriter(QObject *parent)
    : QObject(parent)
{
    manager = new QNetworkAccessManager(this);
    apiUrl = QUrl("http://54.213.147.59:5000/sensor");

    queuePath = resolveQueuePath();
    acknowledgementPath = queuePath + ".acks";
    loadQueue();

    retryTimer.setSingleShot(true);
    connect(&retryTimer, &QTimer::timeout,
            this, &DatabaseWriter::trySendNext);

    if (!pendingKeys.isEmpty()) {
        qInfo() << "Loaded" << pendingKeys.size()
                << "pending cloud write(s) from" << queuePath;
        QTimer::singleShot(0, this, &DatabaseWriter::trySendNext);
    }
}

void DatabaseWriter::setApiUrl(const QString &url)
{
    apiUrl = QUrl(url);
    if (!pendingKeys.isEmpty() &&
        activeReplies.size() < MAX_CONCURRENT_UPLOADS &&
        !retryTimer.isActive())
        QTimer::singleShot(0, this, &DatabaseWriter::trySendNext);
}

QString DatabaseWriter::resolveQueuePath() const
{
    QString stateRoot = qEnvironmentVariable("XDG_STATE_HOME");
    if (stateRoot.isEmpty())
        stateRoot = QDir::homePath() + "/.local/state";

    const QString dirPath = stateRoot + "/anacostiaiq";
    QDir().mkpath(dirPath);
    return dirPath + "/upload-queue.jsonl";
}

void DatabaseWriter::loadQueue()
{
    pending.clear();
    pendingKeys.clear();
    acknowledgementsSinceCompaction = 0;

    QSet<QByteArray> acknowledgedKeys;
    int badAcknowledgements = 0;
    QFile acknowledgements(acknowledgementPath);
    if (acknowledgements.exists()) {
        if (!acknowledgements.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "Cannot open upload acknowledgement journal:"
                       << acknowledgementPath;
        } else {
            while (!acknowledgements.atEnd()) {
                const QByteArray encoded = acknowledgements.readLine().trimmed();
                if (encoded.isEmpty())
                    continue;

                const QByteArray key = QByteArray::fromBase64(encoded);
                if (key.isEmpty() || key.toBase64() != encoded) {
                    ++badAcknowledgements;
                    continue;
                }
                acknowledgedKeys.insert(key);
            }
            acknowledgements.close();
        }
    }

    QFile f(queuePath);
    if (f.exists() && !f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Cannot open persistent upload queue:" << queuePath;
        return;
    }

    int badLines = 0;
    int duplicateLines = 0;
    int acknowledgedLines = 0;
    while (f.isOpen() && !f.atEnd()) {
        const QByteArray line = f.readLine().trimmed();
        if (line.isEmpty())
            continue;

        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            ++badLines;
            continue;
        }

        const QJsonObject json = doc.object();
        const QByteArray key = queueRecordKey(json);
        if (acknowledgedKeys.contains(key)) {
            ++acknowledgedLines;
            continue;
        }
        if (pendingKeys.contains(key)) {
            ++duplicateLines;
            continue;
        }

        pendingKeys.insert(key);
        pending.append(json);
    }
    if (f.isOpen())
        f.close();

    if (badLines > 0)
        qWarning() << "Ignored" << badLines
                   << "corrupt upload-queue record(s)";

    if (duplicateLines > 0) {
        qInfo() << "Removed" << duplicateLines
                << "duplicate persistent upload-queue record(s)";
    }

    if (badAcknowledgements > 0)
        qWarning() << "Ignored" << badAcknowledgements
                   << "invalid upload acknowledgement(s)";

    if (acknowledgedLines > 0)
        qInfo() << "Recovered" << acknowledgedLines
                << "acknowledged cloud write(s) from the journal";

    // Collapse recovered acknowledgements and duplicate queue entries into a
    // clean source-of-truth file. The journal is cleared only after the queue
    // replacement commits, so a crash cannot resurrect acknowledged records.
    if ((!acknowledgedKeys.isEmpty() || duplicateLines > 0) &&
        rewriteQueueFile() && !acknowledgedKeys.isEmpty()) {
        clearAcknowledgementFile();
    }
}

bool DatabaseWriter::appendToQueueFile(const QJsonObject &json)
{
    QFile f(queuePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qCritical() << "Cannot persist sensor reading to" << queuePath;
        return false;
    }

    const QByteArray line = QJsonDocument(json).toJson(QJsonDocument::Compact) + '\n';
    if (f.write(line) != line.size()) {
        qCritical() << "Failed writing sensor reading to" << queuePath;
        return false;
    }
    return f.flush();
}

bool DatabaseWriter::appendAcknowledgement(const QByteArray &key)
{
    QFile f(acknowledgementPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning() << "Cannot persist upload acknowledgement to"
                   << acknowledgementPath;
        return false;
    }

    const QByteArray line = key.toBase64() + '\n';
    if (f.write(line) != line.size()) {
        qWarning() << "Failed writing upload acknowledgement to"
                   << acknowledgementPath;
        return false;
    }
    return f.flush();
}

bool DatabaseWriter::clearAcknowledgementFile()
{
    QSaveFile f(acknowledgementPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Cannot clear upload acknowledgement journal:"
                   << acknowledgementPath;
        return false;
    }
    if (!f.commit()) {
        qWarning() << "Could not atomically clear upload acknowledgement journal";
        return false;
    }
    return true;
}

bool DatabaseWriter::compactQueue()
{
    if (!rewriteQueueFile())
        return false;
    if (!clearAcknowledgementFile())
        return false;

    acknowledgementsSinceCompaction = 0;
    return true;
}

bool DatabaseWriter::rewriteQueueFile()
{
    QSaveFile f(queuePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Cannot rewrite persistent upload queue:" << queuePath;
        return false;
    }

    QList<QJsonObject> livePending;
    livePending.reserve(pendingKeys.size());
    for (const QJsonObject &json : pending) {
        if (!pendingKeys.contains(queueRecordKey(json)))
            continue;

        const QByteArray line =
            QJsonDocument(json).toJson(QJsonDocument::Compact) + '\n';
        if (f.write(line) != line.size()) {
            qWarning() << "Failed while rewriting persistent upload queue";
            f.cancelWriting();
            return false;
        }
        livePending.append(json);
    }

    if (!f.commit()) {
        qWarning() << "Could not atomically commit persistent upload queue";
        return false;
    }
    pending.swap(livePending);
    return true;
}

void DatabaseWriter::sendReading(const QString &sensorId, double value,
                                 const QString &unit, const QDateTime &timestamp)
{
    QJsonObject json;
    json["sensor_id"] = sensorId;
    json["value"] = QString::number(value, 'f', 2).toDouble();
    json["unit"] = unit;

    if (timestamp.isValid())
        json["timestamp"] = timestamp.toString(Qt::ISODate);
    else
        json["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    const QByteArray key = queueRecordKey(json);
    if (pendingKeys.contains(key)) {
        qInfo() << "Skipping duplicate queued reading for"
                << sensorId << json["timestamp"].toString();
        return;
    }

    if (!appendToQueueFile(json))
        return;

    pending.append(json);
    pendingKeys.insert(key);

    if (activeReplies.size() < MAX_CONCURRENT_UPLOADS &&
        !retryTimer.isActive())
        QTimer::singleShot(0, this, &DatabaseWriter::trySendNext);
}

void DatabaseWriter::sendWeatherData(const QString &sensorId, const QString &unit,
                                     const QVector<WeatherData> &weatherData)
{
    for (const auto &data : weatherData)
        sendReading(sensorId, data.value, unit, data.timestamp);
}

void DatabaseWriter::sendDepthReading(double depthCm)
{
    sendReading("depth_sensor", depthCm, "cm");
}

void DatabaseWriter::sendMoistureReading(double moist)
{
    sendReading("moisture_sensor", moist, "%");
}

void DatabaseWriter::sendValveState(bool open)
{
    sendReading("valve_state", open ? 1.0 : 0.0, "bool");
}

bool DatabaseWriter::isPriorityRecord(const QJsonObject &json)
{
    return json.value("sensor_id").toString().startsWith("health_");
}

int DatabaseWriter::nextPendingIndex() const
{
    // Health/liveness telemetry must not be blocked by a backlog of forecasts
    // or ordinary observations. Preserve FIFO ordering within the health class.
    for (int i = 0; i < pending.size(); ++i) {
        if (!isPriorityRecord(pending.at(i)))
            continue;
        const QByteArray key = queueRecordKey(pending.at(i));
        if (pendingKeys.contains(key) && !activeKeys.contains(key))
            return i;
    }

    // With no health telemetry waiting, retain normal FIFO behavior.
    for (int i = 0; i < pending.size(); ++i) {
        const QByteArray key = queueRecordKey(pending.at(i));
        if (pendingKeys.contains(key) && !activeKeys.contains(key))
            return i;
    }
    return -1;
}

void DatabaseWriter::trySendNext()
{
    if (pendingKeys.isEmpty() || retryTimer.isActive())
        return;

    if (!apiUrl.isValid() || apiUrl.isEmpty()) {
        qWarning() << "Invalid API URL; keeping" << pendingKeys.size()
                   << "reading(s) queued locally";
        lastFailureAt = QDateTime::currentDateTime();
        scheduleRetry();
        return;
    }

    while (activeReplies.size() < MAX_CONCURRENT_UPLOADS) {
        const int index = nextPendingIndex();
        if (index < 0 || index >= pending.size())
            break;

        const QJsonObject record = pending.at(index);
        const QByteArray data =
            QJsonDocument(record).toJson(QJsonDocument::Compact);
        const QByteArray key = queueRecordKey(record);

        QNetworkRequest request(apiUrl);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setTransferTimeout(15000);

        QNetworkReply *reply = manager->post(request, data);
        activeReplies.insert(reply, key);
        activeKeys.insert(key);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            onReplyFinished(reply);
        });
    }
}

void DatabaseWriter::scheduleRetry()
{
    if (pendingKeys.isEmpty())
        return;

    if (!retryTimer.isActive())
        retryTimer.start(retryDelayMs);

    retryDelayMs = qMin(retryDelayMs * 2, MAX_RETRY_MS);
}

void DatabaseWriter::onReplyFinished(QNetworkReply *reply)
{
    const QByteArray acknowledgedKey = activeReplies.take(reply);
    activeKeys.remove(acknowledgedKey);

    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool httpOk = status >= 200 && status < 300;
    const bool success =
        reply->error() == QNetworkReply::NoError && httpOk;

    if (success) {
        if (!acknowledgedKey.isEmpty() &&
            pendingKeys.contains(acknowledgedKey)) {
            // Journal the acknowledgement before changing memory. Until the
            // next batched compaction, startup recovery uses this exact key to
            // skip the already-delivered record still present in the JSONL.
            if (!appendAcknowledgement(acknowledgedKey)) {
                qWarning() << "Cloud write succeeded but its acknowledgement "
                              "could not be persisted; retaining the record";
                reply->deleteLater();
                scheduleRetry();
                return;
            }

            // Leave an in-memory tombstone in the ordered list. The key set is
            // the logical queue, and batched compaction removes tombstones.
            // This makes every successful acknowledgement O(1).
            pendingKeys.remove(acknowledgedKey);
            ++acknowledgementsSinceCompaction;
        } else {
            qWarning() << "Successful cloud write but in-flight record was not "
                          "found in the persistent queue; retaining remaining data";
        }
        if (acknowledgementsSinceCompaction >= COMPACTION_ACK_THRESHOLD &&
            !compactQueue()) {
            qWarning() << "Upload queue compaction deferred; acknowledgement "
                          "journal remains authoritative";
        }

        if (failCount > 0)
            qInfo() << "Cloud connection recovered; flushing"
                    << pendingKeys.size() << "queued reading(s)";

        failCount = 0;
        lastSuccessAt = QDateTime::currentDateTime();
        retryDelayMs = INITIAL_RETRY_MS;
        retryTimer.stop();

        reply->deleteLater();

        if (!pendingKeys.isEmpty())
            QTimer::singleShot(0, this, &DatabaseWriter::trySendNext);
        return;
    }

    // The failed record remains in pending. Clearing only the transient key
    // allows the priority selector to choose it again on the scheduled retry.
    ++failCount;
    lastFailureAt = QDateTime::currentDateTime();
    if (failCount <= 3) {
        qWarning() << "DB write failed; reading retained locally:"
                   << reply->errorString()
                   << "HTTP" << status
                   << "| queued:" << pendingKeys.size();
    }
    if (failCount == 3) {
        qWarning() << "Suppressing repeated DB errors until connectivity recovers;"
                   << "persistent queue remains active";
    }

    reply->deleteLater();
    scheduleRetry();
}
