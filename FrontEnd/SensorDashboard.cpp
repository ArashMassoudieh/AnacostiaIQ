#include "SensorDashboard.h"
#include <QLinearGradient>
#include <QGraphicsDropShadowEffect>
#include <QFont>
#include <QPixmap>
#include <QUrlQuery>
#include <QJsonValue>
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
    project            = config.project();
    refreshIntervalSec = config.refreshIntervalSec();

    // ── Seed the sensor list from config. If the config pinned an
    //    explicit list, that's authoritative and we won't let the API
    //    discovery overwrite it. If it didn't, we fall back to the old
    //    default list and let GET /sensors refine it.
    sensorIds = config.visibleSensorIds();
    if (sensorIds.isEmpty()) {
        sensorIds = QStringList()
            << "precip_amount"
            << "precip_prob"
            << "temperature"
            << "water_depth"
            << "valve_state"
            << "moisture_sensor";
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

    if (config.cameraEnabled()) {
        cameraRefreshTimer = new QTimer(this);
        cameraRefreshTimer->setInterval(30000);
        connect(cameraRefreshTimer, &QTimer::timeout,
                this, &SensorDashboard::fetchCameraState);
    }

    if (!config.healthStationId().isEmpty() && !config.healthComponents().isEmpty()) {
        healthRefreshTimer = new QTimer(this);
        healthRefreshTimer->setInterval(30000);
        connect(healthRefreshTimer, &QTimer::timeout,
                this, &SensorDashboard::fetchHealthState);
    }

    setupUI();

    if (config.cameraEnabled()) {
        fetchCameraState();
        cameraRefreshTimer->start();
    }
    if (healthRefreshTimer) {
        fetchHealthState();
        healthRefreshTimer->start();
    }

    // Create chart widgets in configured order before asynchronous network
    // replies arrive. Creating them from reply callbacks lets whichever
    // sensor responds first determine the visible order.
    for (const QString &id : sensorIds)
        getOrCreateChart(id);

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
#ifndef Q_OS_WASM
    // Desktop builds start at a practical window size. The browser build is
    // sized from the live viewport in main.cpp instead; keeping this resize
    // there leaves the central widget stuck at 1200 px after maximization.
    resize(1200, 850);
#endif

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
        QComboBox {
            background-color: #2b3038;
            color: #e0e0e0;
            border: 1px solid #3a3f47;
            border-radius: 6px;
            padding: 6px 12px;
            min-width: 120px;
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
    QGridLayout *controlRow = new QGridLayout(controlGroup);
    controlRow->setHorizontalSpacing(10);
    controlRow->setVerticalSpacing(8);

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

    controlRow->addWidget(startLabel, 0, 0);
    controlRow->addWidget(startDateTimeEdit, 0, 1);
    controlRow->addWidget(endLabel, 0, 2);
    controlRow->addWidget(endDateTimeEdit, 0, 3);
    controlRow->addWidget(fetchButton, 1, 0, 1, 2);
    controlRow->addWidget(autoRefreshCheckBox, 1, 2);
    controlRow->addWidget(countdownLabel, 1, 3);
    controlRow->setColumnStretch(1, 1);
    controlRow->setColumnStretch(3, 1);

    if (config.cameraEnabled()) {
        cameraGroup = new QGroupBox(config.cameraTitle(), this);
        QVBoxLayout *cameraLayout = new QVBoxLayout(cameraGroup);
        QHBoxLayout *scheduleControls = new QHBoxLayout();
        QHBoxLayout *cameraActions = new QHBoxLayout();
        cameraImage = new QLabel("Waiting for the first camera image…", this);
        cameraImage->setAlignment(Qt::AlignCenter);
        cameraImage->setMinimumHeight(260);
        cameraImage->setMaximumHeight(460);
        cameraImage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        cameraImage->setStyleSheet(
            "background:#15181d; border:1px solid #343a42; border-radius:8px;");
        cameraStatus = new QLabel("Loading camera status…", this);
        cameraSchedule = new QComboBox(this);
        cameraSchedule->addItem("Off", 0);
        cameraSchedule->addItem("Every 1 hour", 1);
        cameraSchedule->addItem("Every 6 hours", 6);
        cameraSchedule->addItem("Every 12 hours", 12);
        cameraSchedule->addItem("Every 24 hours", 24);
        cameraScheduleButton = new QPushButton("Save schedule", this);
        cameraCaptureButton = new QPushButton("Capture now", this);
        scheduleControls->addWidget(new QLabel("Automatic:", this));
        scheduleControls->addWidget(cameraSchedule, 1);
        scheduleControls->addWidget(cameraScheduleButton);
        cameraActions->addWidget(cameraCaptureButton);
        cameraActions->addWidget(cameraStatus, 1);
        cameraLayout->addLayout(scheduleControls);
        cameraLayout->addLayout(cameraActions);
        cameraLayout->addWidget(cameraImage, 1);
        connect(cameraCaptureButton, &QPushButton::clicked,
                this, &SensorDashboard::requestCameraCapture);
        connect(cameraScheduleButton, &QPushButton::clicked,
                this, &SensorDashboard::saveCameraSchedule);
    }

    // === Charts Area ===
    chartsContainer = new QWidget();
    chartsContainer->setStyleSheet("background-color: transparent;");
    chartsLayout = new QVBoxLayout(chartsContainer);
    chartsLayout->setContentsMargins(0, 0, 0, 0);

    // Layout mode is now a runtime config flag (scrollable_charts)
    // rather than a compile-time #ifdef.
    QVBoxLayout *leftLayout = new QVBoxLayout();
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(10);
    leftLayout->addWidget(controlGroup);

    if (config.scrollableCharts()) {
        chartsLayout->setSpacing(10);
        scrollArea = new QScrollArea(this);
        scrollArea->setWidgetResizable(true);
        scrollArea->setFrameShape(QFrame::NoFrame);
        scrollArea->setWidget(chartsContainer);

        leftLayout->addWidget(scrollArea, 1);
    } else {
        chartsLayout->setSpacing(4);
        leftLayout->addWidget(chartsContainer, 1);
    }

    leftPane = new QWidget(this);
    leftPane->setLayout(leftLayout);

    QVBoxLayout *rightLayout = new QVBoxLayout();
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(10);
    if (cameraGroup)
        rightLayout->addWidget(cameraGroup, 3);

    if (!config.healthStationId().isEmpty() && !config.healthComponents().isEmpty()) {
        healthGroup = new QGroupBox(config.healthStationName() + " Health", this);
        QVBoxLayout *healthLayout = new QVBoxLayout(healthGroup);
        const auto makeHealthRow = [this, healthLayout](const QString &name) {
            QLabel *label = new QLabel(name + ":  Checking…", this);
            label->setStyleSheet(
                "background:#191d22; border:1px solid #303640; border-radius:6px;"
                "padding:8px 10px; color:#b0bec5; font-weight:600;");
            healthLayout->addWidget(label);
            return label;
        };
        healthOverall = makeHealthRow("Overall");
        healthSensors = makeHealthRow("Sensors");
        healthApplication = makeHealthRow("Application");
        healthCloud = makeHealthRow("Cloud / API");
        healthQueue = makeHealthRow("Upload queue");
        healthUpdated = new QLabel("Loading current station health…", this);
        healthUpdated->setStyleSheet("color:#607d8b; font-size:11px;");
        healthLayout->addWidget(healthUpdated);
        rightLayout->addWidget(healthGroup, 2);
    }
    rightLayout->addStretch();
    rightPane = new QWidget(this);
    rightPane->setLayout(rightLayout);
    rightPane->setMinimumWidth(360);

    contentSplitter = new QSplitter(Qt::Horizontal, this);
    contentSplitter->setChildrenCollapsible(false);
    contentSplitter->setHandleWidth(8);
    contentSplitter->addWidget(leftPane);
    contentSplitter->addWidget(rightPane);
    contentSplitter->setStretchFactor(0, 7);
    contentSplitter->setStretchFactor(1, 4);
    contentSplitter->setSizes({760, 440});
    mainLayout->addWidget(contentSplitter, 1);

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

void SensorDashboard::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (contentSplitter)
        contentSplitter->setOrientation(event->size().width() < 900
                                        ? Qt::Vertical : Qt::Horizontal);
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
//  Network — optional lab camera
// ================================================================

void SensorDashboard::fetchCameraState()
{
    if (!config.cameraEnabled())
        return;
    QUrl url(apiUrl + "/camera/" + config.cameraStationId() + "/state");
    QNetworkReply *reply = networkManager->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isObject())
                updateCameraState(doc.object());
        } else if (cameraStatus) {
            cameraStatus->setText("Camera server unavailable");
        }
        reply->deleteLater();
    });
}

