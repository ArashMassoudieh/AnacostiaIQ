/////////////////////////////////////////////////////////////
// CONFIG.H - Loads config.json: app settings + sensor factory
/////////////////////////////////////////////////////////////
#ifndef CONFIG_H
#define CONFIG_H

#include <QString>
#include <QVector>
#include <QJsonDocument>
#include <QJsonObject>
#include "Sensor.h"

class Config {
public:
    Config() = default;
    bool load(const QString &path);
    QString errorString() const { return m_error; }

    int pollIntervalSeconds() const { return m_pollInterval; }
    double barrelDepthCm() const { return m_barrelDepth; }
    QString apiUrl() const { return m_apiUrl; }

    // Stable station identity used to namespace remote health telemetry.
    // Read from the retained raw JSON so older Config.cpp implementations
    // remain source-compatible; absent values fall back conservatively.
    QString stationId() const {
        const QJsonObject s = QJsonDocument::fromJson(m_raw).object().value("station").toObject();
        return s.value("id").toString("station_01");
    }
    QString stationName() const {
        const QJsonObject s = QJsonDocument::fromJson(m_raw).object().value("station").toObject();
        return s.value("name").toString("AnacostiaIQ Station");
    }

    bool adaptiveEnabled() const { return m_adaptiveEnabled; }
    int idleIntervalFactor() const { return m_idleFactor; }
    double rainProbabilityThreshold() const { return m_rainThreshold; }
    int rainLookaheadHours() const { return m_lookaheadHours; }

    int weatherIntervalSeconds() const { return m_weatherInterval; }
    QString weatherSource() const { return m_weatherSource; }
    double latitude() const { return m_lat; }
    double longitude() const { return m_lon; }
    QString noaaOffice() const { return m_office; }
    int noaaGridX() const { return m_gridX; }
    int noaaGridY() const { return m_gridY; }

    QVector<Sensor*> createSensors(QObject *parent = nullptr) const;

private:
    int m_pollInterval = 3600;
    double m_barrelDepth = 137.16;
    QString m_apiUrl = "http://54.213.147.59:5000/sensor";

    bool m_adaptiveEnabled = true;
    int m_idleFactor = 10;
    double m_rainThreshold = 0.0;
    int m_lookaheadHours = 24;

    int m_weatherInterval = 3600;
    QString m_weatherSource = "openmeteo";
    double m_lat = 38.98;
    double m_lon = -77.10;
    QString m_office = "LWX";
    int m_gridX = 97;
    int m_gridY = 71;

    QByteArray m_raw;
    QString m_error;
    bool m_loaded = false;
};

#endif // CONFIG_H
