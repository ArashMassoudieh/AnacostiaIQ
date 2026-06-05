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

    setupDashboard();

    // Build the sensor registry from config, then bring each one up.
    // The app keeps running even if a sensor or GPIO is missing.
    registerSensors();
    for (Sensor *s : sensors) {
        bool ok = s->initialize();
        if (!ok)
            qWarning() << s->displayName()
                       << "unavailable — no GPIO or sensor missing";
    }

    // Per-sensor dashboard messaging for the two that have cards
    reportSensorAvailability(depthSensor    && depthSensor->isAvailable(),
                             moistureSensor && moistureSensor->isAvailable());

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

    // Resolve convenience handles used by the dashboard cards.
    depthSensor    = findSensor("hcsr04_depth");
    moistureSensor = findSensor("moisture_sensor");

    // Use the depth sensor's full-scale (its totalLength) for the
    // dashboard progress bar, in the sensor's own unit.
    if (depthSensor && depthSensor->fullScale() > 0.0)
        depthFullScale = depthSensor->fullScale();
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

void AnacostiaIQ::reportSensorAvailability(bool depthOk, bool moistureOk)
{
    // A failed initialize() means either there's no GPIO on this
    // machine, or the hardware layer (libgpiod/SPI) didn't come up.
    // The app keeps running; we just surface a message and the
    // monitoring tick will skip DB writes for the missing sensor.

    if (!depthOk) {
        qWarning() << "Depth sensor unavailable — no GPIO or sensor missing";
        sensorStatusLabel->setText("⚠ Depth sensor unavailable (no GPIO)");
        sensorStatusLabel->show();
        depthValueLabel->setText("--");
        depthValueLabel->setStyleSheet("color: #78909c; background: transparent;");
        depthBar->setValue(0);
    }

    if (!moistureOk) {
        qWarning() << "Moisture sensor unavailable — no GPIO or sensor missing";
        moistureStatusLabel->setText("⚠ Moisture sensor unavailable (no GPIO)");
        moistureStatusLabel->show();
        moistureValueLabel->setText("--");
        moistureValueLabel->setStyleSheet("color: #78909c; background: transparent;");
        moistureBar->setValue(0);
    }
}