void SensorDashboard::requestCameraCapture()
{
    if (!config.cameraEnabled())
        return;
    cameraCaptureButton->setEnabled(false);
    cameraStatus->setText("Capture requested…");
    QUrl url(apiUrl + "/camera/" + config.cameraStationId() + "/request");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = networkManager->post(request, QByteArray("{}"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        cameraCaptureButton->setEnabled(true);
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (reply->error() == QNetworkReply::NoError && doc.isObject()) {
            updateCameraState(doc.object());
            cameraStatus->setText("Capture queued — waiting for the Pi…");
            QTimer::singleShot(3000, this, &SensorDashboard::fetchCameraState);
        } else {
            cameraStatus->setText("Could not request capture");
        }
        reply->deleteLater();
    });
}

void SensorDashboard::saveCameraSchedule()
{
    if (!config.cameraEnabled())
        return;
    const int hours = cameraSchedule->currentData().toInt();
    cameraScheduleButton->setEnabled(false);
    QUrl url(apiUrl + "/camera/" + config.cameraStationId() + "/schedule");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    const QByteArray payload = QJsonDocument(
        QJsonObject{{"hours", hours}}).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = networkManager->sendCustomRequest(
        request, QByteArray("PUT"), payload);
    connect(reply, &QNetworkReply::finished, this, [this, reply, hours]() {
        cameraScheduleButton->setEnabled(true);
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (reply->error() == QNetworkReply::NoError && doc.isObject()) {
            updateCameraState(doc.object());
            cameraStatus->setText(hours == 0 ? "Automatic capture is off"
                                             : QString("Capturing every %1 h").arg(hours));
        } else {
            cameraStatus->setText("Could not save camera schedule");
        }
        reply->deleteLater();
    });
}

