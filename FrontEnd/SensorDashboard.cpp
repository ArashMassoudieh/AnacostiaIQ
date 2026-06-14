#include "SensorDashboard.h"
#include <QLinearGradient>
#include <QGraphicsDropShadowEffect>
#include <QFont>
#include <limits>

SensorDashboard::SensorDashboard(const QString &configPath, QWidget *parent)
    : QMainWindow(parent)
{
    networkManager = new QNetworkAccessManager(this);
    pendingRequests = 0;

    // ── Load configuration first; it drives sensors + display + globals.
    // On desktop the file is read synchronously. In a WebAssembly build
    // there is no local filesystem, so the file read fails — in that case
    // we fetch config.json over HTTP (relative to the served page) and
    // finish initialising once it arrives.
    if (config.load(configPath)) {
        qDebug() << "Config loaded from" << configPath;
        finishInitialization();
    } else {
        qWarning() << "Local config not available (" << config.errorString()
        << ") — attempting HTTP fetch of config.json";
        fetchConfig();
    }
}

// Fetch config.json over HTTP. Used when no local file is readable
// (the WebAssembly case). The URL is relative to the document base, so
// config.json must sit next to SensorDashboard.html on the web server.
void SensorDashboard::fetchConfig()
{
    // Relative URL → resolved against the page the .wasm was loaded from.
    QUrl url("config.json");
    QNetworkRequest request(url);

    QNetworkReply *reply = networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            if (config.loadFromData(data))
                qDebug() << "Config loaded via HTTP fetch (config.json)";
            else
                qWarning() << "Fetched config.json invalid ("
                           << config.errorString()
                           << ") — using built-in defaults";
        } else {
            qWarning() << "Could not fetch config.json ("
                       << reply->errorString()
                       << ") — using built-in defaults";
        }
        reply->deleteLater();
        finishInitialization();
    });
}

// Everything that depends on the config being loaded. Called directly
// after a synchronous desktop load, or from the HTTP-fetch callback in
// the WebAssembly build. Either way, by the time this runs `config`
// holds whatever was loaded (or the built-in defaults if loading failed).
void SensorDashboard::finishInitialization()
{
    apiUrl             = config.apiUrl();
    refreshIntervalSec = config.refreshIntervalSec();

    // ── Seed the sensor list from config. If the config pinned an
    //    explicit list, that's authoritative and we won't let the API
    //    discovery overwrite it. If it didn't, we fall back to the old
    //    default list and let GET /sensors refine it.
    sensorIds = config.visibleSensorIds();
    if (sensorIds.isEmpty()) {
        sensorIds = {
            "precip_amount",
            "precip_prob",
            "temperature",
            "water_depth",
            "valve_state",
            "moisture_sensor"
        };
    }

    // Auto-refresh timer (interval from config)
    refreshTimer = new QTimer(this);
    refreshTimer->setInterval(refreshIntervalSec * 1000);
    connect(refreshTimer, &QTimer::timeout,
            this, &SensorDashboard::onAutoRefreshTimeout);

    // Countdown display timer
    countdownTimer = new QTimer(this);
    countdownTimer->setInterval(1000);
    connect(countdownTimer, &QTimer::timeout, this, [this]() {
        countdownSeconds--;
        if (countdownSeconds >= 0)
            countdownLabel->setText(QString("  %1s").arg(countdownSeconds));
    });
    countdownSeconds = refreshIntervalSec;

    setupUI();

    // Auto-refresh on by default if the config asked for it.
    if (config.autoRefreshDefault())
        autoRefreshCheckBox->setChecked(true);

    // Only ask the server for the sensor list when the config did NOT
    // pin one — otherwise honour the configured selection exactly.
    if (config.hasExplicitSensorList())
        fetchAllSensors();
    else
        fetchSensorList();
}

SensorDashboard::~SensorDashboard()
{
}

// ================================================================
//  UI
// ================================================================

