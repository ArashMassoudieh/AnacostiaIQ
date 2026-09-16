#include "DashboardConfig.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>

DashboardConfig::DashboardConfig() {}

QColor DashboardConfig::parseColor(const QString &s, const QColor &fallback)
{
    if (s.isEmpty()) return fallback;
    QColor c(s);
    return c.isValid() ? c : fallback;
}

SensorDef DashboardConfig::defaultDefFor(const QString &id) const
{
    SensorDef d;
    d.id = id;
    if (id == "precip_amount") {
        d.displayName="Precipitation Amount"; d.unit="mm"; d.lineColor=QColor("#42a5f5"); d.areaColor=QColor(66,165,245,45); d.floorAtZero=true;
    } else if (id == "precip_prob") {
        d.displayName="Precipitation Probability"; d.unit="%"; d.lineColor=QColor("#ab47bc"); d.areaColor=QColor(171,71,188,40); d.floorAtZero=true;
    } else if (id == "temperature") {
        d.displayName="Temperature"; d.unit=QString::fromUtf8("\xc2\xb0""C"); d.lineColor=QColor("#ef5350"); d.areaColor=QColor(239,83,80,40);
    } else if (id == "water_depth" || id == "depth_sensor" || id == "hcsr04_depth" || id == "maxbotix_depth" || id == "MB7389_100_depth") {
        d.displayName="Water Depth"; d.unit="cm"; d.lineColor=QColor("#26c6da"); d.areaColor=QColor(38,198,218,40);
    } else if (id == "valve_state") {
        d.displayName="Valve State"; d.unit="0 / 1"; d.lineColor=QColor("#66bb6a"); d.areaColor=QColor(102,187,106,40); d.floorAtZero=true;
    } else if (id == "moisture_sensor") {
        d.displayName="Soil Moisture"; d.unit="%"; d.lineColor=QColor("#8d6e63"); d.areaColor=QColor(141,110,99,45); d.floorAtZero=true;
    } else {
        QString name=id; name.replace('_',' '); if(!name.isEmpty()) name[0]=name[0].toUpper();
        d.displayName=name; d.lineColor=QColor("#78909c"); d.areaColor=QColor(120,144,156,30);
    }
    return d;
}

bool DashboardConfig::load(const QString &path)
{
    m_loaded=false; m_error.clear();
    QFile f(path);
    if (!f.exists()) { m_error=QString("Config file not found: %1").arg(QFileInfo(path).absoluteFilePath()); return false; }
    if (!f.open(QIODevice::ReadOnly)) { m_error=QString("Cannot open config file: %1").arg(path); return false; }
    return loadFromData(f.readAll());
}

bool DashboardConfig::loadFromData(const QByteArray &bytes)
{
    m_loaded=false; m_error.clear();
    QJsonParseError perr;
    QJsonDocument doc=QJsonDocument::fromJson(bytes,&perr);
    if (perr.error!=QJsonParseError::NoError || !doc.isObject()) { m_error=QString("Invalid JSON: %1").arg(perr.errorString()); return false; }
    const QJsonObject root=doc.object();

    if(root.contains("api_url")) m_apiUrl=root.value("api_url").toString(m_apiUrl);
    if(root.contains("refresh_interval_sec")) m_refreshSec=root.value("refresh_interval_sec").toInt(m_refreshSec);
    if(root.contains("auto_refresh")) m_autoRefreshDefault=root.value("auto_refresh").toBool(m_autoRefreshDefault);
    if(root.contains("scrollable_charts")) m_scrollable=root.value("scrollable_charts").toBool(m_scrollable);
    if(root.contains("window_title")) m_windowTitle=root.value("window_title").toString(m_windowTitle);
    if(root.contains("default_range")) { const QJsonObject r=root.value("default_range").toObject(); m_rangeBack=r.value("days_back").toInt(m_rangeBack); m_rangeAhead=r.value("days_ahead").toInt(m_rangeAhead); }

    m_defs.clear(); m_order.clear(); m_hasExplicitList=false;
    if(root.contains("sensors") && root.value("sensors").isArray()) {
        const QJsonArray arr=root.value("sensors").toArray();
        m_hasExplicitList=!arr.isEmpty();
        for(const QJsonValue &v:arr) {
            SensorDef d;
            if(v.isString()) d=defaultDefFor(v.toString());
            else if(v.isObject()) {
                const QJsonObject o=v.toObject();
                const QString id=o.value("id").toString();
                if(id.isEmpty()) continue;
                d=defaultDefFor(id);
                d.type=o.value("type").toString(d.type);
                if(o.contains("name")) d.displayName=o.value("name").toString(d.displayName);
                if(o.contains("unit")) d.unit=o.value("unit").toString(d.unit);
                if(o.contains("color")) d.lineColor=parseColor(o.value("color").toString(),d.lineColor);
                if(o.contains("area_color")) d.areaColor=parseColor(o.value("area_color").toString(),d.areaColor);
                if(o.contains("floor_at_zero")) d.floorAtZero=o.value("floor_at_zero").toBool(d.floorAtZero);
                if(o.contains("visible")) d.visible=o.value("visible").toBool(d.visible);

                if(d.isDerived()) {
                    const QJsonObject inputs=o.value("inputs").toObject();
                    for(auto it=inputs.begin();it!=inputs.end();++it) if(it.value().isString()) d.inputs.insert(it.key(),it.value().toString());
                    const QJsonObject params=o.value("params").toObject();
                    for(auto it=params.begin();it!=params.end();++it) if(it.value().isDouble()) d.params.insert(it.key(),it.value().toDouble());
                    const QJsonObject lets=o.value("let").toObject();
                    for(auto it=lets.begin();it!=lets.end();++it) if(it.value().isString()) d.lets.insert(it.key(),it.value().toString());
                    d.expression=o.value("expression").toString();
                    d.validWhen=o.value("valid_when").toString();
                }
            } else continue;
            if(!m_defs.contains(d.id)) m_order.append(d.id);
            m_defs.insert(d.id,d);
        }
    }
    m_loaded=true;
    return true;
}

QStringList DashboardConfig::visibleSensorIds() const
{
    QStringList out; for(const QString &id:m_order) if(m_defs.value(id).visible) out.append(id); return out;
}
QStringList DashboardConfig::fetchableSensorIds() const
{
    QStringList out; for(const QString &id:m_order) { const SensorDef d=m_defs.value(id); if(d.visible && !d.isDerived()) out.append(id); } return out;
}
QStringList DashboardConfig::derivedSensorIds() const
{
    QStringList out; for(const QString &id:m_order) { const SensorDef d=m_defs.value(id); if(d.visible && d.isDerived()) out.append(id); } return out;
}
SensorDef DashboardConfig::sensorDef(const QString &id) const { return m_defs.contains(id)?m_defs.value(id):defaultDefFor(id); }
QString DashboardConfig::displayName(const QString &id) const { return sensorDef(id).displayName; }
QString DashboardConfig::unit(const QString &id) const { return sensorDef(id).unit; }
QColor DashboardConfig::lineColor(const QString &id) const { return sensorDef(id).lineColor; }
QColor DashboardConfig::areaColor(const QString &id) const { return sensorDef(id).areaColor; }
bool DashboardConfig::floorAtZero(const QString &id) const { return sensorDef(id).floorAtZero; }
