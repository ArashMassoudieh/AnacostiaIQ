/////////////////////////////////////////////////////////////
// HEADLESSMONITOR.CPP - Sensor + weather monitoring, no GUI
/////////////////////////////////////////////////////////////

#include "HeadlessMonitor.h"
#include "RainPolicy.h"

#include <QTimer>
#include <QDateTime>
#include <QDebug>
#include <QMap>
#include <QtMath>
#include <cmath>

HeadlessMonitor::HeadlessMonitor(const QString &configPath, QObject *parent)
    : QObject(parent), m_configPath(configPath), healthMonitor(&dbWriter, this) {
}

HeadlessMonitor::~HeadlessMonitor() {
    shutdown();
}

bool HeadlessMonitor::start() {
    if (!config.load(m_configPath)) {
        qCritical().noquote()
            << "Cannot read" << m_configPath << "—" << config.errorString();
        return false;
    }

    loadConfiguration();
    registerSensors();

    if (sensors.isEmpty())
        qWarning() << "No sensors configured — weather polling only";

    int up = 0;
    for (Sensor *s : sensors) {
        if (s->initialize()) {
            ++up;
            qInfo().noquote()
                << QString("  %1 (%2) ready — every %3 s, %4 sample(s)/reading")
                       .arg(s->displayName(), s->id())
                       .arg(effectiveIntervalSeconds(s))
                       .arg(s->samplesPerReading());
        } else {
            qWarning().noquote()
                << QString("  %1 (%2) unavailable — no GPIO or sensor missing")
                       .arg(s->displayName(), s->id());
        }
    }
    qInfo().noquote() << QString("%1 of %2 sensor(s) available")
                             .arg(up).arg(sensors.size());

    healthMonitor.setStationIdentity(config.stationId(), config.stationName());
    healthMonitor.setSensors(sensors);
    const QJsonObject ht = config.healthThresholds();
    HealthMonitor::Thresholds thresholds;
    thresholds.sensorDegradedFailures = qMax(1, ht.value("sensorDegradedFailures").toInt(2));
    thresholds.sensorStaleMinimumSeconds = qMax(1, ht.value("sensorStaleMinimumSeconds").toInt(60));
    thresholds.sensorStalePollFactor = qMax(1, ht.value("sensorStalePollFactor").toInt(3));
    thresholds.moistureDegradedBoundaryReadings = qMax(1, ht.value("moistureDegradedBoundaryReadings").toInt(1));
    thresholds.moistureOfflineBoundaryReadings = qMax(thresholds.moistureDegradedBoundaryReadings,
        ht.value("moistureOfflineBoundaryReadings").toInt(3));
    thresholds.cloudDegradedFailures = qMax(1, ht.value("cloudDegradedFailures").toInt(1));
    thresholds.cloudOfflineFailures = qMax(thresholds.cloudDegradedFailures,
        ht.value("cloudOfflineFailures").toInt(3));
    thresholds.queueDegraded = qMax(1, ht.value("queueDegraded").toInt(500));
    thresholds.queueCritical = qMax(thresholds.queueDegraded,
        ht.value("queueCritical").toInt(2000));
    thresholds.diskDegradedBytes = qMax<qint64>(1, ht.value("diskDegradedMb").toInt(500)) * 1024LL * 1024LL;
    thresholds.diskCriticalBytes = qMin(thresholds.diskDegradedBytes,
        qMax<qint64>(1, ht.value("diskCriticalMb").toInt(100)) * 1024LL * 1024LL);
    thresholds.cpuDegradedC = ht.value("cpuDegradedC").toDouble(70.0);
    thresholds.cpuCriticalC = qMax(thresholds.cpuDegradedC,
        ht.value("cpuCriticalC").toDouble(80.0));
    healthMonitor.setThresholds(thresholds);
    healthMonitor.start();

    startPolling();
    return true;
}