void SensorDashboard::setupUI()
{
    setWindowTitle(config.windowTitle());
    resize(1200, 850);

    // ── Global stylesheet (modern, flat) ───────────────────────
    setStyleSheet(R"(
        QMainWindow {
            background-color: #1a1d23;
        }
        QGroupBox {
            font-weight: bold;
            font-size: 13px;
            color: #b0bec5;
            border: 1px solid #2d3139;
            border-radius: 8px;
            margin-top: 10px;
            padding: 14px 10px 8px 10px;
            background-color: #21252b;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 16px;
            padding: 0 8px;
        }
        QPushButton {
            background-color: #0d6efd;
            color: white;
            border: none;
            border-radius: 6px;
            padding: 7px 22px;
            font-weight: bold;
            font-size: 13px;
        }
        QPushButton:hover {
            background-color: #3d8bfd;
        }
        QPushButton:pressed {
            background-color: #0a58ca;
        }
        QDateTimeEdit {
            background-color: #2b3038;
            color: #e0e0e0;
            border: 1px solid #3a3f47;
            border-radius: 6px;
            padding: 5px 10px;
            font-size: 13px;
        }
        QDateTimeEdit::drop-down {
            border: none;
            width: 20px;
        }
        QLabel {
            color: #90a4ae;
            font-size: 13px;
        }
        QCheckBox {
            color: #90a4ae;
            font-size: 13px;
            spacing: 6px;
        }
        QCheckBox::indicator {
            width: 16px;
            height: 16px;
            border-radius: 3px;
            border: 1px solid #4a5060;
            background-color: #2b3038;
        }
        QCheckBox::indicator:checked {
            background-color: #0d6efd;
            border-color: #0d6efd;
        }
        QStatusBar {
            background-color: #181b20;
            color: #607d8b;
            font-size: 12px;
            border-top: 1px solid #2d3139;
        }
        QScrollArea {
            background-color: transparent;
            border: none;
        }
    )");

    centralWidget = new QWidget(this);
    mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(10);

    // === Control Panel ===
    controlGroup = new QGroupBox("Query Controls", this);
    QHBoxLayout *controlRow = new QHBoxLayout(controlGroup);
    controlRow->setSpacing(10);

    startLabel = new QLabel("From:", this);
    startDateTimeEdit = new QDateTimeEdit(this);
    startDateTimeEdit->setDisplayFormat("yyyy-MM-dd HH:mm");
    startDateTimeEdit->setCalendarPopup(true);
    startDateTimeEdit->setDateTime(
        QDateTime::currentDateTime().addDays(-config.defaultRangeDaysBack()));

    endLabel = new QLabel("To:", this);
    endDateTimeEdit = new QDateTimeEdit(this);
    endDateTimeEdit->setDisplayFormat("yyyy-MM-dd HH:mm");
    endDateTimeEdit->setCalendarPopup(true);
    endDateTimeEdit->setDateTime(
        QDateTime::currentDateTime().addDays(config.defaultRangeDaysAhead()));

    fetchButton = new QPushButton("Fetch Data", this);

    autoRefreshCheckBox = new QCheckBox(
        QString("Auto-refresh (%1s)").arg(refreshIntervalSec), this);

    countdownLabel = new QLabel("", this);
    countdownLabel->setStyleSheet("color: #546e7a; font-style: italic;");

    controlRow->addWidget(startLabel);
    controlRow->addWidget(startDateTimeEdit);
    controlRow->addWidget(endLabel);
    controlRow->addWidget(endDateTimeEdit);
    controlRow->addWidget(fetchButton);
    controlRow->addSpacing(24);
    controlRow->addWidget(autoRefreshCheckBox);
    controlRow->addWidget(countdownLabel);
    controlRow->addStretch();

    // === Charts Area ===
    chartsContainer = new QWidget();
    chartsContainer->setStyleSheet("background-color: transparent;");
    chartsLayout = new QVBoxLayout(chartsContainer);
    chartsLayout->setContentsMargins(0, 0, 0, 0);

    // Layout mode is now a runtime config flag (scrollable_charts)
    // rather than a compile-time #ifdef.
    if (config.scrollableCharts()) {
        chartsLayout->setSpacing(10);
        scrollArea = new QScrollArea(this);
        scrollArea->setWidgetResizable(true);
        scrollArea->setFrameShape(QFrame::NoFrame);
        scrollArea->setWidget(chartsContainer);

        mainLayout->addWidget(controlGroup);
        mainLayout->addWidget(scrollArea, 1);
    } else {
        chartsLayout->setSpacing(4);
        mainLayout->addWidget(controlGroup);
        mainLayout->addWidget(chartsContainer, 1);
    }

    setCentralWidget(centralWidget);
    statusBar()->showMessage(
        QString("Ready — default range: -%1 / +%2 days")
            .arg(config.defaultRangeDaysBack())
            .arg(config.defaultRangeDaysAhead()));

    // === Signals ===
    connect(fetchButton, &QPushButton::clicked,
            this, &SensorDashboard::onFetchClicked);
    connect(autoRefreshCheckBox, &QCheckBox::toggled,
            this, &SensorDashboard::onAutoRefreshToggled);
}

