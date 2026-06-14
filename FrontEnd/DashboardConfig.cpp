#include "DashboardConfig.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QDebug>

DashboardConfig::DashboardConfig()
{
    // No file loaded yet — built-in defaults (member initialisers)
    // are already in effect. load() may override them.
}

// --------------------------------------------------------------------
//  Colour parsing
// --------------------------------------------------------------------
//
// Accepts "#rrggbb", "#aarrggbb", or a named colour. Returns the
// fallback if the string is empty or unparseable.
QColor DashboardConfig::parseColor(const QString &s, const QColor &fallback)
{
    if (s.isEmpty())
        return fallback;
    QColor c(s);
    return c.isValid() ? c : fallback;
}

// --------------------------------------------------------------------
//  Defaults for an unconfigured sensor
// --------------------------------------------------------------------
//
// Used both for known legacy ids (so behaviour matches the old
// hardcoded tables) and for any sensor discovered at runtime that the
// config didn't mention.
SensorDef DashboardConfig::defaultDefFor(const QString &id) const
{
    SensorDef d;
    d.id = id;

    // Known ids carry the original SmartRainHarvest styling so an empty
    // config reproduces the previous look exactly.
    if (id == "precip_amount") {
        d.displayName = "Precipitation Amount";
        d.unit = "mm";
        d.lineColor = QColor("#42a5f5");
        d.areaColor = QColor(66, 165, 245, 45);
        d.floorAtZero = true;
    } else if (id == "precip_prob") {
        d.displayName = "Precipitation Probability";
        d.unit = "%";
        d.lineColor = QColor("#ab47bc");
        d.areaColor = QColor(171, 71, 188, 40);
        d.floorAtZero = true;
    } else if (id == "temperature") {
        d.displayName = "Temperature";
        d.unit = QString::fromUtf8("\xc2\xb0""C");   // °C
        d.lineColor = QColor("#ef5350");
        d.areaColor = QColor(239, 83, 80, 40);
        d.floorAtZero = false;
    } else if (id == "water_depth" || id == "depth_sensor" ||
               id == "hcsr04_depth" || id == "maxbotix_depth" ||
               id == "MB7389_100_depth") {
        d.displayName = "Water Depth";
        d.unit = "cm";
        d.lineColor = QColor("#26c6da");
        d.areaColor = QColor(38, 198, 218, 40);
        d.floorAtZero = false;
    } else if (id == "valve_state") {
        d.displayName = "Valve State";
        d.unit = "0 / 1";
        d.lineColor = QColor("#66bb6a");
        d.areaColor = QColor(102, 187, 106, 40);
        d.floorAtZero = true;
    } else if (id == "moisture_sensor") {
        d.displayName = "Soil Moisture";
        d.unit = "%";
        d.lineColor = QColor("#8d6e63");
        d.areaColor = QColor(141, 110, 99, 45);
        d.floorAtZero = true;
    } else {
        // Unknown sensor: derive a friendly name from the id and use a
        // neutral grey palette.
        QString name = id;
        name.replace('_', ' ');
        if (!name.isEmpty())
            name[0] = name[0].toUpper();
        d.displayName = name;
        d.unit = "";
        d.lineColor = QColor("#78909c");
        d.areaColor = QColor(120, 144, 156, 30);
        d.floorAtZero = false;
    }

    d.visible = true;
    return d;
}

// --------------------------------------------------------------------
//  Load
// --------------------------------------------------------------------
bool DashboardConfig::load(const QString &path)
{
    m_loaded = false;
    m_error.clear();

    QFile f(path);
    if (!f.exists()) {
        m_error = QString("Config file not found: %1").arg(QFileInfo(path).absoluteFilePath());
        return false;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        m_error = QString("Cannot open config file: %1").arg(path);
        return false;
    }

    const QByteArray bytes = f.readAll();
    f.close();
    return loadFromData(bytes);
}