void HeadlessMonitor::loadConfiguration() {
    pollInterval     = config.pollIntervalSeconds();
    weatherInterval = config.weatherIntervalSeconds();
    dbWriter.setApiUrl(config.apiUrl());

    adaptiveEnabled = config.adaptiveEnabled();
    idleFactor      = config.idleIntervalFactor();
    rainThreshold   = config.rainProbabilityThreshold();
    lookaheadHours  = config.rainLookaheadHours();

    fetcher.setSourceFromString(config.weatherSource());
    fetcher.setLocation(config.latitude(), config.longitude());
    fetcher.setNoaaGrid(config.noaaOffice(),
                        config.noaaGridX(), config.noaaGridY());

    qInfo().noquote() << "Config loaded from" << m_configPath
                      << "| station:" << config.stationId()
                      << "| weather source:" << config.weatherSource()
                      << "| API:" << config.apiUrl();
}

void HeadlessMonitor::registerSensors() {
    sensors = config.createSensors(this);
}

void HeadlessMonitor::startPolling() {
    for (Sensor *s : sensors) {
        QTimer *t = new QTimer(this);
        connect(t, &QTimer::timeout, this, [this, s]() { pollSensor(s); });
        t->start(effectiveIntervalSeconds(s) * 1000);
        sensorTimers.insert(s, t);
        QTimer::singleShot(0, this, [this, s]() { pollSensor(s); });
    }

    weatherTimer = new QTimer(this);
    connect(weatherTimer, &QTimer::timeout, this, &HeadlessMonitor::pollWeather);
    weatherTimer->start(weatherInterval * 1000);
    QTimer::singleShot(0, this, &HeadlessMonitor::pollWeather);
}

void HeadlessMonitor::shutdown() {
    if (m_stopped)
        return;
    m_stopped = true;

    healthMonitor.stop();

    for (auto it = sensorTimers.cbegin(); it != sensorTimers.cend(); ++it)
        it.value()->stop();
    if (weatherTimer)
        weatherTimer->stop();

    for (Sensor *s : sensors)
        s->cleanup();

    qInfo() << "Monitor stopped; hardware released";
}

int HeadlessMonitor::effectiveIntervalSeconds(Sensor *s) const {
    const int base = (s->pollIntervalSeconds() > 0)
                         ? s->pollIntervalSeconds()
                         : pollInterval;

    if (!adaptiveEnabled || !lowFrequency)
        return base;

    static const int MAX_INTERVAL_SEC = 24 * 3600;
    const qint64 scaled = static_cast<qint64>(base) * idleFactor;
    return static_cast<int>(qMin<qint64>(scaled, MAX_INTERVAL_SEC));
}

void HeadlessMonitor::setLowFrequencyMode(bool low) {
    if (low == lowFrequency)
        return;

    lowFrequency = low;

    for (auto it = sensorTimers.cbegin(); it != sensorTimers.cend(); ++it)
        it.value()->start(effectiveIntervalSeconds(it.key()) * 1000);

    qInfo() << "Adaptive polling:" << (low ? "LOW" : "HIGH") << "frequency mode"
            << "— sensor intervals x" << (low ? idleFactor : 1);
}

void HeadlessMonitor::pollSensor(Sensor *s) {
    if (!s || m_stopped)
        return;
    if (!s->isAvailable()) {
        healthMonitor.evaluateNow();
        return;
    }

    const double value = s->takeReading();

    if (!Sensor::isValid(value)) {
        qWarning().noquote()
            << QString("%1: no valid reading").arg(s->displayName());
        healthMonitor.evaluateNow();
        return;
    }

    qInfo().noquote() << QString("%1 = %2 %3")
                             .arg(s->displayName())
                             .arg(value, 0, 'f', 2)
                             .arg(s->unit());

    dbWriter.sendReading(s->id(), value, s->unit());
    publishDerivedWeirFlow(s, value);
    healthMonitor.evaluateNow();
}

double HeadlessMonitor::headToMeters(double head, const QString &unit) const {
    if (unit.compare("m", Qt::CaseInsensitive) == 0)
        return head;
    if (unit.compare("cm", Qt::CaseInsensitive) == 0)
        return head / 100.0;
    if (unit.compare("mm", Qt::CaseInsensitive) == 0)
        return head / 1000.0;
    if (unit.compare("in", Qt::CaseInsensitive) == 0)
        return head * 0.0254;
    qWarning() << "V-notch flow: unsupported head unit" << unit;
    return -1.0;
}