void SensorDashboard::updateCameraState(const QJsonObject &state)
{
    const int hours = state.value("schedule_hours").toInt(6);
    const int scheduleIndex = cameraSchedule->findData(hours);
    if (scheduleIndex >= 0)
        cameraSchedule->setCurrentIndex(scheduleIndex);

    const QJsonObject requestState = state.value("request").toObject();
    const QJsonObject latest = state.value("latest").toObject();
    const bool capturePending = requestState.value("status").toString() == "pending";
    if (capturePending)
        QTimer::singleShot(3000, this, &SensorDashboard::fetchCameraState);
    if (latest.isEmpty()) {
        cameraStatus->setText(capturePending
                              ? "Capture queued — waiting for the Pi…"
                              : "No camera image uploaded yet");
        return;
    }

    const QString capturedAt = latest.value("captured_at").toString();
    const QDateTime captured = QDateTime::fromString(capturedAt, Qt::ISODate);
    QString status = captured.isValid()
        ? QString("Latest: %1").arg(captured.toLocalTime().toString("MMM d, h:mm:ss AP"))
        : QString("Latest image available");
    if (capturePending)
        status += " · new capture queued";
    cameraStatus->setText(status);

    const QString captureId = latest.value("capture_id").toString();
    if (!captureId.isEmpty() && captureId == cameraCaptureId)
        return;
    cameraCaptureId = captureId;

    QUrl imageUrl(latest.value("image_url").toString());
    if (imageUrl.isRelative()) {
        QUrl publicBase(apiUrl);
        publicBase.setPort(-1);
        publicBase.setPath("/");
        publicBase.setQuery(QString());
        imageUrl = publicBase.resolved(imageUrl);
    }
    QUrlQuery cacheBust(imageUrl);
    cacheBust.addQueryItem("capture", captureId);
    imageUrl.setQuery(cacheBust);
    fetchCameraImage(imageUrl);
}

