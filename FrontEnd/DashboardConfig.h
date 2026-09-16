#ifndef DASHBOARDCONFIG_H
#define DASHBOARDCONFIG_H

#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QColor>
#include <QMap>
#include <QVector>

struct SensorDef {
    QString id;
    QString displayName;
    QString unit;
    QColor  lineColor;
    QColor  areaColor;
    bool    floorAtZero = false;
    bool    visible = true;

    // Physical series are fetched from the API. Derived series are evaluated
    // locally from retained source samples after all physical requests finish.
    QString type = "sensor";
    QMap<QString, QString> inputs;   // expression variable -> source series id
    QMap<QString, double> params;    // named constants available to expressions
    QMap<QString, QString> lets;     // ordered-by-key intermediate expressions
    QString expression;
    QString validWhen;

    bool isDerived() const { return type.compare("derived", Qt::CaseInsensitive) == 0; }
};

class DashboardConfig
{
public:
    DashboardConfig();

    bool load(const QString &path);
    bool loadFromData(const QByteArray &bytes);

    QString errorString() const { return m_error; }
    bool loadedFromFile() const { return m_loaded; }

    QString apiUrl() const             { return m_apiUrl; }
    int refreshIntervalSec() const     { return m_refreshSec; }
    int defaultRangeDaysBack() const   { return m_rangeBack; }
    int defaultRangeDaysAhead() const  { return m_rangeAhead; }
    bool autoRefreshDefault() const    { return m_autoRefreshDefault; }
    bool scrollableCharts() const      { return m_scrollable; }
    QString windowTitle() const        { return m_windowTitle; }

    // Ordered visible chart series (physical + derived).
    QStringList visibleSensorIds() const;
    // Only physical/backend ids. Derived ids must never be sent to /sensor/.
    QStringList fetchableSensorIds() const;
    QStringList derivedSensorIds() const;

    bool hasExplicitSensorList() const { return m_hasExplicitList; }

    SensorDef sensorDef(const QString &id) const;
    QString displayName(const QString &id) const;
    QString unit(const QString &id) const;
    QColor lineColor(const QString &id) const;
    QColor areaColor(const QString &id) const;
    bool floorAtZero(const QString &id) const;
    bool isDerived(const QString &id) const { return sensorDef(id).isDerived(); }

private:
    SensorDef defaultDefFor(const QString &id) const;
    static QColor parseColor(const QString &s, const QColor &fallback);

    QString m_apiUrl = "http://54.213.147.59:5000";
    int m_refreshSec = 60;
    int m_rangeBack = 7;
    int m_rangeAhead = 7;
    bool m_autoRefreshDefault = false;
    bool m_scrollable = false;
    QString m_windowTitle = "Sensor Dashboard";

    QMap<QString, SensorDef> m_defs;
    QStringList m_order;
    bool m_hasExplicitList = false;

    bool m_loaded = false;
    QString m_error;
};

#endif // DASHBOARDCONFIG_H
