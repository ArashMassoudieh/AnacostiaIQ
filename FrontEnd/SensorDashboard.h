#ifndef SENSORDASHBOARD_H
#define SENSORDASHBOARD_H

#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QDateTimeEdit>
#include <QTimer>
#include <QStatusBar>
#include <QGroupBox>
#include <QScrollArea>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QMap>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QAreaSeries>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QValueAxis>
#include <QDateTime>
#include <QDebug>

#include "DashboardConfig.h"

struct SensorChart {
    QChart      *chart     = nullptr;
    QChartView  *chartView = nullptr;
    QLineSeries *series    = nullptr;
    QAreaSeries *area      = nullptr;
    QDateTimeAxis *axisX   = nullptr;
    QValueAxis    *axisY   = nullptr;
};

class SensorDashboard : public QMainWindow
{
    Q_OBJECT

public:
    // configPath: path to config.json (defaults to "config.json" next
    // to the executable / in the CWD).
    SensorDashboard(const QString &configPath = "config.json",
                    QWidget *parent = nullptr);
    ~SensorDashboard();

private slots:
    void onFetchClicked();
    void onAutoRefreshToggled(bool checked);
    void onAutoRefreshTimeout();
    void onSensorListReceived(QNetworkReply *reply);

private:
    void fetchConfig();
    void finishInitialization();
    void setupUI();
    void fetchSensorList();
    void fetchAllSensors();
    void fetchSensorData(const QString &sensorId);
    void onDataReceived(const QString &sensorId, QNetworkReply *reply);
    void updateChart(const QString &sensorId, const QJsonArray &dataArray);
    SensorChart &getOrCreateChart(const QString &sensorId);
    void setStatus(const QString &message);

    // Display helpers — now thin wrappers over DashboardConfig so the
    // chart-building code reads the same as before.
    QString friendlyName(const QString &sensorId);
    QString unitLabel(const QString &sensorId);
    QColor seriesColor(const QString &sensorId);
    QColor areaColor(const QString &sensorId);
    bool   floorAtZero(const QString &sensorId);

    // Configuration (loaded first; drives sensors + display + globals)
    DashboardConfig config;

    // UI
    QWidget *centralWidget;
    QVBoxLayout *mainLayout;

    // Controls
    QGroupBox *controlGroup;
    QLabel *startLabel;
    QDateTimeEdit *startDateTimeEdit;
    QLabel *endLabel;
    QDateTimeEdit *endDateTimeEdit;
    QPushButton *fetchButton;
    QCheckBox *autoRefreshCheckBox;
    QLabel *countdownLabel;

    // Charts area
    QScrollArea *scrollArea = nullptr;   // used only when scrollable
    QWidget *chartsContainer;
    QVBoxLayout *chartsLayout;

    // One chart per sensor
    QMap<QString, SensorChart> sensorCharts;

    // Sensor list (from config; optionally refreshed from the API)
    QStringList sensorIds;

    // Network
    QNetworkAccessManager *networkManager;
    QString apiUrl;
    int pendingRequests;

    // Auto-refresh
    QTimer *refreshTimer;
    QTimer *countdownTimer;
    int countdownSeconds;
    int refreshIntervalSec = 60;
};

#endif // SENSORDASHBOARD_H
