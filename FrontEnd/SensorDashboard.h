#ifndef SENSORDASHBOARD_H
#define SENSORDASHBOARD_H

#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
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

QT_CHARTS_USE_NAMESPACE
#include <QDateTime>
#include "DashboardConfig.h"

struct SensorChart {
    QChart *chart=nullptr;
    QChartView *chartView=nullptr;
    QLineSeries *series=nullptr;
    QAreaSeries *area=nullptr;
    QDateTimeAxis *axisX=nullptr;
    QValueAxis *axisY=nullptr;
};

class SensorDashboard : public QMainWindow
{
    Q_OBJECT
public:
    SensorDashboard(const QString &configPath="config.json", QWidget *parent=nullptr);
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
    void evaluateDerivedSeries();
    QJsonArray evaluateDerived(const SensorDef &def, QString *error=nullptr) const;
    void updateChart(const QString &sensorId, const QJsonArray &dataArray);
    SensorChart &getOrCreateChart(const QString &sensorId);
    void setStatus(const QString &message);

    QString friendlyName(const QString &sensorId);
    QString unitLabel(const QString &sensorId);
    QColor seriesColor(const QString &sensorId);
    QColor areaColor(const QString &sensorId);
    bool floorAtZero(const QString &sensorId);

    DashboardConfig config;
    QWidget *centralWidget;
    QVBoxLayout *mainLayout;
    QGroupBox *controlGroup;
    QLabel *startLabel;
    QDateTimeEdit *startDateTimeEdit;
    QLabel *endLabel;
    QDateTimeEdit *endDateTimeEdit;
    QPushButton *fetchButton;
    QCheckBox *autoRefreshCheckBox;
    QLabel *countdownLabel;
    QScrollArea *scrollArea=nullptr;
    QWidget *chartsContainer;
    QVBoxLayout *chartsLayout;
    QMap<QString, SensorChart> sensorCharts;

    // sensorIds is display order (physical + derived). fetchSensorIds is the
    // backend request set. rawSeries retains physical/derived samples so
    // expressions can be evaluated after all network requests complete.
    QStringList sensorIds;
    QStringList fetchSensorIds;
    QMap<QString,QJsonArray> rawSeries;

    QNetworkAccessManager *networkManager;
    QString apiUrl;
    int pendingRequests;
    QTimer *refreshTimer;
    QTimer *countdownTimer;
    int countdownSeconds;
    int refreshIntervalSec=60;
};

#endif // SENSORDASHBOARD_H