void SensorDashboard::fetchCameraImage(const QUrl &url)
{
    QNetworkReply *reply = networkManager->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QPixmap pixmap;
            if (pixmap.loadFromData(reply->readAll(), "JPG")) {
                cameraImage->setPixmap(pixmap.scaled(
                    cameraImage->size(), Qt::KeepAspectRatio,
                    Qt::SmoothTransformation));
            }
        }
        reply->deleteLater();
    });
}

// ================================================================
//  Network — compact station health summary
// ================================================================

void SensorDashboard::fetchHealthState()
{
    const QString stationId = config.healthStationId();
    const QVector<HealthComponentDef> components = config.healthComponents();
    if (stationId.isEmpty() || components.isEmpty())
        return;

    healthStates.clear();
    updateHealthSummary();

    const QDateTime end = QDateTime::currentDateTime();
    const QDateTime start = end.addDays(-1);
    const QString timestampFormat = "yyyy-MM-dd'T'HH:mm:ss";

    for (const HealthComponentDef &component : components) {
        QUrl url(apiUrl + "/sensor/health_" + stationId + "_" + component.id);
        QUrlQuery query;
        query.addQueryItem("start", start.toString(timestampFormat));
        query.addQueryItem("end", end.toString(timestampFormat));
        if (!project.isEmpty())
            query.addQueryItem("project", project);
        url.setQuery(query);

        QNetworkReply *reply = networkManager->get(QNetworkRequest(url));
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, component]() {
            int state = -1;
            if (reply->error() == QNetworkReply::NoError) {
                const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
                QJsonArray readings;
                if (document.isArray())
                    readings = document.array();
                else if (document.isObject())
                    readings = document.object().value("readings").toArray();

                QDateTime newestTime;
                for (const QJsonValue &value : readings) {
                    const QJsonObject reading = value.toObject();
                    const QDateTime timestamp = QDateTime::fromString(
                        reading.value("timestamp").toString(), Qt::ISODate);
                    if (!timestamp.isValid() ||
                        (newestTime.isValid() && timestamp <= newestTime))
                        continue;
                    newestTime = timestamp;
                    const QJsonValue stateValue = reading.value("value");
                    state = stateValue.isString()
                        ? stateValue.toString().toInt()
                        : stateValue.toInt(-1);
                }
            }
            healthStates.insert(component.id, state);
            updateHealthSummary();
            reply->deleteLater();
        });
    }
}

void SensorDashboard::updateHealthSummary()
{
    if (!healthGroup)
        return;

    const auto stateName = [](int state) {
        switch (state) {
        case 0: return QString("Healthy");
        case 1: return QString("Degraded");
        case 2: return QString("Offline");
        case 3: return QString("Critical");
        default: return QString("Checking…");
        }
    };
    const auto stateColor = [](int state) {
        switch (state) {
        case 0: return QString("#6ee7a2");
        case 1: return QString("#ffc766");
        case 2: return QString("#ff8f8f");
        case 3: return QString("#ff7a97");
        default: return QString("#a9bac7");
        }
    };
    const auto setRow = [&stateName, &stateColor](QLabel *label,
                                                   const QString &name,
                                                   int state,
                                                   const QString &detail = QString()) {
        if (!label)
            return;
        const QString value = detail.isEmpty() ? stateName(state) : detail;
        label->setText(name + ":  " + value);
        label->setStyleSheet(QString(
            "background:#191d22; border:1px solid #303640; border-radius:6px;"
            "padding:8px 10px; color:%1; font-weight:700;").arg(stateColor(state)));
    };

    setRow(healthOverall, "Overall", healthStates.value("overall", -1));
    setRow(healthApplication, "Application", healthStates.value("application", -1));
    setRow(healthCloud, "Cloud / API", healthStates.value("cloud", -1));
    setRow(healthQueue, "Upload queue", healthStates.value("upload_queue", -1));

    int sensorCount = 0;
    int healthyCount = 0;
    int sensorWorst = -1;
    for (auto it = healthStates.cbegin(); it != healthStates.cend(); ++it) {
        if (!it.key().startsWith("sensor_"))
            continue;
        sensorCount++;
        if (it.value() == 0)
            healthyCount++;
        if (it.value() > sensorWorst)
            sensorWorst = it.value();
    }
    const QString sensorDetail = sensorCount == 0
        ? QString("Checking…")
        : QString("%1 of %2 healthy").arg(healthyCount).arg(sensorCount);
    setRow(healthSensors, "Sensors", sensorWorst, sensorDetail);

    if (healthUpdated) {
        const int received = healthStates.size();
        const int expected = config.healthComponents().size();
        healthUpdated->setText(received < expected
            ? QString("Refreshing health… %1/%2").arg(received).arg(expected)
            : QString("Updated %1 · full details on Health page")
                  .arg(QDateTime::currentDateTime().toString("h:mm:ss AP")));
    }
}

