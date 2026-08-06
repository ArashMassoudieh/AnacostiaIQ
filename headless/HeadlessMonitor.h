/////////////////////////////////////////////////////////////
// HEADLESSMONITOR.H - Sensor + weather monitoring, no GUI
//
//  The non-UI half of AnacostiaIQ, extracted so it can run on a Pi
//  with no display: same config.json, same sensor factory, same DB
//  writer, same adaptive-polling rule. Readings go to the log and to
//  the cloud API instead of to cards and charts.
//
//  Everything it depends on (Config, Sensor, DatabaseWriter,
//  WeatherFetcher, RainPolicy) is already widget-free, so this
//  target links QtCore + QtNetwork only.
/////////////////////////////////////////////////////////////

#ifndef HEADLESSMONITOR_H
#define HEADLESSMONITOR_H

#include <QObject>
#include <QHash>
#include <QVector>
#include <QString>

#include "Config.h"
#include "Sensor.h"
#include "DatabaseWriter.h"
#include "WeatherFetcher.h"

class QTimer;

class HeadlessMonitor : public QObject {
    Q_OBJECT

public:
    explicit HeadlessMonitor(const QString &configPath,
                             QObject *parent = nullptr);
    ~HeadlessMonitor() override;

    // Load config, bring up the sensors, start the timers. Returns
    // false only when config.json couldn't be read — a service told to
    // use a config that isn't there should fail loudly rather than run
    // on built-in defaults pointing at a default API URL.
    bool start();

    // Release the hardware and stop polling. Idempotent, so the signal
    // handler and the destructor can both call it.
    void shutdown();

private slots:
    // Read one sensor and push the value to the DB.
    void pollSensor(Sensor *s);
    // Fetch the forecast, push it, and re-evaluate the polling cadence.
    void pollWeather();

private:
    void loadConfiguration();
    void registerSensors();
    void startPolling();

    // Base interval scaled by idleFactor when the forecast is dry.
    int  effectiveIntervalSeconds(Sensor *s) const;
    // Restart every sensor timer on the new cadence. Only acts on a
    // real transition — QTimer::start() resets the countdown, so
    // running this each weather tick would starve any sensor whose
    // interval exceeds the weather interval.
    void setLowFrequencyMode(bool low);

    QString m_configPath;

    Config         config;
    DatabaseWriter dbWriter;
    WeatherFetcher fetcher;

    QVector<Sensor *>        sensors;
    QHash<Sensor *, QTimer *> sensorTimers;
    QTimer                   *weatherTimer = nullptr;

    // ── Settings (from config.json) ────────────────────────
    int pollInterval    = 3600;
    int weatherInterval = 3600;

    bool   adaptiveEnabled = true;
    int    idleFactor      = 10;
    double rainThreshold   = 0.0;
    int    lookaheadHours  = 24;

    // ── Adaptive state ─────────────────────────────────────
    bool lowFrequency = false;
    bool haveForecast = false;

    bool m_stopped = false;
};

#endif // HEADLESSMONITOR_H
