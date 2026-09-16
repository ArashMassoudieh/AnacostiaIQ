#include "SensorDashboard.h"
#include "ExpressionEvaluator.h"
#include <QFont>
#include <QPainter>
#include <QPen>
#include <QSet>
#include <limits>
#include <cmath>

SensorDashboard::SensorDashboard(const QString &configPath, QWidget *parent)
    : QMainWindow(parent)
{
    networkManager = new QNetworkAccessManager(this);
    pendingRequests = 0;
    if (config.load(configPath)) finishInitialization();
    else fetchConfig();
}

SensorDashboard::~SensorDashboard() {}

void SensorDashboard::fetchConfig()
{
    QNetworkReply *reply = networkManager->get(QNetworkRequest(QUrl("config.json")));
    connect(reply,&QNetworkReply::finished,this,[this,reply](){
        if(reply->error()==QNetworkReply::NoError) {
            if(!config.loadFromData(reply->readAll())) qWarning()<<config.errorString();
        } else qWarning()<<"Could not fetch config.json:" << reply->errorString();
        reply->deleteLater();
        finishInitialization();
    });
}

void SensorDashboard::finishInitialization()
{
    apiUrl=config.apiUrl();
    refreshIntervalSec=config.refreshIntervalSec();
    sensorIds=config.visibleSensorIds();
    fetchSensorIds=config.fetchableSensorIds();

    if(sensorIds.isEmpty()) {
        sensorIds=QStringList()<<"precip_amount"<<"precip_prob"<<"temperature"<<"water_depth"<<"valve_state"<<"moisture_sensor";
        fetchSensorIds=sensorIds;
    }

    refreshTimer=new QTimer(this);
    refreshTimer->setInterval(refreshIntervalSec*1000);
    connect(refreshTimer,&QTimer::timeout,this,&SensorDashboard::onAutoRefreshTimeout);
    countdownTimer=new QTimer(this);
    countdownTimer->setInterval(1000);
    connect(countdownTimer,&QTimer::timeout,this,[this](){ if(--countdownSeconds>=0) countdownLabel->setText(QString("  %1s").arg(countdownSeconds)); });
    countdownSeconds=refreshIntervalSec;

    setupUI();
    if(config.autoRefreshDefault()) autoRefreshCheckBox->setChecked(true);
    if(config.hasExplicitSensorList()) fetchAllSensors(); else fetchSensorList();
}

