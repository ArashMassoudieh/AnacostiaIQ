/////////////////////////////////////////////////////////////
// CONFIG.CPP - Config loading + sensor factory
/////////////////////////////////////////////////////////////

#include "Config.h"
#include "DistanceSensor.h"
#include "MoistureSensor.h"
#include "MaxbotixSensor.h"
#include "AdcBus.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QDebug>

#include <memory>

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

    // ── Adaptive polling (each optional; falls back to default) ─
    QJsonObject adaptive = root.value("adaptive").toObject();
    m_adaptiveEnabled = adaptive.value("enabled").toBool(m_adaptiveEnabled);
    m_idleFactor      = adaptive.value("idleFactor").toInt(m_idleFactor);
    m_rainThreshold   = adaptive.value("rainProbabilityThreshold")
                            .toDouble(m_rainThreshold);
    m_lookaheadHours  = adaptive.value("lookaheadHours").toInt(m_lookaheadHours);

    // A factor below 1 would speed sensors up when it's dry, which is
    // the opposite of the intent; 1 disables the scaling.
    if (m_idleFactor < 1) {
        qWarning() << "Config: adaptive.idleFactor" << m_idleFactor
                   << "< 1 — clamping to 1 (no scaling)";
        m_idleFactor = 1;
    }
    if (m_lookaheadHours < 1) {
        qWarning() << "Config: adaptive.lookaheadHours" << m_lookaheadHours
                   << "< 1 — clamping to 1";
        m_lookaheadHours = 1;
    }

    // ── Weather settings (each optional; falls back to default) ─
    QJsonObject weather = root.value("weather").toObject();
    // Weather interval defaults to the app-level poll interval when absent.
    m_weatherInterval = weather.value("intervalSeconds").toInt(m_pollInterval);
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
    QJsonObject root = doc.object();
    QJsonArray arr = root.value("sensors").toArray();

    // ── Shared ADC bus (built before any sensor) ───────────
    // Every "moisture" entry is one channel of a single ADC0804 +
    // CD4014 bank that shares its WR / P/S / CLOCK lines, so the bus
    // has to know all its channels up front — libgpiod requests the
    // data lines in one batch. Skipped entirely when no moisture
    // sensor is configured, leaving those GPIOs free.
    std::shared_ptr<AdcBus> adcBus;
    QVector<int> moistureDataPins;
    for (const QJsonValue &v : arr) {
        const QJsonObject obj = v.toObject();
        if (obj.value("type").toString() != "moisture")
            continue;
        if (obj.value("id").toString().isEmpty())
            continue;   // the loop below skips it; don't claim its line
        const int pin = obj.value("params").toObject()
                            .value("dataPin").toInt(-1);
        if (pin >= 0)
            moistureDataPins.append(pin);
    }

    if (!moistureDataPins.isEmpty()) {
        const QJsonObject adc = root.value("adc").toObject();
        adcBus = std::make_shared<AdcBus>(
            adc.value("chip").toString("/dev/gpiochip0"),
            adc.value("wrPin").toInt(-1),
            adc.value("psPin").toInt(-1),
            adc.value("clockPin").toInt(-1),
            adc.value("conversionDelayMs").toInt(50),
            adc.value("pulseWidthUs").toInt(500),
            adc.value("cacheMs").toInt(500));

        for (int pin : moistureDataPins)
            adcBus->addChannel(pin);
    }

    for (const QJsonValue &v : arr) {
        QJsonObject obj = v.toObject();

        const QString type = obj.value("type").toString();
        const QString id   = obj.value("id").toString();
        const QString unit = obj.value("unit").toString();
        const QString name = obj.value("name").toString(id);
        QJsonObject params = obj.value("params").toObject();

        // Per-sensor poll interval; falls back to the app-level default
        // when the entry omits "intervalSeconds".
        const int interval = obj.value("intervalSeconds").toInt(m_pollInterval);

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
            // Pins/timing for the bus itself live in the "adc" block;
            // a moisture entry only names its channel and calibration.
            int dataPin = params.value("dataPin").toInt(-1);
            int adcDry  = params.value("adcDry").toInt(105);
            int adcWet  = params.value("adcWet").toInt(32);

            sensor = new MoistureSensor(id, unit, name, adcBus, dataPin,
                                        adcDry, adcWet);
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

        if (sensor)
            sensor->setPollIntervalSeconds(interval);

        result.append(sensor);
        qDebug() << "Config: created" << type << "sensor" << id
                 << "| interval(s):" << interval;
    }

    return result;
}
