/////////////////////////////////////////////////////////////
// ANACOSTIAIQ.CPP - Main Application Logic
//
//  Digital-twin data collection:
//    Polls sensors + weather on a fixed interval, plots the
//    readings, and pushes every reading to the cloud database.
/////////////////////////////////////////////////////////////

#include "anacostiaiq.h"
#include <QMap>
#include <QSplitter>
#include <QFont>

// ================================================================
//  Constructor / Destructor
// ================================================================

AnacostiaIQ::AnacostiaIQ(QWidget *parent)
    : QMainWindow(parent)
{
    // Load config.json first so settings drive the UI and sensors.
    loadConfiguration();

    // Timer
    monitoringTimer = new QTimer(this);
    connect(monitoringTimer, &QTimer::timeout,
            this, &AnacostiaIQ::onMonitoringTick);

    // Build the sensor registry from config BEFORE the dashboard,
    // so the info panel can create one card per registered sensor.
    registerSensors();

    setupDashboard();

    // Bring each sensor up. The app keeps running even if a sensor
    // or GPIO is missing.
    for (Sensor *s : sensors) {
        bool ok = s->initialize();
        if (!ok)
            qWarning() << s->displayName()
                       << "unavailable — no GPIO or sensor missing";
    }

    // Per-sensor dashboard messaging (marks any unavailable sensor)
    reportSensorAvailability();

    monitoringTimer->start(pollInterval * 1000);
    QTimer::singleShot(0, this, &AnacostiaIQ::onMonitoringTick);
}

AnacostiaIQ::~AnacostiaIQ()
{
}

// ================================================================
//  Configuration
// ================================================================

void AnacostiaIQ::loadConfiguration()
{
    // Look for config.json next to the executable, then the CWD.
    const QString path = "config.json";

    if (config.load(path)) {
        pollInterval = config.pollIntervalSeconds();
        barrelDepth  = config.barrelDepthCm();
        dbWriter.setApiUrl(config.apiUrl());

        // Weather source + location
        fetcher.setSourceFromString(config.weatherSource());
        fetcher.setLocation(config.latitude(), config.longitude());
        fetcher.setNoaaGrid(config.noaaOffice(),
                            config.noaaGridX(), config.noaaGridY());

        qDebug() << "Config loaded from" << path
                 << "| weather source:" << config.weatherSource();
    } else {
        // Missing/invalid config: keep built-in defaults and run on.
        qWarning() << "Using built-in defaults —" << config.errorString();
    }
}

// ================================================================
//  Sensor registry (built by the config factory)
// ================================================================

void AnacostiaIQ::registerSensors()
{
    // The factory constructs each sensor from config.json. To add a
    // sensor, edit config.json — no code change needed here.
    sensors = config.createSensors(this);

    // Resolve convenience handles used for the depth/moisture charts.
    depthSensor    = findSensor("hcsr04_depth");
    moistureSensor = findSensor("moisture_sensor");
}

Sensor *AnacostiaIQ::findSensor(const QString &id) const
{
    for (Sensor *s : sensors)
        if (s->id() == id)
            return s;
    return nullptr;
}

// Generic read + DB write for every registered sensor.
// Owns all sensor DB writes; the specific UI code below does not
// write to the DB, to avoid duplicate readings.
void AnacostiaIQ::logSensorReadings()
{
    lastReadings.clear();
    for (Sensor *s : sensors) {
        if (!s->isAvailable())
            continue;                       // skip missing sensors
        double value = s->measure();
        if (!Sensor::isValid(value))
            continue;                       // skip invalid readings
        lastReadings[s->id()] = value;      // cache for UI reuse
        dbWriter.sendReading(s->id(), value, s->unit());
    }
}

// ================================================================
//  Sensor availability reporting (startup)
// ================================================================