// ================================================================
//  UI Setup
// ================================================================

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

    // ── Helper: create an info card ────────────────────────
    auto makeCard = [](const QString &title, QWidget *parent) -> QGroupBox* {
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
    };

    // ── Helper: big value label ────────────────────────────
    auto makeBigLabel = [](const QString &text) -> QLabel* {
        QLabel *lbl = new QLabel(text);
        QFont f;
        f.setPixelSize(28);
        f.setBold(true);
        lbl->setFont(f);
        lbl->setStyleSheet("color: #eceff1; background: transparent;");
        return lbl;
    };

    // ── Helper: small label ────────────────────────────────
    auto makeSmallLabel = [](const QString &text) -> QLabel* {
        QLabel *lbl = new QLabel(text);
        lbl->setStyleSheet("color: #607d8b; font-size: 11px; background: transparent;");
        return lbl;
    };

    // ── Helper: progress bar ───────────────────────────────
    auto makeBar = [](const QString &color) -> QProgressBar* {
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
    };

    // ── Helper: indicator dot ──────────────────────────────
    auto makeDot = [](const QString &color) -> QFrame* {
        QFrame *dot = new QFrame();
        dot->setFixedSize(14, 14);
        dot->setStyleSheet(QString(
                               "background-color: %1; border-radius: 7px; border: none;"
                               ).arg(color));
        return dot;
    };

    // ════════════════════════════════════════════════════════
    //  Build the info panel (left column)
    // ════════════════════════════════════════════════════════

    QWidget *infoPanel = new QWidget();
    infoPanel->setFixedWidth(260);
    infoPanel->setStyleSheet("background-color: #1a1d23;");
    QVBoxLayout *infoLayout = new QVBoxLayout(infoPanel);
    infoLayout->setContentsMargins(8, 8, 8, 8);
    infoLayout->setSpacing(0);

    // ── Water Depth card ───────────────────────────────────
    QGroupBox *depthCard = makeCard("WATER DEPTH", infoPanel);
    QVBoxLayout *depthLay = new QVBoxLayout(depthCard);
    depthLay->setSpacing(0);

    QHBoxLayout *depthRow = new QHBoxLayout();
    depthValueLabel = makeBigLabel("--");
    depthUnitLabel  = makeSmallLabel("cm");
    depthRow->addWidget(depthValueLabel);
    depthRow->addWidget(depthUnitLabel);
    depthRow->addStretch();
    depthLay->addLayout(depthRow);

    depthBar = makeBar("#26c6da");
    depthLay->addWidget(depthBar);

    sensorStatusLabel = new QLabel("", depthCard);
    sensorStatusLabel->setStyleSheet(
        "color: #ef5350; font-size: 11px; font-weight: bold; background: transparent;");
    sensorStatusLabel->setWordWrap(true);
    sensorStatusLabel->hide();
    depthLay->addWidget(sensorStatusLabel);

    infoLayout->addWidget(depthCard);

    // ── Soil Moisture card ───────────────────────────────────
    QGroupBox *moistureCard = makeCard("SOIL MOISTURE", infoPanel);
    QVBoxLayout *moistureLay = new QVBoxLayout(moistureCard);
    moistureLay->setSpacing(0);

    QHBoxLayout *moistureRow = new QHBoxLayout();
    moistureValueLabel = makeBigLabel("--");
    moistureUnitLabel  = makeSmallLabel("%");
    moistureRow->addWidget(moistureValueLabel);
    moistureRow->addWidget(moistureUnitLabel);
    moistureRow->addStretch();
    moistureLay->addLayout(moistureRow);

    moistureBar = makeBar("#26c6da");
    moistureLay->addWidget(moistureBar);

    moistureStatusLabel = new QLabel("", moistureCard);
    moistureStatusLabel->setStyleSheet(
        "color: #ef5350; font-size: 11px; font-weight: bold; background: transparent;");
    moistureStatusLabel->setWordWrap(true);
    moistureStatusLabel->hide();
    moistureLay->addWidget(moistureStatusLabel);

    infoLayout->addWidget(moistureCard);

    // ── Cumulative Rain card ───────────────────────────────
    QGroupBox *rainCard = makeCard("CUMULATIVE RAIN (2-DAY)", infoPanel);
    QVBoxLayout *rainLay = new QVBoxLayout(rainCard);
    rainLay->setSpacing(0);

    QHBoxLayout *rainRow = new QHBoxLayout();
    rainValueLabel = makeBigLabel("--");
    rainUnitLabel  = makeSmallLabel("mm");
    rainRow->addWidget(rainValueLabel);
    rainRow->addWidget(rainUnitLabel);
    rainRow->addStretch();
    rainLay->addLayout(rainRow);

    rainBar = makeBar("#42a5f5");
    rainLay->addWidget(rainBar);

    infoLayout->addWidget(rainCard);

    // ── Config card ────────────────────────────────────────
    QGroupBox *threshCard = makeCard("CONFIG", infoPanel);
    QGridLayout *threshGrid = new QGridLayout(threshCard);
    threshGrid->setSpacing(0);

    auto addThreshRow = [&](int row, const QString &label, const QString &value) {
        QLabel *l = makeSmallLabel(label);
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

    QSplitter *hSplitterMiddle = new QSplitter(Qt::Horizontal, vSplitter);
    hSplitterMiddle->addWidget(cumulativeChart->GetChartView());
    hSplitterMiddle->addWidget(depthChart->GetChartView());

    vSplitter->addWidget(moistureChart->GetChartView());

    vSplitter->setStretchFactor(0, 1);
    vSplitter->setStretchFactor(1, 1);
    vSplitter->setStretchFactor(2, 1);
    hSplitterMiddle->setStretchFactor(0, 1);
    hSplitterMiddle->setStretchFactor(1, 1);

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
    // Depth
    depthValueLabel->setText(QString::number(lastDepth, 'f', 1));
    // Scale the bar against the configured full-scale depth. Note this
    // is a display-only bound; the sensor reports finished depth in its
    // own configured unit, so keep depthFullScale in that same unit.
    double fullScale = (depthFullScale > 0.0) ? depthFullScale : 1.0;
    int depthPct = qBound(0, static_cast<int>(lastDepth / fullScale * 100), 100);
    depthBar->setValue(depthPct);

    // Moisture
    moistureValueLabel->setText(QString::number(lastMoisture, 'f', 1));
    int moisturePct = qBound(0, static_cast<int>(lastMoisture), 100);
    moistureBar->setValue(moisturePct);

    depthValueLabel->setStyleSheet("color: #26c6da; background: transparent;");
    moistureValueLabel->setStyleSheet("color: #26c6da; background: transparent;");

    // Rain
    rainValueLabel->setText(QString::number(lastCumRain, 'f', 1));
    int rainPct = qBound(0, static_cast<int>(lastCumRain / 30.0 * 100), 100);
    rainBar->setValue(rainPct);
    rainValueLabel->setStyleSheet("color: #42a5f5; background: transparent;");
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

    // ── Depth: the sensor now reports finished depth directly ──
    // (DB logging is handled generically in logSensorReadings; here
    //  we only update the dashboard card.)
    if (depthSensor && depthSensor->isAvailable()) {
        if (lastReadings.contains(depthSensor->id())) {
            lastDepth = lastReadings[depthSensor->id()];
            sensorStatusLabel->hide();
            sensorFailCount = 0;
            recordDepth(lastDepth);
        } else {
            // available but no valid reading this tick
            sensorFailCount++;
            if (sensorFailCount >= MAX_SENSOR_FAILS) {
                sensorStatusLabel->setText(
                    "⚠ Sensor not connected or malfunctioning");
                sensorStatusLabel->show();
                depthValueLabel->setText("--");
                depthValueLabel->setStyleSheet(
                    "color: #78909c; background: transparent;");
                depthBar->setValue(0);
            }
        }
    }

    // ── Moisture: UI from cached reading ───────────────────
    if (moistureSensor && moistureSensor->isAvailable()) {
        if (lastReadings.contains(moistureSensor->id())) {
            lastMoisture = lastReadings[moistureSensor->id()];
            moistureStatusLabel->hide();
            recordMoisture(lastMoisture);
        } else {
            moistureStatusLabel->setText("⚠ Moisture sensor not responding");
            moistureStatusLabel->show();
            moistureValueLabel->setText("--");
            moistureValueLabel->setStyleSheet(
                "color: #78909c; background: transparent;");
            moistureBar->setValue(0);
        }
    }

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

    // ── Cumulative rain (2-day) ────────────────────────────
    lastCumRain = calculateCumulativeValue(rainAmount, 2);

    if (cumulativeRainHistory.count() > MAX_HISTORY)
        cumulativeRainHistory.removeFirst();
    cumulativeRainHistory.append({QDateTime::currentDateTime(), lastCumRain});
    cumulativeChart->plotWeatherData(cumulativeRainHistory,
                                     "Cumulative rain forecast [mm]");

    updateInfoPanels();
}

// ================================================================
//  Data Recording
// ================================================================

void AnacostiaIQ::recordDepth(double depth)
{
    if (depthHistory.count() > MAX_HISTORY)
        depthHistory.removeFirst();
    depthHistory.append({QDateTime::currentDateTime(), depth});
    depthChart->plotWeatherData(depthHistory, "Water Depth (cm)");
}

void AnacostiaIQ::recordMoisture(double moisture)
{
    if (moistureHistory.count() > MAX_HISTORY)
        moistureHistory.removeFirst();
    moistureHistory.append({QDateTime::currentDateTime(), moisture});
    moistureChart->plotWeatherData(moistureHistory, "Moisture Level (%)");
}