// ================================================================
//  Network — sensor list
// ================================================================

void SensorDashboard::fetchSensorList()
{
    QUrl url(apiUrl + "/sensors");
    if (!project.isEmpty()) {
        QUrlQuery listQuery;
        listQuery.addQueryItem("project", project);
        url.setQuery(listQuery);
    }
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

    const QDateTime displayStart = startDateTimeEdit->dateTime();
    const QDateTime displayEnd   = endDateTimeEdit->dateTime();

    // Reuse a complete range already held in this page session. This makes a
    // repeated Fetch Data click instant and avoids another API transfer.
    const auto cacheIt = sensorDataCache.constFind(sensorId);
    if (cacheIt != sensorDataCache.cend() && cacheIt->initialized &&
        displayStart >= cacheIt->coveredStart &&
        displayEnd <= cacheIt->coveredEnd) {
        qDebug() << "Sensor cache hit:" << sensorId
                 << displayStart << displayEnd;
        updateChart(sensorId, readingsInRange(sensorId, displayStart, displayEnd));
        completeSensorRequest();
        return;
    }

    QDateTime fetchStart = displayStart;
    const QDateTime fetchEnd = displayEnd;
    bool replaceCache = true;

    // Auto-refresh normally keeps the same start and advances only the end.
    // Fetch just that new tail and merge it into the existing readings.
    if (cacheIt != sensorDataCache.cend() && cacheIt->initialized &&
        displayStart >= cacheIt->coveredStart &&
        displayEnd > cacheIt->coveredEnd) {
        fetchStart = cacheIt->coveredEnd;
        replaceCache = false;
        qDebug() << "Incremental sensor fetch:" << sensorId
                 << fetchStart << fetchEnd;
    } else {
        qDebug() << "Full sensor fetch:" << sensorId
                 << fetchStart << fetchEnd;
    }

    // The deployed API stores its DynamoDB sort-key timestamps as local,
    // timezone-free ISO strings (for example 2026-09-30T15:00:03). Sending
    // these controls through toUTC() shifts a selected Eastern-time range by
    // four or five hours and can make a valid historical window look empty.
    // Preserve the wall-clock values shown in the dashboard and match the
    // server's stored timestamp format exactly.
    const QString apiTimestampFormat = "yyyy-MM-dd'T'HH:mm:ss";
    query.addQueryItem("start", fetchStart.toString(apiTimestampFormat));
    query.addQueryItem("end",   fetchEnd.toString(apiTimestampFormat));
    // Without this the API serves legacy (unnamespaced) rows, so a project
    // dashboard would silently chart another project's data.
    if (!project.isEmpty())
        query.addQueryItem("project", project);
    url.setQuery(query);

    QNetworkRequest request(url);

    QNetworkReply *reply = networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, sensorId, reply, displayStart, displayEnd,
             fetchStart, fetchEnd, replaceCache]() {
        onDataReceived(sensorId, reply, displayStart, displayEnd,
                       fetchStart, fetchEnd, replaceCache);
    });
}