void AnacostiaIQ::reportSensorAvailability()
{
    // A failed initialize() means either there's no GPIO/serial on
    // this machine, or the hardware layer didn't come up. The app
    // keeps running; we surface a message on each affected card and
    // the monitoring tick skips DB writes for missing sensors.
    for (Sensor *s : sensors) {
        if (s->isAvailable())
            continue;
        if (!sensorCards.contains(s->id()))
            continue;

        qWarning() << s->displayName()
                   << "unavailable — no GPIO or sensor missing";
        SensorCard &c = sensorCards[s->id()];
        c.status->setText("⚠ " + s->displayName() + " unavailable");
        c.status->show();
        c.value->setText("--");
        c.value->setStyleSheet("color: #78909c; background: transparent;");
        if (c.bar)
            c.bar->setValue(0);
    }
}

// ================================================================
//  UI Setup
// ================================================================

// Shared card/widget style helpers (file scope so both
// setupDashboard and buildSensorCard can use them).
namespace {

QGroupBox *uiMakeCard(const QString &title, QWidget *parent) {
    QGroupBox *box = new QGroupBox(title, parent);
    box->setStyleSheet(R"(
        QGroupBox {
            font-size: 11px;
            font-weight: bold;
            color: #78909c;
            background-color: #21252b;
            border: 1px solid #2d3139;
            border-radius: 8px;
            margin-top: 10px;
            padding-top: 10px;
            padding-left: 10px;
            padding-right: 10px;
            padding-bottom: 6px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 12px;
            padding: 0 6px;
        }
        QLabel {
            color: #cfd8dc;
            background: transparent;
            border: none;
        }
    )");
    return box;
}

QLabel *uiMakeBigLabel(const QString &text) {
    QLabel *lbl = new QLabel(text);
    QFont f;
    f.setPixelSize(28);
    f.setBold(true);
    lbl->setFont(f);
    lbl->setStyleSheet("color: #eceff1; background: transparent;");
    return lbl;
}

QLabel *uiMakeSmallLabel(const QString &text) {
    QLabel *lbl = new QLabel(text);
    lbl->setStyleSheet("color: #607d8b; font-size: 11px; background: transparent;");
    return lbl;
}

QProgressBar *uiMakeBar(const QString &color) {
    QProgressBar *bar = new QProgressBar();
    bar->setRange(0, 100);
    bar->setValue(0);
    bar->setTextVisible(false);
    bar->setFixedHeight(6);
    bar->setStyleSheet(QString(R"(
        QProgressBar {
            background-color: #2d3139;
            border: none;
            border-radius: 3px;
        }
        QProgressBar::chunk {
            background-color: %1;
            border-radius: 3px;
        }
    )").arg(color));
    return bar;
}

} // namespace

// Build one info-panel card for a sensor. A progress bar is added
// only when the sensor reports a positive fullScale (e.g. depth
// sensors with a totalLength); sensors without a meaningful range
// get value + unit + status only.
QGroupBox *AnacostiaIQ::buildSensorCard(Sensor *s, QWidget *parent) {
    QGroupBox *card = uiMakeCard(s->displayName().toUpper(), parent);
    QVBoxLayout *lay = new QVBoxLayout(card);
    lay->setSpacing(0);

    SensorCard sc;

    QHBoxLayout *row = new QHBoxLayout();
    sc.value = uiMakeBigLabel("--");
    sc.unit  = uiMakeSmallLabel(s->unit());
    row->addWidget(sc.value);
    row->addWidget(sc.unit);
    row->addStretch();
    lay->addLayout(row);

    if (s->fullScale() > 0.0) {
        sc.bar = uiMakeBar("#26c6da");
        lay->addWidget(sc.bar);
    }

    sc.status = new QLabel("", card);
    sc.status->setStyleSheet(
        "color: #ef5350; font-size: 11px; font-weight: bold; background: transparent;");
    sc.status->setWordWrap(true);
    sc.status->hide();
    lay->addWidget(sc.status);

    sensorCards.insert(s->id(), sc);
    return card;
}

void AnacostiaIQ::setupDashboard()
{
    setWindowTitle("AnacostiaIQ");
    resize(1400, 900);

    // ── Global stylesheet ──────────────────────────────────
    setStyleSheet(R"(
        QMainWindow {
            background-color: #1a1d23;
        }
        QSplitter::handle {
            background-color: #2d3139;
        }
    )");

    // ════════════════════════════════════════════════════════
    //  Build the info panel (left column)
    // ════════════════════════════════════════════════════════

    QWidget *infoPanel = new QWidget();
    infoPanel->setFixedWidth(260);
    infoPanel->setStyleSheet("background-color: #1a1d23;");
    QVBoxLayout *infoLayout = new QVBoxLayout(infoPanel);
    infoLayout->setContentsMargins(8, 8, 8, 8);
    infoLayout->setSpacing(0);

    // ── One card per registered sensor (built from config) ──
    for (Sensor *s : sensors)
        infoLayout->addWidget(buildSensorCard(s, infoPanel));

    // ── Config card ────────────────────────────────────────
    QGroupBox *threshCard = uiMakeCard("CONFIG", infoPanel);
    QGridLayout *threshGrid = new QGridLayout(threshCard);
    threshGrid->setSpacing(0);

    auto addThreshRow = [&](int row, const QString &label, const QString &value) {
        QLabel *l = uiMakeSmallLabel(label);
        QLabel *v = new QLabel(value);
        v->setStyleSheet("color: #b0bec5; font-size: 12px; font-weight: bold; background: transparent;");
        v->setAlignment(Qt::AlignRight);
        threshGrid->addWidget(l, row, 0);
        threshGrid->addWidget(v, row, 1);
        return v;
    };

    addThreshRow(0, "Barrel depth",   QString("%1 cm").arg(barrelDepth));
    addThreshRow(1, "Poll interval",  QString("%1 s").arg(pollInterval));

    infoLayout->addWidget(threshCard);

    infoLayout->addStretch();

    // ════════════════════════════════════════════════════════
    //  Build the charts (right side)
    // ════════════════════════════════════════════════════════

    QSplitter *vSplitter = new QSplitter(Qt::Vertical);
    vSplitter->addWidget(weatherChart->GetChartView());

    // Collect the lower charts: one per sensor. Each starts as an
    // empty now→+7day frame (Y 0..fullScale, or 0..100 if none) and
    // fills as readings arrive.
    QList<QWidget*> lowerCharts;
    for (Sensor *s : sensors) {
        ChartContainer *cc = new ChartContainer();
        double yMax = (s->fullScale() > 0.0) ? s->fullScale() : 100.0;
        cc->plotEmpty(s->displayName() + " (" + s->unit() + ")", yMax);
        lowerCharts.append(cc->GetChartView());
        sensorChartMap.insert(s->id(), cc);
    }

    // Arrange them two per row: a vertical stack of horizontal rows,
    // each row holding at most two charts.
    for (int i = 0; i < lowerCharts.size(); i += 2) {
        QSplitter *row = new QSplitter(Qt::Horizontal, vSplitter);
        row->addWidget(lowerCharts[i]);
        if (i + 1 < lowerCharts.size())
            row->addWidget(lowerCharts[i + 1]);
        row->setStretchFactor(0, 1);
        row->setStretchFactor(1, 1);
        vSplitter->addWidget(row);
    }

    // Equal vertical stretch for the weather chart + each row.
    for (int i = 0; i < vSplitter->count(); ++i)
        vSplitter->setStretchFactor(i, 1);

    // ════════════════════════════════════════════════════════
    //  Main layout: info panel | charts
    // ════════════════════════════════════════════════════════

    QWidget *central = new QWidget(this);
    QHBoxLayout *mainLayout = new QHBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(infoPanel);
    mainLayout->addWidget(vSplitter, 1);

    setCentralWidget(central);

    // Initial state
    updateInfoPanels();
}

// ================================================================
//  UI Updates
// ================================================================

void AnacostiaIQ::updateInfoPanels()
{
    // ── Per-sensor cards (generic) ─────────────────────────
    for (Sensor *s : sensors) {
        if (!sensorCards.contains(s->id()))
            continue;
        SensorCard &c = sensorCards[s->id()];

        if (lastReadings.contains(s->id())) {
            double val = lastReadings[s->id()];
            c.value->setText(QString::number(val, 'f', 1));
            c.value->setStyleSheet("color: #26c6da; background: transparent;");
            if (c.bar) {
                double fs = (s->fullScale() > 0.0) ? s->fullScale() : 100.0;
                int pct = qBound(0, static_cast<int>(val / fs * 100), 100);
                c.bar->setValue(pct);
            }
        }
        // If no reading this tick, the tick's status handling (below)
        // already set "--" / warning text; leave the card as-is.
    }
}

// ================================================================
//  MONITORING tick
// ================================================================

void AnacostiaIQ::onMonitoringTick()
{
    // ── Read + log every registered sensor (raw values) ────
    // This owns all generic sensor DB writes and caches the
    // readings in lastReadings for the UI code below.
    logSensorReadings();

    // ── Per-sensor card status (generic) ───────────────────
    // logSensorReadings() already did the DB writes and cached
    // values. Here we just reflect availability/validity on each
    // card; updateInfoPanels() fills in the values.
    for (Sensor *s : sensors) {
        if (!sensorCards.contains(s->id()))
            continue;
        SensorCard &c = sensorCards[s->id()];

        if (!s->isAvailable())
            continue;   // startup message already shown; leave as-is

        if (lastReadings.contains(s->id())) {
            c.status->hide();
        } else {
            // Available but no valid reading this tick.
            c.status->setText("⚠ " + s->displayName() + " not responding");
            c.status->show();
            c.value->setText("--");
            c.value->setStyleSheet("color: #78909c; background: transparent;");
            if (c.bar)
                c.bar->setValue(0);
        }
    }

    // ── Per-sensor charts: append this tick's reading ──────
    for (Sensor *s : sensors) {
        if (lastReadings.contains(s->id()))
            recordSensorPoint(s, lastReadings[s->id()]);
        // No reading → chart keeps its empty now→+7day frame (set at
        // startup), or its existing history if data came earlier.
    }
    if (depthSensor && lastReadings.contains(depthSensor->id()))
        lastDepth = lastReadings[depthSensor->id()];
    if (moistureSensor && lastReadings.contains(moistureSensor->id()))
        lastMoisture = lastReadings[moistureSensor->id()];

    // ── Weather forecast ───────────────────────────────────
    QVector<WeatherData> rainAmount =
        fetcher.getWeatherPrediction(datatype::PrecipitationAmount);
    QVector<WeatherData> rainProb =
        fetcher.getWeatherPrediction(datatype::ProbabilityofPrecipitation);
    QVector<WeatherData> temp =
        fetcher.getWeatherPrediction(datatype::Temperature);

    QMap<QString, QVector<WeatherData>> forecastMap;
    forecastMap["Precipitation [mm]"]            = rainAmount;
    forecastMap["Precipitation probability (%)"] = rainProb;
    forecastMap["Temperature (<sup>o</sup>C)"]   = temp;
    weatherChart->plotWeatherDataMap(forecastMap);
    weatherChart->GetChartView()->setRenderHint(QPainter::Antialiasing);

    dbWriter.sendWeatherData("precip_amount", "mm",  rainAmount);
    dbWriter.sendWeatherData("precip_prob",   "%",   rainProb);
    dbWriter.sendWeatherData("temperature",   "C",   temp);

    updateInfoPanels();
}

// ================================================================
//  Data Recording
// ================================================================

// Append a reading to a sensor's history and replot its chart. Once
// any data exists, the chart auto-ranges to the data (replacing the
// empty now→+7day frame).
void AnacostiaIQ::recordSensorPoint(Sensor *s, double value)
{
    if (!sensorChartMap.contains(s->id()))
        return;

    QVector<WeatherData> &hist = sensorHistory[s->id()];
    if (hist.count() > MAX_HISTORY)
        hist.removeFirst();
    hist.append({QDateTime::currentDateTime(), value});

    sensorChartMap[s->id()]->plotWeatherData(
        hist, s->displayName() + " (" + s->unit() + ")");
}
