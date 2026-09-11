/////////////////////////////////////////////////////////////
// DATABASEWRITER.H - Database Writer Class Header
/////////////////////////////////////////////////////////////

#ifndef DATABASEWRITER_H
#define DATABASEWRITER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonObject>
#include <QJsonDocument>
#include <QUrl>
#include <QDebug>
#include <QDateTime>
#include <QVector>
#include <QList>
#include <QTimer>
#include <QString>

#include "WeatherFetcher.h"

class DatabaseWriter : public QObject
{
    Q_OBJECT

public:
    explicit DatabaseWriter(QObject *parent = nullptr);

    void setApiUrl(const QString &url);

    void sendReading(const QString &sensorId, double value,
                     const QString &unit, const QDateTime &timestamp = QDateTime());

    void sendWeatherData(const QString &sensorId, const QString &unit,
                         const QVector<WeatherData> &weatherData);

    void sendDepthReading(double depthCm);
    void sendMoistureReading(double moist);
    void sendValveState(bool open);

    int pendingCount() const { return pending.size(); }
    QString queueFilePath() const { return queuePath; }

    // Read-only delivery state used by HealthMonitor. A failed cloud write
    // never drops data; the persistent queue remains the source of truth.
    int consecutiveFailures() const { return failCount; }
    QDateTime lastSuccessfulUpload() const { return lastSuccessAt; }
    QDateTime lastFailedUpload() const { return lastFailureAt; }

private slots:
    void onReplyFinished(QNetworkReply *reply);
    void trySendNext();

private:
    QNetworkAccessManager *manager;
    QUrl apiUrl;

    QList<QJsonObject> pending;
    QString queuePath;
    QTimer retryTimer;
    bool inFlight = false;
    int failCount = 0;
    int retryDelayMs = 5000;
    QDateTime lastSuccessAt;
    QDateTime lastFailureAt;

    static constexpr int INITIAL_RETRY_MS = 5000;
    static constexpr int MAX_RETRY_MS = 5 * 60 * 1000;

    QString resolveQueuePath() const;
    void loadQueue();
    bool appendToQueueFile(const QJsonObject &json);
    bool rewriteQueueFile();
    void scheduleRetry();
};

#endif // DATABASEWRITER_H
