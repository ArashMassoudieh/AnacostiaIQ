#include <QtTest>
#include "Bmp390Sensor.h"
class Sample : public Bmp390Sensor {
public:
 Sample():Bmp390Sensor("test","Test",nullptr,true) {}
 double sample=0;
 double measure() override { return sample; }
};
class BmpTest:public QObject {
 Q_OBJECT
private slots:
 void signedTemperature() {
  Sample s;
  for(double t : {-40.0,-1.0,0.0,25.0,85.0}) {
   s.sample=t; QCOMPARE(s.takeReading(),t); QVERIFY(s.lastValidReading().isValid());
  }
 }
 void invalidIsNotTemperature() {
  Sample s; s.sample=s.invalidReading();
  QVERIFY(!s.acceptsReading(s.takeReading()));
  QCOMPARE(s.consecutiveFailures(),1);
  QVERIFY(!s.lastValidReading().isValid());
 }
 void pressureBounds() {
  Bmp390Sensor s("p","Pressure",nullptr,false);
  QVERIFY(s.acceptsReading(1013.25));
  QVERIFY(!s.acceptsReading(0)); QVERIFY(!s.acceptsReading(1300));
  QVERIFY(!s.acceptsReading(std::numeric_limits<double>::infinity()));
  QVERIFY(!s.initialize());
 }
};
QTEST_GUILESS_MAIN(BmpTest)
#include "bmp390_test.moc"
