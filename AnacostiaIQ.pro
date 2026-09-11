QT       += core gui
QT       += charts

greaterThan(QT_MAJOR_VERSION, 4):
QT += widgets network charts

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000

# RasPi: only define on ARM (the Raspberry Pi).
# To force a Pi build manually, run qmake with:  DEFINES+=RasPi

#DEFINES += RasPi

DEFINES += Qt5

config_copy.files = config.json
config_copy.path = $$OUT_PWD
COPIES += config_copy

SOURCES += \
    DatabaseWriter.cpp \
    HealthMonitor.cpp \
    RuntimeModeGuard.cpp \
    Config.cpp \
    AdcBus.cpp \
    RainPolicy.cpp \
    DistanceSensor.cpp \
    MoistureSensor.cpp \
    MaxbotixSensor.cpp \
    chartcontainer.cpp \
    main.cpp \
    WeatherFetcher.cpp \
    anacostiaiq.cpp

HEADERS += \
    DatabaseWriter.h \
    HealthMonitor.h \
    RuntimeModeGuard.h \
    Config.h \
    Sensor.h \
    AdcBus.h \
    RainPolicy.h \
    DistanceSensor.h \
    MoistureSensor.h \
    MaxbotixSensor.h \
    chartcontainer.h \
    WeatherFetcher.h \
    anacostiaiq.h

contains(DEFINES, RasPi) {
    LIBS += -lgpiodcxx
}

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