// ================================================================
//  Slots
// ================================================================

void SensorDashboard::onFetchClicked()
{
    fetchAllSensors();
}

void SensorDashboard::onAutoRefreshToggled(bool checked)
{
    if (checked) {
        countdownSeconds = refreshIntervalSec;
        countdownLabel->setText(QString("  %1s").arg(countdownSeconds));
        refreshTimer->start();
        countdownTimer->start();
    } else {
        refreshTimer->stop();
        countdownTimer->stop();
        countdownLabel->setText("");
    }
}

void SensorDashboard::onAutoRefreshTimeout()
{
    endDateTimeEdit->setDateTime(
        QDateTime::currentDateTime().addDays(config.defaultRangeDaysAhead()));
    fetchAllSensors();
    countdownSeconds = refreshIntervalSec;
}

// ================================================================
//  Network — sensor list
// ================================================================

void SensorDashboard::fetchSensorList()
{
    QUrl url(apiUrl + "/sensors");
    QNetworkRequest request(url);

    qDebug() << "Fetching sensor list from" << url.toString();

    QNetworkReply *reply = networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onSensorListReceived(reply);
    });
}

void SensorDashboard::onSensorListReceived(QNetworkReply *reply)
{
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);

        if (doc.isArray()) {
            QJsonArray arr = doc.array();
            QStringList newIds;
            for (const QJsonValue &v : arr)
                newIds.append(v.toString());
            if (!newIds.isEmpty()) {
                sensorIds = newIds;
                qDebug() << "Loaded sensors from API:" << sensorIds;
            }
        }
    } else {
        qDebug() << "Could not fetch sensor list, using defaults:"
                 << reply->errorString();
    }

    reply->deleteLater();
    fetchAllSensors();
}

// ================================================================
//  Network — sensor data
// ================================================================

void SensorDashboard::fetchAllSensors()
{
    pendingRequests = sensorIds.size();
    setStatus(QString("Fetching data for %1 sensors...").arg(pendingRequests));

    for (const QString &id : sensorIds)
        fetchSensorData(id);
}

void SensorDashboard::fetchSensorData(const QString &sensorId)
{
    QUrl url(apiUrl + "/sensor/" + sensorId);
    QUrlQuery query;

    QDateTime startDt = startDateTimeEdit->dateTime();
    QDateTime endDt   = endDateTimeEdit->dateTime();

    query.addQueryItem("start", startDt.toUTC().toString(Qt::ISODate));
    query.addQueryItem("end",   endDt.toUTC().toString(Qt::ISODate));
    url.setQuery(query);

    QNetworkRequest request(url);

    QNetworkReply *reply = networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, sensorId, reply]() {
        onDataReceived(sensorId, reply);
    });
}

