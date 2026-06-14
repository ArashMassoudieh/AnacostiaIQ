#ifndef DASHBOARDCONFIG_H
#define DASHBOARDCONFIG_H

#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QColor>
#include <QMap>
#include <QVector>

// ====================================================================
//  DashboardConfig
//
//  Loads config.json and becomes the single source of truth for:
//    - which sensors the dashboard shows (and in what order)
//    - per-sensor display: friendly name, unit, line/area colour,
//      whether the Y axis floors at zero
//    - global settings: API URL, auto-refresh interval, default
//      query window, layout mode
//
//  Mirrors the config-as-factory pattern from the AnacostiaIQ sensor
//  app: to add/remove a chart or restyle one, edit config.json — no
//  recompile. Anything missing from the file falls back to a built-in
//  default, so a partial (or absent) config still produces a working
//  dashboard.
// ====================================================================

// Per-sensor display definition, resolved from config (or defaults).
struct SensorDef {
    QString id;                 // DynamoDB sensor_id (the key)
    QString displayName;        // human label on the chart
    QString unit;               // unit string shown in the title
    QColor  lineColor;          // series pen colour
    QColor  areaColor;          // translucent fill under the curve
    bool    floorAtZero = false;// clamp Y-axis minimum to 0
    bool    visible     = true; // show this sensor at all
};

class DashboardConfig
{
public:
    DashboardConfig();

    // Load from a JSON file. Returns false (and sets errorString) on a
    // missing/invalid file; built-in defaults remain in effect either
    // way, so callers can run on regardless.
    bool load(const QString &path);

    // Parse config from raw bytes (WebAssembly HTTP-fetch path, where
    // there is no local file). Same semantics/return as load().
    bool loadFromData(const QByteArray &bytes);

    QString errorString() const { return m_error; }
    bool    loadedFromFile() const { return m_loaded; }

    // ── Global settings ─────────────────────────────────────────
    QString apiUrl() const            { return m_apiUrl; }
    int     refreshIntervalSec() const{ return m_refreshSec; }
    int     defaultRangeDaysBack() const  { return m_rangeBack; }
    int     defaultRangeDaysAhead() const { return m_rangeAhead; }
    bool    autoRefreshDefault() const{ return m_autoRefreshDefault; }
    bool    scrollableCharts() const  { return m_scrollable; }
    QString windowTitle() const       { return m_windowTitle; }

    // ── Sensor selection ────────────────────────────────────────
    // The ordered list of sensor ids the dashboard should display.
    // This is what the UI loops over (replacing the old hardcoded
    // sensorIds list and the API-driven discovery as the default).
    QStringList visibleSensorIds() const;

    // True if config.json explicitly listed sensors. When false, the
    // dashboard may fall back to discovering them from GET /sensors.
    bool hasExplicitSensorList() const { return m_hasExplicitList; }

    // Per-sensor display lookups. All fall back to sensible defaults
    // (derived from the id) for sensors not named in the config, so
    // a discovered-but-unconfigured sensor still renders.
    SensorDef sensorDef(const QString &id) const;
    QString displayName(const QString &id) const;
    QString unit(const QString &id) const;
    QColor  lineColor(const QString &id) const;
    QColor  areaColor(const QString &id) const;
    bool    floorAtZero(const QString &id) const;

private:
    SensorDef defaultDefFor(const QString &id) const;
    static QColor parseColor(const QString &s, const QColor &fallback);

    // Global settings (with built-in defaults)
    QString m_apiUrl       = "http://54.213.147.59:5000";
    int     m_refreshSec   = 60;
    int     m_rangeBack    = 7;
    int     m_rangeAhead   = 7;
    bool    m_autoRefreshDefault = false;
    bool    m_scrollable   = false;
    QString m_windowTitle  = "Sensor Dashboard";

    // Sensor table, keyed by id, plus insertion order for display.
    QMap<QString, SensorDef> m_defs;
    QStringList              m_order;
    bool m_hasExplicitList = false;

    bool    m_loaded = false;
    QString m_error;
};

#endif // DASHBOARDCONFIG_H
