#############################################################
# anacostiaiqd — headless AnacostiaIQ
#############################################################

QT       += core network
QT       -= gui

CONFIG   += c++17 console
CONFIG   -= app_bundle

TARGET   = anacostiaiqd
TEMPLATE = app

INCLUDEPATH += $$PWD/..

contains(QMAKE_HOST.arch, "^(arm|aarch64)") {
    DEFINES += RasPi
}

contains(DEFINES, RasPi) {
    LIBS += -lgpiodcxx
    message("anacostiaiqd: building WITH GPIO (RasPi defined)")
} else {
    message("anacostiaiqd: building WITHOUT GPIO — sensors will report unavailable")
}

config_copy.files = $$PWD/../config.json
config_copy.path  = $$OUT_PWD
COPIES += config_copy

SOURCES += \
    $$PWD/../DatabaseWriter.cpp \
    $$PWD/../HealthMonitor.cpp \
    $$PWD/../RuntimeModeGuard.cpp \
    $$PWD/../Config.cpp \
    $$PWD/../AdcBus.cpp \
    $$PWD/../RainPolicy.cpp \
    $$PWD/../DistanceSensor.cpp \
    $$PWD/../MoistureSensor.cpp \
    $$PWD/../MaxbotixSensor.cpp \
    $$PWD/../WeatherFetcher.cpp \
    HeadlessMonitor.cpp \
    main.cpp

HEADERS += \
    $$PWD/../DatabaseWriter.h \
    $$PWD/../HealthMonitor.h \
    $$PWD/../RuntimeModeGuard.h \
    $$PWD/../Config.h \
    $$PWD/../Sensor.h \
    $$PWD/../AdcBus.h \
    $$PWD/../RainPolicy.h \
    $$PWD/../DistanceSensor.h \
    $$PWD/../MoistureSensor.h \
    $$PWD/../MaxbotixSensor.h \
    $$PWD/../WeatherFetcher.h \
    HeadlessMonitor.h

unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
