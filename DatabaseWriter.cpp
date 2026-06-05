/////////////////////////////////////////////////////////////
// DATABASEWRITER.CPP - Database Writer Implementation
/////////////////////////////////////////////////////////////

#include "DatabaseWriter.h"

DatabaseWriter::DatabaseWriter(QObject *parent)
    : QObject(parent)
{
    manager = new QNetworkAccessManager(this);

    // API endpoint - UPDATE THIS TO YOUR EC2 IP
    apiUrl = QUrl("http://54.213.147.59:5000/sensor");
}

void DatabaseWriter::setApiUrl(const QString &url)
{
    apiUrl = QUrl(url);
}

// Send a single reading to the API
void DatabaseWriter::sendReading(const QString &sensorId, double value,
                                 const QString &unit, const QDateTime &timestamp)
{
    QJsonObject json;
    json["sensor_id"] = sensorId;
    json["value"] = QString::number(value, 'f', 2).toDouble();
    json["unit"] = unit;

    if (timestamp.isValid()) {
        json["timestamp"] = timestamp.toString(Qt::ISODate);
    } else {
        json["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    }

    postJson(json);
}

// Send an entire weather forecast array
void DatabaseWriter::sendWeatherData(const QString &sensorId, const QString &unit,
                                     const QVector<WeatherData> &weatherData)
{
    for (const auto &data : weatherData) {
        sendReading(sensorId, data.value, unit, data.timestamp);
    }

    //qDebug() << "Sent" << weatherData.size() << "readings for" << sensorId;
}

// Convenience: send a depth sensor reading with current timestamp
void DatabaseWriter::sendDepthReading(double depthCm)
{
    sendReading("depth_sensor", depthCm, "cm");
}

// Convenience: send a moisture sensor reading with current timestamp
void DatabaseWriter::sendMoistureReading(double moist)
{
    sendReading("moisture_sensor", moist, "%");
}

// Convenience: send valve state with current timestamp
void DatabaseWriter::sendValveState(bool open)
{
    sendReading("valve_state", open ? 1.0 : 0.0, "bool");
}

// Internal: POST a JSON object to the API
void DatabaseWriter::postJson(const QJsonObject &json)
{
    QJsonDocument doc(json);
    QByteArray data = doc.toJson();

    QNetworkRequest request(apiUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = manager->post(request, data);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReplyFinished(reply);
    });
}

// Handle API response
void DatabaseWriter::onReplyFinished(QNetworkReply *reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        failCount++;
        if (failCount <= 3) {
            qWarning() << "DB write failed:" << reply->errorString();
        }
        if (failCount == 3) {
            qWarning() << "Suppressing further DB error messages...";
        }
    }
    reply->deleteLater();
}
