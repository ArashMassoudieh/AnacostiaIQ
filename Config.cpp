/////////////////////////////////////////////////////////////
// CONFIG.CPP - Config loading + sensor factory
/////////////////////////////////////////////////////////////

#include "Config.h"
#include "DistanceSensor.h"
#include "MoistureSensor.h"
#include "MaxbotixSensor.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QDebug>

bool Config::load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_error = QString("Cannot open config file: %1").arg(path);
        qWarning() << m_error;
        return false;
    }

    m_raw = file.readAll();
    file.close();

    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(m_raw, &parseErr);
    if (doc.isNull()) {
        m_error = QString("JSON parse error in %1: %2")
                      .arg(path, parseErr.errorString());
        qWarning() << m_error;
        return false;
    }

    QJsonObject root = doc.object();

    // ── App settings (each optional; falls back to default) ─
    QJsonObject app = root.value("app").toObject();
    m_pollInterval = app.value("pollIntervalSeconds").toInt(m_pollInterval);
    m_barrelDepth  = app.value("barrelDepthCm").toDouble(m_barrelDepth);
    m_apiUrl       = app.value("apiUrl").toString(m_apiUrl);

    // ── Weather settings (each optional; falls back to default) ─
    QJsonObject weather = root.value("weather").toObject();
    m_weatherSource = weather.value("source").toString(m_weatherSource);
    m_lat           = weather.value("latitude").toDouble(m_lat);
    m_lon           = weather.value("longitude").toDouble(m_lon);
    m_office        = weather.value("noaaOffice").toString(m_office);
    m_gridX         = weather.value("noaaGridX").toInt(m_gridX);
    m_gridY         = weather.value("noaaGridY").toInt(m_gridY);

    m_loaded = true;
    m_error.clear();
    return true;
}

QVector<Sensor*> Config::createSensors(QObject *parent) const
{
    QVector<Sensor*> result;

    if (!m_loaded) {
        qWarning() << "Config::createSensors called before successful load()";
        return result;
    }

    QJsonDocument doc = QJsonDocument::fromJson(m_raw);
    QJsonArray arr = doc.object().value("sensors").toArray();

    for (const QJsonValue &v : arr) {
        QJsonObject obj = v.toObject();

        const QString type = obj.value("type").toString();
        const QString id   = obj.value("id").toString();
        const QString unit = obj.value("unit").toString();
        const QString name = obj.value("name").toString(id);
        QJsonObject params = obj.value("params").toObject();

        if (id.isEmpty() || type.isEmpty()) {
            qWarning() << "Config: skipping sensor with missing type/id";
            continue;
        }

        Sensor *sensor = nullptr;

        if (type == "distance") {
            int trigPin = params.value("trigPin").toInt(-1);
            int echoPin = params.value("echoPin").toInt(-1);
            double totalLength = params.value("totalLength").toDouble(0.0);
            QString chip = params.value("chip").toString("/dev/gpiochip0");
            sensor = new DistanceSensor(id, unit, name, trigPin, echoPin,
                                        totalLength, chip);
        }
        else if (type == "moisture") {
            int spiChannel = params.value("spiChannel").toInt(0);
            int adcChannel = params.value("adcChannel").toInt(0);
            sensor = new MoistureSensor(id, unit, name, spiChannel, adcChannel);
        }
        else if (type == "maxbotix") {
            QString device     = params.value("device").toString("/dev/ttyAMA0");
            double  totalLength = params.value("totalLength").toDouble(0.0);
            sensor = new MaxbotixSensor(id, unit, name, device, totalLength);
        }
        else {
            qWarning() << "Config: unknown sensor type" << type
                       << "— skipping" << id;
            continue;
        }

        if (parent && sensor)
            sensor->setParent(parent);

        result.append(sensor);
        qDebug() << "Config: created" << type << "sensor" << id;
    }

    return result;
}