void SensorDashboard::onDataReceived(const QString &sensorId,
                                     QNetworkReply *reply,
                                     const QDateTime &displayStart,
                                     const QDateTime &displayEnd,
                                     const QDateTime &fetchStart,
                                     const QDateTime &fetchEnd,
                                     bool replaceCache)
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

        SensorDataCache &cache = sensorDataCache[sensorId];
        QMap<QString, QJsonObject> readingsByTimestamp;

        if (!replaceCache && cache.initialized) {
            for (const QJsonValue &value : cache.readings) {
                const QJsonObject reading = value.toObject();
                const QString timestamp = reading.value("timestamp").toString();
                if (!timestamp.isEmpty())
                    readingsByTimestamp.insert(timestamp, reading);
            }
        }

        for (const QJsonValue &value : dataArray) {
            const QJsonObject reading = value.toObject();
            const QString timestamp = reading.value("timestamp").toString();
            if (!timestamp.isEmpty())
                readingsByTimestamp.insert(timestamp, reading);
        }

        cache.readings = QJsonArray();
        for (auto it = readingsByTimestamp.cbegin();
             it != readingsByTimestamp.cend(); ++it)
            cache.readings.append(it.value());

        if (replaceCache || !cache.initialized) {
            cache.coveredStart = fetchStart;
            cache.coveredEnd = fetchEnd;
        } else {
            if (fetchStart < cache.coveredStart)
                cache.coveredStart = fetchStart;
            if (fetchEnd > cache.coveredEnd)
                cache.coveredEnd = fetchEnd;
        }
        cache.initialized = true;

        updateChart(sensorId, readingsInRange(sensorId, displayStart, displayEnd));
    } else {
        qDebug() << "Error fetching" << sensorId << ":" << reply->errorString();
        if (sensorDataCache.value(sensorId).initialized)
            updateChart(sensorId, readingsInRange(sensorId, displayStart, displayEnd));
        else
            updateChart(sensorId, QJsonArray());
    }

    reply->deleteLater();

    completeSensorRequest();
}

QJsonArray SensorDashboard::readingsInRange(const QString &sensorId,
                                            const QDateTime &start,
                                            const QDateTime &end) const
{
    QJsonArray filtered;
    const SensorDataCache cache = sensorDataCache.value(sensorId);
    for (const QJsonValue &value : cache.readings) {
        const QDateTime timestamp = QDateTime::fromString(
            value.toObject().value("timestamp").toString(), Qt::ISODate);
        if (timestamp.isValid() && timestamp >= start && timestamp <= end)
            filtered.append(value);
    }
    return filtered;
}

void SensorDashboard::completeSensorRequest()
{
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
        titleFont.setPixelSize(18);
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
        sc.axisX->setLabelsColor(QColor("#b0bec5"));
        QFont axisFont;
        axisFont.setPixelSize(13);
        sc.axisX->setLabelsFont(axisFont);
        sc.chart->addAxis(sc.axisX, Qt::AlignBottom);
        sc.area->attachAxis(sc.axisX);

        // ── Y axis (value) ─────────────────────────────────────
        sc.axisY = new QValueAxis();
        sc.axisY->setGridLineVisible(true);
        sc.axisY->setGridLineColor(QColor("#2d3139"));
        sc.axisY->setLinePenColor(QColor("#3a3f47"));
        sc.axisY->setLabelsColor(QColor("#b0bec5"));
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
            sc.chartView->setMinimumHeight(320);
            sc.chartView->setMaximumHeight(400);
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
    QDateTime minTime, maxTime, latestTime;
    double latestValue = 0.0;

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
        if (!latestTime.isValid() || ts > latestTime) {
            latestTime = ts;
            latestValue = v;
        }
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

    // Put the newest value in large, high-contrast title text. Axis labels
    // remain useful for trends, but users should not have to infer the live
    // reading from a compressed line chart.
    QString uLabel = unit.isEmpty() ? unitLabel(sensorId) : unit;
    double absLatest = qAbs(latestValue);
    int decimals = absLatest >= 100.0 ? 0 : (absLatest >= 10.0 ? 1 : 2);
    QString latestText = QString::number(latestValue, 'f', decimals);
    sc.chart->setTitle(QString("%1  —  Latest: %2 %3  ·  %4 readings")
                           .arg(friendlyName(sensorId))
                           .arg(latestText)
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
    sc.axisY->setLabelFormat(qAbs(yMax + pad) >= 100.0 ? "%.0f" : "%.1f");

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
