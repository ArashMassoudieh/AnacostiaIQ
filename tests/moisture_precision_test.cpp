#include "MoistureSensorI2C.h"
#include "HealthMonitor.h"
#include "DatabaseWriter.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

// Link-time ADC substitute: exercise the real measure()/takeReading() and
// health evaluation without opening hardware or posting network requests.
static int adcRaw = 22964;
static bool adcPresent = true;
Ads1115Bus::Ads1115Bus(const QString &device, int address, double fs,
                       int rate, int cache)
    : m_device(device), m_address(address), m_fsVoltage(fs),
      m_dataRateSps(rate), m_cacheMs(cache) {}
Ads1115Bus::~Ads1115Bus() = default;
bool Ads1115Bus::initialize() { m_available = adcPresent; return adcPresent; }
int Ads1115Bus::read(int channel) { return channel == 2 ? adcRaw : -1; }
void DatabaseWriter::sendReading(const QString &, double, const QString &,
                                 const QDateTime &)
{
    qFatal("This test must never publish telemetry");
}

class MoisturePrecisionTest : public QObject {
    Q_OBJECT
private:
    QTemporaryDir stateDir;
    QJsonObject evaluate(Sensor &sensor,
                         const HealthMonitor::Thresholds &thresholds = {}) {
        HealthMonitor monitor(nullptr);
        monitor.setThresholds(thresholds);
        monitor.setSensors({&sensor});
        monitor.evaluateNow();
        QFile file(stateDir.path() + "/anacostiaiq/health.json");
        if (!file.open(QIODevice::ReadOnly))
            qFatal("Missing health snapshot");
        return QJsonDocument::fromJson(file.readAll()).object()
            .value("components").toObject()
            .value("sensor_moisture_a2").toObject();
    }
    std::shared_ptr<Ads1115Bus> bus() {
        return std::make_shared<Ads1115Bus>("unused", 0x48, 4.096, 128, 0);
    }
private slots:
    void init() { adcPresent = true; }
    void initTestCase() {
        QVERIFY(stateDir.isValid());
        qputenv("XDG_STATE_HOME", stateDir.path().toUtf8());
    }
    void fractionalEndpointsStayHealthy_data() {
        QTest::addColumn<int>("raw");
        QTest::addColumn<double>("low");
        QTest::addColumn<double>("high");
        QTest::newRow("observed-A2-dry") << 22964 << 0.2 << 0.3;
        QTest::newRow("near-wet") << 6436 << 99.7 << 99.9;
    }
    void fractionalEndpointsStayHealthy() {
        QFETCH(int, raw); QFETCH(double, low); QFETCH(double, high);
        adcRaw = raw;
        MoistureSensorI2C sensor("moisture_a2", "%", "A2", bus(), 2, 23000, 6400);
        QVERIFY(sensor.initialize());
        for (int i = 0; i < 4; ++i) {
            const double reading = sensor.takeReading();
            QVERIFY(reading > low && reading < high);
            QCOMPARE(evaluate(sensor).value("status").toString(), QString("healthy"));
        }
    }
    void trueBoundariesReportLimit_data() {
        QTest::addColumn<int>("raw");
        QTest::addColumn<QString>("reason");
        QTest::addColumn<QString>("status");
        QTest::newRow("dry-endpoint") << 23000 << QString("dry_limit_reached_check_calibration") << QString("degraded");
        QTest::newRow("wet-endpoint") << 6400 << QString("wet_limit_reached") << QString("healthy");
        QTest::newRow("clamped-wetter-than-reference") << 5000 << QString("wet_limit_reached") << QString("healthy");
    }
    void trueBoundariesReportLimit() {
        QFETCH(int, raw); QFETCH(QString, reason); QFETCH(QString, status); adcRaw = raw;
        MoistureSensorI2C sensor("moisture_a2", "%", "A2", bus(), 2, 23000, 6400);
        QVERIFY(sensor.initialize());
        for (int i = 0; i < 20; ++i) {
            sensor.takeReading();
            const auto state = evaluate(sensor);
            QCOMPARE(state.value("status").toString(), status);
            QCOMPARE(state.value("reason").toString(), reason);
        }
        adcRaw = 15000;
        sensor.takeReading();
        QCOMPARE(evaluate(sensor).value("status").toString(), QString("healthy"));
    }
    void failuresAfterWetLimitKeepFailureReason() {
        adcRaw = 6400;
        MoistureSensorI2C sensor("moisture_a2", "%", "A2", bus(), 2, 23000, 6400);
        QVERIFY(sensor.initialize()); sensor.takeReading();
        adcRaw = -1;
        sensor.takeReading(); sensor.takeReading();
        const auto state = evaluate(sensor);
        QCOMPARE(state.value("status").toString(), QString("degraded"));
        QCOMPARE(state.value("reason").toString(), QString("2_consecutive_failures"));
    }
    void unavailableAdcRemainsOffline() {
        adcPresent = false; adcRaw = 6400;
        MoistureSensorI2C sensor("moisture_a2", "%", "A2", bus(), 2, 23000, 6400);
        QVERIFY(!sensor.initialize());
        const auto state = evaluate(sensor);
        QCOMPARE(state.value("status").toString(), QString("offline"));
        QCOMPARE(state.value("reason").toString(), QString("sensor_unavailable"));
    }
    void staleWetReadingRemainsOffline() {
        adcRaw = 6400;
        MoistureSensorI2C sensor("moisture_a2", "%", "A2", bus(), 2, 23000, 6400);
        QVERIFY(sensor.initialize()); sensor.takeReading();
        HealthMonitor::Thresholds thresholds;
        thresholds.sensorStaleMinimumSeconds = 0;
        thresholds.sensorStalePollFactor = 0;
        QTest::qWait(1100);
        const auto state = evaluate(sensor, thresholds);
        QCOMPARE(state.value("status").toString(), QString("offline"));
        QVERIFY(state.value("reason").toString().startsWith("stale_"));
    }
    void transportFailuresRemainFailures() {
        adcRaw = -1;
        MoistureSensorI2C sensor("moisture_a2", "%", "A2", bus(), 2, 23000, 6400);
        QVERIFY(sensor.initialize());
        for (int i = 0; i < 3; ++i) QCOMPARE(sensor.takeReading(), -1.0);
        QVERIFY(evaluate(sensor).value("status").toString() != "healthy");
    }
};

QTEST_GUILESS_MAIN(MoisturePrecisionTest)
#include "moisture_precision_test.moc"
