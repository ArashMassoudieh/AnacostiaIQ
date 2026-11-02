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
Ads1115Bus::Ads1115Bus(const QString &device, int address, double fs,
                       int rate, int cache)
    : m_device(device), m_address(address), m_fsVoltage(fs),
      m_dataRateSps(rate), m_cacheMs(cache) {}
Ads1115Bus::~Ads1115Bus() = default;
bool Ads1115Bus::initialize() { m_available = true; return true; }
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
    QJsonObject evaluate(Sensor &sensor) {
        HealthMonitor monitor(nullptr);
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
    void trueBoundariesStillRaiseAlarm_data() {
        QTest::addColumn<int>("raw");
        QTest::newRow("dry-endpoint") << 23000;
        QTest::newRow("wet-endpoint") << 6400;
    }
    void trueBoundariesStillRaiseAlarm() {
        QFETCH(int, raw); adcRaw = raw;
        MoistureSensorI2C sensor("moisture_a2", "%", "A2", bus(), 2, 23000, 6400);
        QVERIFY(sensor.initialize());
        for (int i = 0; i < 3; ++i) sensor.takeReading();
        QCOMPARE(evaluate(sensor).value("status").toString(), QString("offline"));
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
