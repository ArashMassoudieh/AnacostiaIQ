/////////////////////////////////////////////////////////////
// CONFIG.H - Loads config.json: app settings + sensor factory
//
//  Reads an external JSON file so sensors and app settings can be
//  changed without recompiling. load() must be called (and checked)
//  before the getters or createSensors() are used.
//
//  createSensors() is a factory: it reads the "sensors" array and
//  constructs the matching Sensor subclass for each entry, so the
//  JSON fully drives what the app instantiates.
/////////////////////////////////////////////////////////////

#ifndef CONFIG_H
#define CONFIG_H

#include <QString>
#include <QVector>
#include "Sensor.h"

class Config {
public:
    Config() = default;

    // Load and parse the JSON file. Returns false on missing file or
    // parse error; errorString() then explains why.
    bool load(const QString &path);
    QString errorString() const { return m_error; }

    // ── App-level settings (with sensible defaults) ────────
    int     pollIntervalSeconds() const { return m_pollInterval; }
    double  barrelDepthCm() const       { return m_barrelDepth; }
    QString apiUrl() const              { return m_apiUrl; }

    // ── Weather settings ───────────────────────────────────
    QString weatherSource() const       { return m_weatherSource; }
    double  latitude() const            { return m_lat; }
    double  longitude() const           { return m_lon; }
    QString noaaOffice() const          { return m_office; }
    int     noaaGridX() const           { return m_gridX; }
    int     noaaGridY() const           { return m_gridY; }

    // ── Sensor factory ─────────────────────────────────────
    // Builds one Sensor* per entry in the JSON "sensors" array.
    // Unknown "type" values are skipped with a warning. Ownership
    // of the returned pointers passes to the caller.
    QVector<Sensor*> createSensors(QObject *parent = nullptr) const;

private:
    // Parsed app settings (defaults match the original hardcoded values)
    int     m_pollInterval = 3600;
    double  m_barrelDepth  = 137.16;
    QString m_apiUrl       = "http://54.213.147.59:5000/sensor";

    // Weather
    QString m_weatherSource = "openmeteo";
    double  m_lat           = 38.98;
    double  m_lon           = -77.10;
    QString m_office        = "LWX";
    int     m_gridX         = 97;
    int     m_gridY         = 71;

    // Raw JSON kept so createSensors() can read the array on demand
    QByteArray m_raw;
    QString    m_error;
    bool       m_loaded = false;
};

#endif // CONFIG_H
