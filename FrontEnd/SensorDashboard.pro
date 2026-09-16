QT += core gui widgets charts network

CONFIG += c++17

TARGET = SensorDashboard

# Layout mode is controlled at runtime by config.json.
SOURCES += \
    main.cpp \
    SensorDashboard.cpp \
    DashboardConfig.cpp \
    ExpressionEvaluator.cpp

HEADERS += \
    SensorDashboard.h \
    DashboardConfig.h \
    ExpressionEvaluator.h

# Runtime config is copied beside the desktop binary and fetched beside
# SensorDashboard.html by the WebAssembly build.
config_json.files = $$PWD/config.json
config_json.path  = $$OUT_PWD
COPIES += config_json

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
