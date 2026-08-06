/////////////////////////////////////////////////////////////
// HEADLESSMONITOR.CPP - Sensor + weather monitoring, no GUI
//
//  Mirrors the polling behaviour of AnacostiaIQ (anacostiaiq.cpp)
//  with the dashboard removed. Keep the two in step: the adaptive
//  rule in particular is a correctness property, not a display
//  detail — we slow the sensors down only when we positively know
//  it's dry.
/////////////////////////////////////////////////////////////

#include "HeadlessMonitor.h"
#include "RainPolicy.h"

#include <QTimer>
#include <QDateTime>
#include <QDebug>
#include <QMap>

HeadlessMonitor::HeadlessMonitor(const QString &configPath, QObject *parent)
    : QObject(parent), m_configPath(configPath) {
}

HeadlessMonitor::~HeadlessMonitor() {
    shutdown();
}

// ================================================================
//  Startup
// ================================================================

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

    // Bring each sensor up. A missing sensor is not fatal: the others
    // keep reporting, and this one is simply skipped every tick.
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

    startPolling();
    return true;
}

void HeadlessMonitor::loadConfiguration() {
    pollInterval    = config.pollIntervalSeconds();
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
                      << "| weather source:" << config.weatherSource()
                      << "| API:" << config.apiUrl();
}

void HeadlessMonitor::registerSensors() {
    // Same factory the GUI uses: config.json fully drives what exists.
    sensors = config.createSensors(this);
}

void HeadlessMonitor::startPolling() {
    for (Sensor *s : sensors) {
        QTimer *t = new QTimer(this);
        connect(t, &QTimer::timeout, this, [this, s]() { pollSensor(s); });
        t->start(effectiveIntervalSeconds(s) * 1000);
        sensorTimers.insert(s, t);

        // First reading immediately, so the log shows life at startup
        // rather than after a full interval.
        QTimer::singleShot(0, this, [this, s]() { pollSensor(s); });
    }

    // Weather on its own interval, never scaled: this poll is what
    // notices rain returning and pulls us back to high frequency.
    weatherTimer = new QTimer(this);
    connect(weatherTimer, &QTimer::timeout, this, &HeadlessMonitor::pollWeather);
    weatherTimer->start(weatherInterval * 1000);
    QTimer::singleShot(0, this, &HeadlessMonitor::pollWeather);
}

// ================================================================
//  Shutdown
// ================================================================

void HeadlessMonitor::shutdown() {
    if (m_stopped)
        return;
    m_stopped = true;

    for (auto it = sensorTimers.cbegin(); it != sensorTimers.cend(); ++it)
        it.value()->stop();
    if (weatherTimer)
        weatherTimer->stop();

    // Hand the GPIO lines back to the kernel so a restart can claim
    // them again — libgpiod holds them exclusively.
    for (Sensor *s : sensors)
        s->cleanup();

    qInfo() << "Monitor stopped; hardware released";
}

// ================================================================
//  Adaptive polling
// ================================================================

int HeadlessMonitor::effectiveIntervalSeconds(Sensor *s) const {
    const int base = (s->pollIntervalSeconds() > 0)
                         ? s->pollIntervalSeconds()
                         : pollInterval;

    if (!adaptiveEnabled || !lowFrequency)
        return base;

    // Clamped to a day so a large idleFactor can't overflow the
    // millisecond int QTimer::start() takes.
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

// ================================================================
//  Polling
// ================================================================

void HeadlessMonitor::pollSensor(Sensor *s) {
    if (!s || m_stopped)
        return;
    if (!s->isAvailable())
        return;   // reported at startup; don't repeat it every tick

    const double value = s->takeReading();   // averages samplesPerReading

    if (!Sensor::isValid(value)) {
        qWarning().noquote()
            << QString("%1: no valid reading").arg(s->displayName());
        return;
    }

    qInfo().noquote() << QString("%1 = %2 %3")
                             .arg(s->displayName())
                             .arg(value, 0, 'f', 2)
                             .arg(s->unit());

    dbWriter.sendReading(s->id(), value, s->unit());
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

    qInfo().noquote()
        << QString("Forecast: %1 precip, %2 probability, %3 temperature point(s)")
               .arg(rainAmount.size()).arg(rainProb.size()).arg(temp.size());

    dbWriter.sendWeatherData("precip_amount", "mm", rainAmount);
    dbWriter.sendWeatherData("precip_prob",   "%",  rainProb);
    dbWriter.sendWeatherData("temperature",   "C",  temp);

    // ── Re-evaluate the polling cadence ────────────────────
    // A failed fetch or a stale forecast leaves us at base intervals:
    // dropping the sampling rate exactly when we've lost visibility is
    // the one outcome we can't accept.
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
