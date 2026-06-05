/////////////////////////////////////////////////////////////
// WEATHERFETCHER.CPP - Weather API client (NOAA or Open-Meteo)
/////////////////////////////////////////////////////////////

#include "WeatherFetcher.h"
#include <QEventLoop>
#include <QUrl>
#include <QDebug>

WeatherFetcher::WeatherFetcher(QObject *parent) : QObject(parent) {
    manager = new QNetworkAccessManager(this);
}

void WeatherFetcher::setSourceFromString(const QString &s) {
    const QString v = s.trimmed().toLower();
    if (v == "noaa")
        m_source = WeatherSource::NOAA;
    else
        m_source = WeatherSource::OpenMeteo;   // default / "openmeteo"
}

// Kept blocking to match the original design (called from the
// monitoring tick). Returns empty QByteArray on error.
QByteArray WeatherFetcher::httpGetBlocking(const QString &url) {
    QNetworkRequest request((QUrl(url)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    // NOAA requires a User-Agent; harmless for Open-Meteo.
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      "AnacostiaIQ/1.0 (digital-twin)");

    QNetworkReply *reply = manager->get(request);

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    QByteArray data;
    if (reply->error() == QNetworkReply::NoError) {
        data = reply->readAll();
        m_lastError.clear();
    } else {
        m_lastError = reply->errorString();
        qWarning() << "WeatherFetcher: request failed:" << m_lastError;
    }
    reply->deleteLater();
    return data;
}

QVector<WeatherData> WeatherFetcher::getWeatherPrediction(datatype type) {
    if (m_source == WeatherSource::NOAA)
        return fetchNOAA(type);
    return fetchOpenMeteo(type);
}

// ── NOAA backend ───────────────────────────────────────────
QVector<WeatherData> WeatherFetcher::fetchNOAA(datatype type) {
    QVector<WeatherData> out;

    QString url = QString("https://api.weather.gov/gridpoints/%1/%2,%3")
                      .arg(m_office).arg(m_gridX).arg(m_gridY);

    QString field;
    switch (type) {
    case datatype::PrecipitationAmount:        field = "quantitativePrecipitation"; break;
    case datatype::ProbabilityofPrecipitation: field = "probabilityOfPrecipitation"; break;
    case datatype::RelativeHumidity:           field = "relativeHumidity"; break;
    case datatype::Temperature:                field = "temperature"; break;
    }

    QByteArray response = httpGetBlocking(url);
    if (response.isEmpty()) return out;

    QJsonObject props = QJsonDocument::fromJson(response)
                            .object()["properties"].toObject();
    QJsonArray values = props[field].toObject()["values"].toArray();

    for (const auto &v : values) {
        QJsonObject obj = v.toObject();

        // validTime: "2026-06-05T12:00:00+00:00/PT6H" -> drop "/PT6H"
        QString iso = obj["validTime"].toString().split('/').first();
        QDateTime t = QDateTime::fromString(iso, Qt::ISODate);
        if (t.isValid())
            t = t.toLocalTime();
        else
            t = QDateTime::fromString(iso.split('+').first(),
                                      "yyyy-MM-ddTHH:mm:ss");
        if (!t.isValid())
            continue;

        QJsonValue val = obj["value"];
        double value = val.isNull() ? 0.0 : val.toDouble();
        out.push_back({ t, value });
    }
    return out;
}

// ── Open-Meteo backend ─────────────────────────────────────
QVector<WeatherData> WeatherFetcher::fetchOpenMeteo(datatype type) {
    QVector<WeatherData> out;

    // Map our enum to the Open-Meteo hourly variable name.
    QString hourlyVar;
    switch (type) {
    case datatype::PrecipitationAmount:        hourlyVar = "precipitation"; break;
    case datatype::ProbabilityofPrecipitation: hourlyVar = "precipitation_probability"; break;
    case datatype::RelativeHumidity:           hourlyVar = "relative_humidity_2m"; break;
    case datatype::Temperature:                hourlyVar = "temperature_2m"; break;
    }

    // timezone=auto -> API returns local-time timestamps for the location.
    QString url = QString("https://api.open-meteo.com/v1/forecast"
                          "?latitude=%1&longitude=%2&hourly=%3&timezone=auto")
                      .arg(m_lat, 0, 'f', 4)
                      .arg(m_lon, 0, 'f', 4)
                      .arg(hourlyVar);

    QByteArray response = httpGetBlocking(url);
    if (response.isEmpty()) return out;

    QJsonObject root = QJsonDocument::fromJson(response).object();

    // Open-Meteo signals bad params with {"error":true,"reason":...}
    if (root["error"].toBool()) {
        m_lastError = root["reason"].toString();
        qWarning() << "Open-Meteo error:" << m_lastError;
        return out;
    }

    QJsonObject hourly = root["hourly"].toObject();
    QJsonArray times = hourly["time"].toArray();
    QJsonArray vals  = hourly[hourlyVar].toArray();

    // The two arrays run in parallel; zip them into WeatherData points.
    const int n = qMin(times.size(), vals.size());
    for (int i = 0; i < n; ++i) {
        // Open-Meteo time looks like "2026-06-05T13:00" (no seconds/offset)
        QDateTime t = QDateTime::fromString(times[i].toString(), Qt::ISODate);
        if (!t.isValid())
            continue;

        QJsonValue v = vals[i];
        double value = v.isNull() ? 0.0 : v.toDouble();
        out.push_back({ t, value });
    }
    return out;
}

// ── Cumulative helper (unchanged behaviour) ────────────────
double calculateCumulativeValue(const QVector<WeatherData> &weatherData, int days) {
    if (weatherData.isEmpty()) return 0.0;

    QDateTime startTime = weatherData.first().timestamp;
    QDateTime endTime = startTime.addDays(days);

    double cumulative = 0.0;
    for (const auto &data : weatherData) {
        if (data.timestamp <= endTime)
            cumulative += data.value;
        else
            break;
    }
    return cumulative;
}