void HeadlessMonitor::publishDerivedWeirFlow(Sensor *source, double head) {
    if (!source || !config.weirFlowEnabled() ||
        source->id() != config.weirHeadSensorId())
        return;

    const double cd = config.weirDischargeCoefficient();
    const double angleDeg = config.weirNotchAngleDegrees();
    const double minHead = config.weirMinimumHead();

    // Geometry/calibration is deliberately required rather than guessed.
    if (!(cd > 0.0) || !(angleDeg > 0.0 && angleDeg < 180.0)) {
        qWarning() << "V-notch flow enabled but dischargeCoefficient/notchAngleDegrees is invalid";
        return;
    }

    // At very low head the HC-SR04 resolution dominates the h^(5/2)
    // relationship. Do not publish a misleading numerical flow value.
    if (head < minHead) {
        qInfo().noquote()
            << QString("Inflow Flow Rate withheld: head %1 %2 < minimum %3 %2")
                   .arg(head, 0, 'f', 3).arg(source->unit())
                   .arg(minHead, 0, 'f', 3);
        return;
    }

    const double h = headToMeters(head, source->unit());
    if (!(h >= 0.0))
        return;

    constexpr double g = 9.80665; // m/s^2
    const double theta = qDegreesToRadians(angleDeg);
    const double qM3s = (8.0 / 15.0) * cd * std::sqrt(2.0 * g)
                       * std::tan(theta / 2.0) * std::pow(h, 2.5);

    QString unit = config.weirFlowUnit();
    double flow = qM3s;
    if (unit.compare("L/s", Qt::CaseInsensitive) == 0)
        flow *= 1000.0;
    else if (unit.compare("cfs", Qt::CaseInsensitive) == 0)
        flow *= 35.3146667215;
    else if (unit.compare("m3/s", Qt::CaseInsensitive) != 0) {
        qWarning() << "V-notch flow: unsupported outputUnit" << unit
                   << "— using m3/s";
        unit = "m3/s";
    }

    qInfo().noquote() << QString("Inflow Flow Rate = %1 %2")
                             .arg(flow, 0, 'f', 4).arg(unit);
    dbWriter.sendReading(config.weirFlowSensorId(), flow, unit);
}

void HeadlessMonitor::pollWeather() {
    if (m_stopped)
        return;

    const QVector<WeatherData> rainAmount =
        fetcher.getWeatherPrediction(datatype::PrecipitationAmount);
    const QVector<WeatherData> rainProb =
        fetcher.getWeatherPrediction(datatype::ProbabilityofPrecipitation);
    const QVector<WeatherData> temp =
        fetcher.getWeatherPrediction(datatype::Temperature);

    // A shutdown signal can arrive while a synchronous weather request is in
    // progress. Do not append hundreds of forecast records after shutdown has
    // already started.
    if (m_stopped) {
        qInfo() << "Weather result discarded because shutdown is in progress";
        return;
    }

    qInfo().noquote()
        << QString("Forecast: %1 precip, %2 probability, %3 temperature point(s)")
               .arg(rainAmount.size()).arg(rainProb.size()).arg(temp.size());

    dbWriter.sendWeatherData("precip_amount", "mm", rainAmount);
    dbWriter.sendWeatherData("precip_prob",   "%",  rainProb);
    dbWriter.sendWeatherData("temperature",   "C",  temp);

    healthMonitor.evaluateNow();

    if (!adaptiveEnabled)
        return;

    const RainPolicy::Decision decision =
        RainPolicy::evaluate(rainProb, QDateTime::currentDateTime(),
                             lookaheadHours, rainThreshold);

    haveForecast = (decision != RainPolicy::Decision::NoDataInWindow);
    if (!haveForecast)
        qWarning() << "Adaptive polling: no usable precipitation forecast "
                      "— staying at high frequency";

    setLowFrequencyMode(decision == RainPolicy::Decision::Dry);
}