void SensorDashboard::onDataReceived(const QString &sensorId,
                                     QNetworkReply *reply)
{
    if (!reply) return;

    if (reply->error() == QNetworkReply::NoError) {
        QByteArray responseData = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(responseData);

        QJsonArray dataArray;
        if (doc.isArray()) {
            dataArray = doc.array();
        } else if (doc.isObject()) {
            QJsonObject obj = doc.object();
            if (obj.contains("readings"))
                dataArray = obj["readings"].toArray();
        }

        updateChart(sensorId, dataArray);
    } else {
        qDebug() << "Error fetching" << sensorId << ":" << reply->errorString();
        QJsonArray empty;
        updateChart(sensorId, empty);
    }

    reply->deleteLater();

    pendingRequests--;
    if (pendingRequests <= 0) {
        setStatus(QString("All sensors loaded — %1")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
    }
}

// ================================================================
//  Chart management
// ================================================================

SensorChart &SensorDashboard::getOrCreateChart(const QString &sensorId)
{
    if (!sensorCharts.contains(sensorId)) {
        SensorChart sc;

        // ── Chart ──────────────────────────────────────────────
        sc.chart = new QChart();
        sc.chart->setAnimationOptions(QChart::SeriesAnimations);
        sc.chart->setBackgroundBrush(QBrush(QColor("#21252b")));
        sc.chart->setBackgroundRoundness(10);
        sc.chart->setMargins(QMargins(12, 8, 12, 4));
        sc.chart->legend()->hide();

        // Title
        QFont titleFont;
        titleFont.setPixelSize(14);
        titleFont.setBold(true);
        sc.chart->setTitleFont(titleFont);
        sc.chart->setTitleBrush(QBrush(QColor("#cfd8dc")));
        sc.chart->setTitle(friendlyName(sensorId));

        // ── Line series ────────────────────────────────────────
        sc.series = new QLineSeries();
        sc.series->setName(sensorId);
        QPen linePen(seriesColor(sensorId));
        linePen.setWidth(2);
        sc.series->setPen(linePen);

        // ── Area fill under curve ──────────────────────────────
        QLineSeries *lower = new QLineSeries();   // stays at 0
        sc.area = new QAreaSeries(sc.series, lower);
        sc.area->setName(sensorId);

        QColor fill = areaColor(sensorId);
        sc.area->setBrush(QBrush(fill));
        sc.area->setPen(linePen);           // top edge = line pen
        QPen noPen(Qt::NoPen);
        sc.area->setBorderColor(Qt::transparent);

        sc.chart->addSeries(sc.area);

        // ── X axis (time) ──────────────────────────────────────
        sc.axisX = new QDateTimeAxis();
        sc.axisX->setFormat("M/dd HH:mm");
        sc.axisX->setLabelsAngle(0);
        sc.axisX->setTickCount(5);
        sc.axisX->setGridLineVisible(true);
        sc.axisX->setGridLineColor(QColor("#2d3139"));
        sc.axisX->setLinePenColor(QColor("#3a3f47"));
        sc.axisX->setLabelsColor(QColor("#78909c"));
        QFont axisFont;
        axisFont.setPixelSize(10);
        sc.axisX->setLabelsFont(axisFont);
        sc.chart->addAxis(sc.axisX, Qt::AlignBottom);
        sc.area->attachAxis(sc.axisX);

        // ── Y axis (value) ─────────────────────────────────────
        sc.axisY = new QValueAxis();
        sc.axisY->setGridLineVisible(true);
        sc.axisY->setGridLineColor(QColor("#2d3139"));
        sc.axisY->setLinePenColor(QColor("#3a3f47"));
        sc.axisY->setLabelsColor(QColor("#78909c"));
        sc.axisY->setLabelsFont(axisFont);
        sc.axisY->setTickCount(5);
        sc.chart->addAxis(sc.axisY, Qt::AlignLeft);
        sc.area->attachAxis(sc.axisY);

        // ── Chart view ─────────────────────────────────────────
        sc.chartView = new QChartView(sc.chart);
        sc.chartView->setRenderHint(QPainter::Antialiasing);
        sc.chartView->setStyleSheet(
            "background-color: #21252b; border-radius: 10px;");

        if (config.scrollableCharts()) {
            sc.chartView->setMinimumHeight(280);
            sc.chartView->setMaximumHeight(360);
        } else {
            sc.chartView->setSizePolicy(
                QSizePolicy::Expanding, QSizePolicy::Expanding);
        }

        // Insert in configured order
        int insertPos = chartsLayout->count();
        for (int i = 0; i < sensorIds.size(); ++i) {
            if (sensorIds[i] == sensorId) {
                insertPos = i;
                break;
            }
        }
        if (insertPos > chartsLayout->count())
            insertPos = chartsLayout->count();
        chartsLayout->insertWidget(insertPos, sc.chartView, 1);

        sensorCharts[sensorId] = sc;
    }

    return sensorCharts[sensorId];
}

void SensorDashboard::updateChart(const QString &sensorId,
                                  const QJsonArray &dataArray)
{
    SensorChart &sc = getOrCreateChart(sensorId);
    sc.series->clear();

    // Also clear the lower bound series of the area
    if (sc.area && sc.area->lowerSeries())
        sc.area->lowerSeries()->clear();

    if (dataArray.isEmpty()) {
        sc.chart->setTitle(friendlyName(sensorId) + "  (no data)");
        return;
    }

    QString unit;
    double minVal =  std::numeric_limits<double>::max();
    double maxVal =  std::numeric_limits<double>::lowest();
    QDateTime minTime, maxTime;

    for (const QJsonValue &val : dataArray) {
        if (!val.isObject()) continue;
        QJsonObject reading = val.toObject();

        QString tsStr = reading["timestamp"].toString();
        QDateTime ts = QDateTime::fromString(tsStr, Qt::ISODate);
        if (!ts.isValid()) continue;

        double v = 0.0;
        QJsonValue vField = reading["value"];
        if (vField.isString())
            v = vField.toString().toDouble();
        else if (vField.isDouble())
            v = vField.toDouble();
        else
            continue;

        if (unit.isEmpty() && reading.contains("unit"))
            unit = reading["unit"].toString();

        sc.series->append(ts.toMSecsSinceEpoch(), v);

        if (v < minVal) minVal = v;
        if (v > maxVal) maxVal = v;
        if (!minTime.isValid() || ts < minTime) minTime = ts;
        if (!maxTime.isValid() || ts > maxTime) maxTime = ts;
    }

    // Fill the lower bound series so the area renders properly
    if (sc.area && sc.area->lowerSeries() && sc.series->count() > 0) {
        QLineSeries *lower = qobject_cast<QLineSeries *>(sc.area->lowerSeries());
        if (lower) {
            lower->clear();
            double floor = floorAtZero(sensorId) ? 0.0 : minVal;
            for (const QPointF &pt : sc.series->points())
                lower->append(pt.x(), floor);
        }
    }

    if (sc.series->count() == 0) {
        sc.chart->setTitle(friendlyName(sensorId) + "  (no valid data)");
        return;
    }

    // Title (include unit)
    QString uLabel = unit.isEmpty() ? unitLabel(sensorId) : unit;
    sc.chart->setTitle(QString("%1 (%2)  —  %3 readings")
                           .arg(friendlyName(sensorId))
                           .arg(uLabel)
                           .arg(sc.series->count()));

    // Y axis range
    double yMin = minVal;
    double yMax = maxVal;

    // Floor at zero for configured sensors
    if (floorAtZero(sensorId))
        yMin = 0.0;

    double range = yMax - yMin;
    double pad   = range * 0.1;
    if (range == 0) pad = (yMax != 0) ? qAbs(yMax) * 0.1 : 1.0;

    // Don't go below zero for floored sensors
    double lowerBound = floorAtZero(sensorId) ? 0.0 : (yMin - pad);
    sc.axisY->setRange(lowerBound, yMax + pad);

    // X axis
    if (minTime.isValid() && maxTime.isValid())
        sc.axisX->setRange(minTime, maxTime);
}

// ================================================================
//  Helpers — now delegate to DashboardConfig
// ================================================================

void SensorDashboard::setStatus(const QString &message)
{
    statusBar()->showMessage(message);
}

QString SensorDashboard::friendlyName(const QString &sensorId)
{
    return config.displayName(sensorId);
}

QString SensorDashboard::unitLabel(const QString &sensorId)
{
    QString u = config.unit(sensorId);
    return u.isEmpty() ? QStringLiteral("Value") : u;
}

QColor SensorDashboard::seriesColor(const QString &sensorId)
{
    return config.lineColor(sensorId);
}

QColor SensorDashboard::areaColor(const QString &sensorId)
{
    return config.areaColor(sensorId);
}

bool SensorDashboard::floorAtZero(const QString &sensorId)
{
    return config.floorAtZero(sensorId);
}