void SensorDashboard::setupUI()
{
    setWindowTitle(config.windowTitle());
    resize(1200,850);
    setStyleSheet(R"(
QMainWindow{background-color:#1a1d23} QGroupBox{font-weight:bold;font-size:13px;color:#b0bec5;border:1px solid #2d3139;border-radius:8px;margin-top:10px;padding:14px 10px 8px 10px;background-color:#21252b}
QGroupBox::title{subcontrol-origin:margin;left:16px;padding:0 8px} QPushButton{background-color:#0d6efd;color:white;border:none;border-radius:6px;padding:7px 22px;font-weight:bold;font-size:13px} QPushButton:hover{background-color:#3d8bfd}
QDateTimeEdit{background-color:#2b3038;color:#e0e0e0;border:1px solid #3a3f47;border-radius:6px;padding:5px 10px;font-size:13px} QLabel,QCheckBox{color:#90a4ae;font-size:13px} QStatusBar{background-color:#181b20;color:#607d8b;font-size:12px;border-top:1px solid #2d3139} QScrollArea{background-color:transparent;border:none}
)");

    centralWidget=new QWidget(this);
    mainLayout=new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(16,16,16,16); mainLayout->setSpacing(10);
    controlGroup=new QGroupBox("Query Controls",this);
    QHBoxLayout *row=new QHBoxLayout(controlGroup);
    startLabel=new QLabel("From:",this); startDateTimeEdit=new QDateTimeEdit(this);
    startDateTimeEdit->setDisplayFormat("yyyy-MM-dd HH:mm"); startDateTimeEdit->setCalendarPopup(true);
    startDateTimeEdit->setDateTime(QDateTime::currentDateTime().addDays(-config.defaultRangeDaysBack()));
    endLabel=new QLabel("To:",this); endDateTimeEdit=new QDateTimeEdit(this);
    endDateTimeEdit->setDisplayFormat("yyyy-MM-dd HH:mm"); endDateTimeEdit->setCalendarPopup(true);
    endDateTimeEdit->setDateTime(QDateTime::currentDateTime().addDays(config.defaultRangeDaysAhead()));
    fetchButton=new QPushButton("Fetch Data",this);
    autoRefreshCheckBox=new QCheckBox(QString("Auto-refresh (%1s)").arg(refreshIntervalSec),this);
    countdownLabel=new QLabel("",this);
    row->addWidget(startLabel); row->addWidget(startDateTimeEdit); row->addWidget(endLabel); row->addWidget(endDateTimeEdit); row->addWidget(fetchButton); row->addSpacing(24); row->addWidget(autoRefreshCheckBox); row->addWidget(countdownLabel); row->addStretch();

    chartsContainer=new QWidget(); chartsLayout=new QVBoxLayout(chartsContainer); chartsLayout->setContentsMargins(0,0,0,0);
    if(config.scrollableCharts()) { scrollArea=new QScrollArea(this); scrollArea->setWidgetResizable(true); scrollArea->setWidget(chartsContainer); mainLayout->addWidget(controlGroup); mainLayout->addWidget(scrollArea,1); }
    else { mainLayout->addWidget(controlGroup); mainLayout->addWidget(chartsContainer,1); }
    setCentralWidget(centralWidget);
    connect(fetchButton,&QPushButton::clicked,this,&SensorDashboard::onFetchClicked);
    connect(autoRefreshCheckBox,&QCheckBox::toggled,this,&SensorDashboard::onAutoRefreshToggled);
    setStatus("Ready");
}

void SensorDashboard::onFetchClicked(){ fetchAllSensors(); }
void SensorDashboard::onAutoRefreshToggled(bool checked)
{
    if(checked){countdownSeconds=refreshIntervalSec;refreshTimer->start();countdownTimer->start();}
    else{refreshTimer->stop();countdownTimer->stop();countdownLabel->clear();}
}
void SensorDashboard::onAutoRefreshTimeout(){endDateTimeEdit->setDateTime(QDateTime::currentDateTime().addDays(config.defaultRangeDaysAhead()));fetchAllSensors();countdownSeconds=refreshIntervalSec;}

void SensorDashboard::fetchSensorList()
{
    QNetworkReply *reply=networkManager->get(QNetworkRequest(QUrl(apiUrl+"/sensors")));
    connect(reply,&QNetworkReply::finished,this,[this,reply](){onSensorListReceived(reply);});
}

void SensorDashboard::onSensorListReceived(QNetworkReply *reply)
{
    if(reply->error()==QNetworkReply::NoError) {
        const QJsonDocument doc=QJsonDocument::fromJson(reply->readAll());
        if(doc.isArray()) {
            QStringList ids; for(const QJsonValue &v:doc.array()) ids.append(v.toString());
            if(!ids.isEmpty()){ sensorIds=ids; fetchSensorIds=ids; }
        }
    }
    reply->deleteLater(); fetchAllSensors();
}

void SensorDashboard::fetchAllSensors()
{
    rawSeries.clear();
    // Re-resolve physical ids each cycle because explicit config can contain
    // derived charts interleaved with measured charts.
    if(config.hasExplicitSensorList()) fetchSensorIds=config.fetchableSensorIds();
    pendingRequests=fetchSensorIds.size();
    setStatus(QString("Fetching %1 measured series...").arg(pendingRequests));
    if(pendingRequests==0){evaluateDerivedSeries();return;}
    for(const QString &id:fetchSensorIds) fetchSensorData(id);
}

void SensorDashboard::fetchSensorData(const QString &sensorId)
{
    QUrl url(apiUrl+"/sensor/"+sensorId); QUrlQuery q;
    q.addQueryItem("start",startDateTimeEdit->dateTime().toUTC().toString(Qt::ISODate));
    q.addQueryItem("end",endDateTimeEdit->dateTime().toUTC().toString(Qt::ISODate)); url.setQuery(q);
    QNetworkReply *reply=networkManager->get(QNetworkRequest(url));
    connect(reply,&QNetworkReply::finished,this,[this,sensorId,reply](){onDataReceived(sensorId,reply);});
}

void SensorDashboard::onDataReceived(const QString &sensorId,QNetworkReply *reply)
{
    QJsonArray a;
    if(reply && reply->error()==QNetworkReply::NoError) {
        const QJsonDocument doc=QJsonDocument::fromJson(reply->readAll());
        if(doc.isArray()) a=doc.array(); else if(doc.isObject()) a=doc.object().value("readings").toArray();
    } else if(reply) qWarning()<<"Error fetching"<<sensorId<<reply->errorString();
    rawSeries.insert(sensorId,a);
    updateChart(sensorId,a);
    if(reply) reply->deleteLater();
    if(--pendingRequests<=0) evaluateDerivedSeries();
}

QJsonArray SensorDashboard::evaluateDerived(const SensorDef &def,QString *error) const
{
    QJsonArray out;
    if(def.inputs.isEmpty() || def.expression.trimmed().isEmpty()) { if(error)*error="missing inputs/expression"; return out; }

    // v1 intentionally evaluates on one source timeline. The schema already
    // supports multiple named inputs; timestamp alignment/resampling can be
    // added here later without changing config or expression syntax.
    if(def.inputs.size()!=1) { if(error)*error="multi-input derived series requires timestamp alignment (not implemented yet)"; return out; }
    const QString var=def.inputs.firstKey();
    const QString sourceId=def.inputs.value(var);
    if(!rawSeries.contains(sourceId)) { if(error)*error=QString("source '%1' not loaded").arg(sourceId); return out; }

    for(const QJsonValue &rv:rawSeries.value(sourceId)) {
        if(!rv.isObject()) continue;
        const QJsonObject r=rv.toObject();
        bool numeric=false; double sourceValue=0;
        const QJsonValue vf=r.value("value");
        if(vf.isDouble()){sourceValue=vf.toDouble();numeric=true;}
        else if(vf.isString()) sourceValue=vf.toString().toDouble(&numeric);
        if(!numeric) continue;

        QMap<QString,double> vars=def.params; vars.insert(var,sourceValue);
        // Let expressions may refer to other lets. Resolve repeatedly so JSON
        // object ordering is irrelevant.
        QMap<QString,QString> unresolved=def.lets;
        bool progressed=true;
        while(!unresolved.isEmpty() && progressed) {
            progressed=false;
            for(auto it=unresolved.begin();it!=unresolved.end();) {
                double v; QString e;
                if(ExpressionEvaluator::evaluate(it.value(),vars,&v,&e)) { vars.insert(it.key(),v); it=unresolved.erase(it); progressed=true; }
                else ++it;
            }
        }
        if(!unresolved.isEmpty()) continue;

        if(!def.validWhen.trimmed().isEmpty()) {
            double valid=0; QString e;
            if(!ExpressionEvaluator::evaluate(def.validWhen,vars,&valid,&e) || valid==0.0) continue; // gap, never zero
        }
        double value=0; QString e;
        if(!ExpressionEvaluator::evaluate(def.expression,vars,&value,&e) || !std::isfinite(value)) continue;
        if(def.floorAtZero && value<0) value=0;
        QJsonObject d; d.insert("timestamp",r.value("timestamp")); d.insert("value",value); d.insert("unit",def.unit); out.append(d);
    }
    return out;
}

void SensorDashboard::evaluateDerivedSeries()
{
    for(const QString &id:config.derivedSensorIds()) {
        const SensorDef d=config.sensorDef(id); QString error;
        const QJsonArray a=evaluateDerived(d,&error);
        if(!error.isEmpty()) qWarning()<<"Derived series"<<id<<error;
        rawSeries.insert(id,a); updateChart(id,a);
    }
    setStatus(QString("All series loaded — %1").arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
}

SensorChart &SensorDashboard::getOrCreateChart(const QString &id)
{
    if(!sensorCharts.contains(id)) {
        SensorChart sc; sc.chart=new QChart(); sc.chart->setBackgroundBrush(QColor("#21252b")); sc.chart->legend()->hide();
        QFont tf;tf.setPixelSize(14);tf.setBold(true);sc.chart->setTitleFont(tf);sc.chart->setTitleBrush(QColor("#cfd8dc"));sc.chart->setTitle(friendlyName(id));
        sc.series=new QLineSeries(); QPen pen(seriesColor(id));pen.setWidth(2);sc.series->setPen(pen);
        QLineSeries *lower=new QLineSeries(); sc.area=new QAreaSeries(sc.series,lower);sc.area->setBrush(areaColor(id));sc.area->setPen(pen);sc.chart->addSeries(sc.area);
        sc.axisX=new QDateTimeAxis();sc.axisX->setFormat("M/dd HH:mm");sc.axisX->setTickCount(5);sc.axisX->setLabelsColor(QColor("#78909c"));sc.chart->addAxis(sc.axisX,Qt::AlignBottom);sc.area->attachAxis(sc.axisX);
        sc.axisY=new QValueAxis();sc.axisY->setTickCount(5);sc.axisY->setLabelsColor(QColor("#78909c"));sc.chart->addAxis(sc.axisY,Qt::AlignLeft);sc.area->attachAxis(sc.axisY);
        sc.chartView=new QChartView(sc.chart);sc.chartView->setRenderHint(QPainter::Antialiasing);sc.chartView->setStyleSheet("background-color:#21252b;border-radius:10px;");
        if(config.scrollableCharts()){sc.chartView->setMinimumHeight(280);sc.chartView->setMaximumHeight(360);} else sc.chartView->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
        int p=sensorIds.indexOf(id);if(p<0||p>chartsLayout->count())p=chartsLayout->count();chartsLayout->insertWidget(p,sc.chartView,1);sensorCharts.insert(id,sc);
    }
    return sensorCharts[id];
}

void SensorDashboard::updateChart(const QString &id,const QJsonArray &a)
{
    SensorChart &sc=getOrCreateChart(id);sc.series->clear();if(sc.area&&sc.area->lowerSeries())sc.area->lowerSeries()->clear();
    if(a.isEmpty()){sc.chart->setTitle(friendlyName(id)+"  (no data)");return;}
    double lo=std::numeric_limits<double>::max(),hi=std::numeric_limits<double>::lowest();QDateTime t0,t1;QString unit;
    for(const QJsonValue &x:a){if(!x.isObject())continue;const QJsonObject r=x.toObject();const QDateTime t=QDateTime::fromString(r.value("timestamp").toString(),Qt::ISODate);if(!t.isValid())continue;bool ok=false;double v=r.value("value").isDouble()?r.value("value").toDouble():r.value("value").toString().toDouble(&ok);if(r.value("value").isDouble())ok=true;if(!ok)continue;sc.series->append(t.toMSecsSinceEpoch(),v);lo=qMin(lo,v);hi=qMax(hi,v);if(!t0.isValid()||t<t0)t0=t;if(!t1.isValid()||t>t1)t1=t;if(unit.isEmpty())unit=r.value("unit").toString();}
    if(sc.series->count()==0){sc.chart->setTitle(friendlyName(id)+"  (no valid data)");return;}
    if(QLineSeries *lower=qobject_cast<QLineSeries*>(sc.area->lowerSeries())){double f=floorAtZero(id)?0:lo;for(const QPointF &p:sc.series->points())lower->append(p.x(),f);}
    const QString u=unit.isEmpty()?unitLabel(id):unit;sc.chart->setTitle(QString("%1 (%2)  —  %3 readings").arg(friendlyName(id),u).arg(sc.series->count()));
    double ymin=floorAtZero(id)?0:lo,range=hi-ymin,pad=range==0?(hi!=0?qAbs(hi)*.1:1):range*.1;sc.axisY->setRange(floorAtZero(id)?0:ymin-pad,hi+pad);sc.axisX->setRange(t0,t1);
}

void SensorDashboard::setStatus(const QString &s){statusBar()->showMessage(s);}
QString SensorDashboard::friendlyName(const QString &id){return config.displayName(id);}
QString SensorDashboard::unitLabel(const QString &id){const QString u=config.unit(id);return u.isEmpty()?QStringLiteral("Value"):u;}
QColor SensorDashboard::seriesColor(const QString &id){return config.lineColor(id);}
QColor SensorDashboard::areaColor(const QString &id){return config.areaColor(id);}
bool SensorDashboard::floorAtZero(const QString &id){return config.floorAtZero(id);}
