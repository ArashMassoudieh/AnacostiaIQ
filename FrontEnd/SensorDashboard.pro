QT += core gui widgets charts network

CONFIG += c++17

TARGET = SensorDashboard

# ── Layout mode ─────────────────────────────────────────────────
# The scrollable-vs-fit layout is now a RUNTIME setting in config.json
# ("scrollable_charts": true|false), so the old SCROLLABLE_CHARTS
# compile-time define is no longer needed.

SOURCES += \
    main.cpp \
    SensorDashboard.cpp \
    DashboardConfig.cpp

HEADERS += \
    SensorDashboard.h \
    DashboardConfig.h

# Default rules for deployment
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
