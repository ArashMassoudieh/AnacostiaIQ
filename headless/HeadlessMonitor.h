/////////////////////////////////////////////////////////////
// HEADLESSMONITOR.H - Sensor + weather monitoring, no GUI
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
#include "HealthMonitor.h"

class QTimer;

class HeadlessMonitor : public QObject {
    Q_OBJECT

public:
    explicit HeadlessMonitor(const QString &configPath,
                             QObject *parent = nullptr);
    ~HeadlessMonitor() override;

    bool start();
    void shutdown();

private slots:
    void pollSensor(Sensor *s);
    void pollWeather();

private:
    void loadConfiguration();
    void registerSensors();
    void startPolling();

    int  effectiveIntervalSeconds(Sensor *s) const;
    void setLowFrequencyMode(bool low);

    QString m_configPath;

    Config         config;
    DatabaseWriter dbWriter;
    WeatherFetcher fetcher;
    HealthMonitor  healthMonitor{&dbWriter, this};

    QVector<Sensor *>         sensors;
    QHash<Sensor *, QTimer *> sensorTimers;
    QTimer                   *weatherTimer = nullptr;

    int pollInterval    = 3600;
    int weatherInterval = 3600;

    bool   adaptiveEnabled = true;
    int    idleFactor      = 10;
    double rainThreshold   = 0.0;
    int    lookaheadHours  = 24;

    bool lowFrequency = false;
    bool haveForecast = false;

    bool m_stopped = false;
};

#endif // HEADLESSMONITOR_H