// Parse config from raw bytes (used by the WebAssembly HTTP-fetch path,
// where there is no local file to read). Shares all parsing logic with
// the file-based load() above.
bool DashboardConfig::loadFromData(const QByteArray &bytes)
{
    m_loaded = false;
    m_error.clear();

    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(bytes, &perr);

    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        m_error = QString("Invalid JSON: %1").arg(perr.errorString());
        return false;
    }

    QJsonObject root = doc.object();

    // ── Global settings ─────────────────────────────────────────
    if (root.contains("api_url"))
        m_apiUrl = root.value("api_url").toString(m_apiUrl);
    if (root.contains("refresh_interval_sec"))
        m_refreshSec = root.value("refresh_interval_sec").toInt(m_refreshSec);
    if (root.contains("auto_refresh"))
        m_autoRefreshDefault = root.value("auto_refresh").toBool(m_autoRefreshDefault);
    if (root.contains("scrollable_charts"))
        m_scrollable = root.value("scrollable_charts").toBool(m_scrollable);
    if (root.contains("window_title"))
        m_windowTitle = root.value("window_title").toString(m_windowTitle);

    if (root.contains("default_range")) {
        QJsonObject r = root.value("default_range").toObject();
        m_rangeBack  = r.value("days_back").toInt(m_rangeBack);
        m_rangeAhead = r.value("days_ahead").toInt(m_rangeAhead);
    }

    // ── Sensor list ─────────────────────────────────────────────
    // Two accepted shapes:
    //   "sensors": ["temperature", "moisture_sensor"]          (ids only)
    //   "sensors": [ {"id":"temperature","name":"Temp",...}, ] (full defs)
    m_defs.clear();
    m_order.clear();
    m_hasExplicitList = false;

    if (root.contains("sensors") && root.value("sensors").isArray()) {
        QJsonArray arr = root.value("sensors").toArray();
        m_hasExplicitList = !arr.isEmpty();

        for (const QJsonValue &v : arr) {
            SensorDef d;

            if (v.isString()) {
                // id-only entry: start from defaults for that id.
                d = defaultDefFor(v.toString());
            } else if (v.isObject()) {
                QJsonObject o = v.toObject();
                QString id = o.value("id").toString();
                if (id.isEmpty())
                    continue;                 // skip malformed entry

                d = defaultDefFor(id);        // seed with defaults, then override
                if (o.contains("name"))
                    d.displayName = o.value("name").toString(d.displayName);
                if (o.contains("unit"))
                    d.unit = o.value("unit").toString(d.unit);
                if (o.contains("color"))
                    d.lineColor = parseColor(o.value("color").toString(), d.lineColor);
                if (o.contains("area_color"))
                    d.areaColor = parseColor(o.value("area_color").toString(), d.areaColor);
                if (o.contains("floor_at_zero"))
                    d.floorAtZero = o.value("floor_at_zero").toBool(d.floorAtZero);
                if (o.contains("visible"))
                    d.visible = o.value("visible").toBool(d.visible);
            } else {
                continue;
            }

            if (!m_defs.contains(d.id))
                m_order.append(d.id);
            m_defs.insert(d.id, d);
        }
    }

    m_loaded = true;
    return true;
}

// --------------------------------------------------------------------
//  Accessors
// --------------------------------------------------------------------
QStringList DashboardConfig::visibleSensorIds() const
{
    QStringList out;
    for (const QString &id : m_order) {
        const SensorDef &d = m_defs.value(id);
        if (d.visible)
            out.append(id);
    }
    return out;
}

SensorDef DashboardConfig::sensorDef(const QString &id) const
{
    if (m_defs.contains(id))
        return m_defs.value(id);
    return defaultDefFor(id);
}

QString DashboardConfig::displayName(const QString &id) const { return sensorDef(id).displayName; }
QString DashboardConfig::unit(const QString &id) const        { return sensorDef(id).unit; }
QColor  DashboardConfig::lineColor(const QString &id) const   { return sensorDef(id).lineColor; }
QColor  DashboardConfig::areaColor(const QString &id) const   { return sensorDef(id).areaColor; }
bool    DashboardConfig::floorAtZero(const QString &id) const { return sensorDef(id).floorAtZero; }
