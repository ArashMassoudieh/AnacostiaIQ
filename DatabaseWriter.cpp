/////////////////////////////////////////////////////////////
// DATABASEWRITER.CPP - Database Writer Implementation
/////////////////////////////////////////////////////////////

#include "DatabaseWriter.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVariant>

DatabaseWriter::DatabaseWriter(QObject *parent)
    : QObject(parent)
{
    manager = new QNetworkAccessManager(this);
    apiUrl = QUrl("http://54.213.147.59:5000/sensor");

    queuePath = resolveQueuePath();
    loadQueue();

    retryTimer.setSingleShot(true);
    connect(&retryTimer, &QTimer::timeout,
            this, &DatabaseWriter::trySendNext);

    if (!pending.isEmpty()) {
        qInfo() << "Loaded" << pending.size()
                << "pending cloud write(s) from" << queuePath;
        QTimer::singleShot(0, this, &DatabaseWriter::trySendNext);
    }
}

void DatabaseWriter::setApiUrl(const QString &url)
{
    apiUrl = QUrl(url);
    if (!pending.isEmpty() && !inFlight && !retryTimer.isActive())
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

    QFile f(queuePath);
    if (!f.exists())
        return;
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Cannot open persistent upload queue:" << queuePath;
        return;
    }

    int badLines = 0;
    while (!f.atEnd()) {
        const QByteArray line = f.readLine().trimmed();
        if (line.isEmpty())
            continue;

        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            ++badLines;
            continue;
        }
        pending.append(doc.object());
    }

    if (badLines > 0)
        qWarning() << "Ignored" << badLines
                   << "corrupt upload-queue record(s)";
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

bool DatabaseWriter::rewriteQueueFile()
{
    QSaveFile f(queuePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Cannot rewrite persistent upload queue:" << queuePath;
        return false;
    }

    for (const QJsonObject &json : pending) {
        const QByteArray line =
            QJsonDocument(json).toJson(QJsonDocument::Compact) + '\n';
        if (f.write(line) != line.size()) {
            qWarning() << "Failed while rewriting persistent upload queue";
            f.cancelWriting();
            return false;
        }
    }

    if (!f.commit()) {
        qWarning() << "Could not atomically commit persistent upload queue";
        return false;
    }
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

    if (!appendToQueueFile(json))
        return;

    pending.append(json);

    if (!inFlight && !retryTimer.isActive())
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

void DatabaseWriter::trySendNext()
{
    if (inFlight || pending.isEmpty())
        return;

    if (!apiUrl.isValid() || apiUrl.isEmpty()) {
        qWarning() << "Invalid API URL; keeping" << pending.size()
                   << "reading(s) queued locally";
        lastFailureAt = QDateTime::currentDateTime();
        scheduleRetry();
        return;
    }

    const QByteArray data =
        QJsonDocument(pending.first()).toJson(QJsonDocument::Compact);

    QNetworkRequest request(apiUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    inFlight = true;
    QNetworkReply *reply = manager->post(request, data);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReplyFinished(reply);
    });
}

void DatabaseWriter::scheduleRetry()
{
    if (pending.isEmpty())
        return;

    if (!retryTimer.isActive())
        retryTimer.start(retryDelayMs);

    retryDelayMs = qMin(retryDelayMs * 2, MAX_RETRY_MS);
}

void DatabaseWriter::onReplyFinished(QNetworkReply *reply)
{
    inFlight = false;

    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool httpOk = status >= 200 && status < 300;
    const bool success =
        reply->error() == QNetworkReply::NoError && httpOk;

    if (success) {
        if (!pending.isEmpty())
            pending.removeFirst();

        rewriteQueueFile();

        if (failCount > 0)
            qInfo() << "Cloud connection recovered; flushing"
                    << pending.size() << "queued reading(s)";

        failCount = 0;
        lastSuccessAt = QDateTime::currentDateTime();
        retryDelayMs = INITIAL_RETRY_MS;
        retryTimer.stop();

        reply->deleteLater();

        if (!pending.isEmpty())
            QTimer::singleShot(0, this, &DatabaseWriter::trySendNext);
        return;
    }

    ++failCount;
    lastFailureAt = QDateTime::currentDateTime();
    if (failCount <= 3) {
        qWarning() << "DB write failed; reading retained locally:"
                   << reply->errorString()
                   << "HTTP" << status
                   << "| queued:" << pending.size();
    }
    if (failCount == 3) {
        qWarning() << "Suppressing repeated DB errors until connectivity recovers;"
                   << "persistent queue remains active";
    }

    reply->deleteLater();
    scheduleRetry();
}
