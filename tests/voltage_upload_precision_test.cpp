#include "DatabaseWriter.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>

class VoltageUploadPrecisionTest : public QObject {
    Q_OBJECT
private slots:
    void rawVoltageAndAdcCountDifferencesSurviveQueueAndRestart() {
        QTemporaryDir state;
        QVERIFY(state.isValid());
        qputenv("XDG_STATE_HOME", state.path().toUtf8());
        const auto time = QDateTime::fromString("2026-11-17T12:00:00Z", Qt::ISODate);
        QString path;
        {
            DatabaseWriter writer;
            // No event processing: deferred uploads cannot run in this scope.
            writer.setApiUrl("http://127.0.0.1:1/never-send");
            writer.setProject("John-McCormack");
            path = writer.queueFilePath();
            writer.sendReading("a0_raw", .830125, "V", time);
            writer.sendReading("a0_raw", .830250, "V", time);
            writer.sendReading("a0_raw", .830125, "V", time); // exact duplicate
            QCOMPARE(writer.pendingCount(), 2);
            QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
            const auto first = QJsonDocument::fromJson(file.readLine()).object();
            const auto second = QJsonDocument::fromJson(file.readLine()).object();
            QCOMPARE(first.value("value").toDouble(), .830125);
            QCOMPARE(second.value("value").toDouble(), .830250);
            QCOMPARE(first.value("unit").toString(), QString("V"));
            QCOMPARE(first.value("project").toString(), QString("John-McCormack"));
            QVERIFY(file.atEnd());
        }
        {
            DatabaseWriter resumed;
            resumed.setProject("John-McCormack");
            QCOMPARE(resumed.pendingCount(), 2);
            resumed.sendReading("a0_raw", .830125, "V", time);
            QCOMPARE(resumed.pendingCount(), 2);
        }
    }
    void historicalRoundedVoltagesAreNotRewrittenAndOtherUnitsStayStable() {
        QTemporaryDir state; QVERIFY(state.isValid());
        qputenv("XDG_STATE_HOME", state.path().toUtf8());
        const auto time = QDateTime::fromString("2026-11-17T12:00:00Z", Qt::ISODate);
        QString path;
        {
            DatabaseWriter writer;
            writer.setApiUrl("http://127.0.0.1:1/never-send");
            path=writer.queueFilePath();
            writer.sendReading("a0_raw", .83, "V", time);
            writer.sendReading("moisture", 91.23456, "%", time);
        }
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray before=file.readAll();file.close();
        {
            DatabaseWriter resumed;
            QCOMPARE(resumed.pendingCount(), 2);
        }
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), before);file.seek(0);file.readLine();
        QCOMPARE(QJsonDocument::fromJson(file.readLine()).object().value("value").toDouble(), 91.23);
    }
};
QTEST_GUILESS_MAIN(VoltageUploadPrecisionTest)
#include "voltage_upload_precision_test.moc"
