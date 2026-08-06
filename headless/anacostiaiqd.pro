#############################################################
# anacostiaiqd — headless AnacostiaIQ
#
# Builds the sensor/weather monitoring loop with no dashboard.
# Links QtCore + QtNetwork only: no widgets, no charts, and so no
# X11/Wayland dependency. Every source below is shared verbatim with
# the GUI build in the parent directory — the only files unique to
# this target are HeadlessMonitor and this main.cpp.
#
#   cd headless && qmake && make
#############################################################

QT       += core network
QT       -= gui

CONFIG   += c++17 console
CONFIG   -= app_bundle

TARGET   = anacostiaiqd
TEMPLATE = app

INCLUDEPATH += $$PWD/..

# RasPi gates the GPIO code. Unlike the GUI .pro this detects the Pi
# instead of asking you to edit the file, so the same checkout builds
# on both machines with no local modification to conflict on a pull.
# Force it either way with:  qmake DEFINES+=RasPi
contains(QMAKE_HOST.arch, "^(arm|aarch64)") {
    DEFINES += RasPi
}

contains(DEFINES, RasPi) {
    # libgpiod v2 C++ bindings: DistanceSensor (HC-SR04 trig/echo) and
    # AdcBus (ADC0804 + CD4014, shared by MoistureSensor).
    # MaxbotixSensor uses POSIX termios — no library needed.
    LIBS += -lgpiodcxx
    message("anacostiaiqd: building WITH GPIO (RasPi defined)")
} else {
    message("anacostiaiqd: building WITHOUT GPIO — sensors will report unavailable")
}

# Same config.json as the GUI, copied next to the executable. main.cpp
# looks there first, so the service works regardless of its cwd.
config_copy.files = $$PWD/../config.json
config_copy.path  = $$OUT_PWD
COPIES += config_copy

SOURCES += \
    $$PWD/../DatabaseWriter.cpp \
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
