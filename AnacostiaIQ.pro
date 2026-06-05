QT       += core gui
QT       += charts

greaterThan(QT_MAJOR_VERSION, 4):
QT += widgets network charts

CONFIG += c++11

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

# RasPi: only define on ARM (the Raspberry Pi). wiringPi aborts at
# runtime on non-Pi hardware, so desktop/x86 builds must NOT define
# it — the sensor code then compiles to its safe "no GPIO" paths and
# the GUI runs normally with simulated/absent sensors.
#
# To force a Pi build manually, run qmake with:  DEFINES+=RasPi
contains(QMAKE_HOST.arch, arm.*)|contains(QMAKE_HOST.arch, aarch64) {
    DEFINES += RasPi
}

DEFINES += Qt5

# Copy config.json next to the built executable so the app can find
# it at runtime (the app looks for "config.json" in the working dir).
config_copy.files = config.json
config_copy.path = $$OUT_PWD
COPIES += config_copy

SOURCES += \
    DatabaseWriter.cpp \
    Config.cpp \
    DistanceSensor.cpp \
    MoistureSensor.cpp \
    MaxbotixSensor.cpp \
    chartcontainer.cpp \
    main.cpp \
    WeatherFetcher.cpp \
    anacostiaiq.cpp

HEADERS += \
    DatabaseWriter.h \
    Config.h \
    Sensor.h \
    DistanceSensor.h \
    MoistureSensor.h \
    MaxbotixSensor.h \
    chartcontainer.h \
    WeatherFetcher.h \
    anacostiaiq.h

contains(DEFINES, RasPi) {
    # wiringPi: MoistureSensor (SPI)
    LIBS += -lwiringPi
    # libgpiod v2 C++ bindings: DistanceSensor (HC-SR04 trig/echo)
    LIBS += -lgpiodcxx
}

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
