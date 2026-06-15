/////////////////////////////////////////////////////////////
// ANACOSTIAIQ.H - Main Application Class Header
/////////////////////////////////////////////////////////////

#ifndef ANACOSTIAIQ_H
#define ANACOSTIAIQ_H

#include <QMainWindow>
#include "WeatherFetcher.h"
#include "chartcontainer.h"
#include "Sensor.h"
#include "DistanceSensor.h"
#include "MoistureSensor.h"
#include "DatabaseWriter.h"
#include "Config.h"
#include <QTimer>
#include <QVector>
#include <QMap>
#include <QLabel>
#include <QGroupBox>
#include <QFrame>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
//using namespace QtCharts;
#endif

class AnacostiaIQ : public QMainWindow
{
    Q_OBJECT

public:
    AnacostiaIQ(QWidget *parent = nullptr);
    ~AnacostiaIQ();

    // ── Tunable parameters (loaded from config.json) ───────

    // Legacy barrel depth (kept for the config card display). Depth
    // sensors now report finished depth themselves, so this is no
    // longer used in the depth math.
    double barrelDepth = 137.16;                        // cm

    int pollInterval  = 3600;                           // Default sensor polling interval (seconds)
    int weatherInterval = 3600;                         // Weather polling interval (seconds)
    bool sensorEnabled      = true;

private slots:
    // Poll a single sensor: measure, log to DB, update its card + chart.
    void pollSensor(Sensor *s);
    // Poll the weather group: fetch all variables, plot, write to DB.
    void pollWeather();

private:
    // ── State ──────────────────────────────────────────────
    double        lastDepth     = 0;
    double        lastMoisture  = 0;

    // ── Hardware (built by the config factory) ─────────────
    // Sensors are constructed from config.json and owned via this
    // registry. The monitoring tick reads + logs them uniformly.
    // Adding a sensor = add an entry to config.json (and a card if
    // you want it on the dashboard).
    QVector<Sensor*> sensors;
    void loadConfiguration();   // read config.json into settings + sensors
    void registerSensors();     // (re)build the sensor list from config
    void startPolling();        // create + start a timer per sensor + weather

    // Convenience handles into 'sensors', resolved by id after the
    // factory runs. Null if the corresponding sensor isn't configured.
    Sensor *depthSensor    = nullptr;
    Sensor *moistureSensor = nullptr;
    Sensor *findSensor(const QString &id) const;

    // Cache of this tick's raw readings, keyed by sensor id(), so
    // UI code can reuse a value without triggering a second
    // hardware read in the same tick.
    QMap<QString, double> lastReadings;

    // ── Timers ─────────────────────────────────────────────
    // One timer per sensor (each on its own interval) plus a single
    // weather timer. Parented to this, so they're cleaned up with it.
    QVector<QTimer*> sensorTimers;
    QTimer *weatherTimer = nullptr;

    // ── Weather ────────────────────────────────────────────
    WeatherFetcher fetcher;

    // ── Data history ───────────────────────────────────────
    static const int MAX_HISTORY = 100;

    // ── UI setup ───────────────────────────────────────────
    void setupDashboard();
    void updateInfoPanels();
    void reportSensorAvailability();

    // ── Charts ─────────────────────────────────────────────
    // Fixed weather forecast chart (precip / probability / temp).
    ChartContainer *weatherChart    = new ChartContainer();

    // One chart per sensor, keyed by sensor id (mirrors sensorCards).
    // Each is empty-framed (now → +7 days) until data arrives.
    QMap<QString, ChartContainer*>      sensorChartMap;
    QMap<QString, QVector<WeatherData>> sensorHistory;   // per-sensor series
    void recordSensorPoint(Sensor *s, double value);     // append + replot

    // ── Info panel labels ──────────────────────────────────
    // ── Info panel: one card per sensor, built from config ─
    // Each registered sensor gets a card. Widgets are looked up by
    // sensor id when updating, so adding a sensor in config.json
    // automatically adds its panel — no code change here.
    struct SensorCard {
        QLabel       *value  = nullptr;  // big numeric value
        QLabel       *unit   = nullptr;  // unit suffix
        QProgressBar *bar    = nullptr;  // optional; null if no fullScale
        QLabel       *status = nullptr;  // warning line (hidden unless error)
    };
    QMap<QString, SensorCard> sensorCards;   // keyed by sensor id
    QGroupBox *buildSensorCard(Sensor *s, QWidget *parent);

    // ── Configuration ──────────────────────────────────────
    Config config;

    // ── Database ───────────────────────────────────────────
    DatabaseWriter dbWriter;
};

#endif // ANACOSTIAIQ_H
