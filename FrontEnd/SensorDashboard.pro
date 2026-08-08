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

# ── Runtime config ──────────────────────────────────────────────
# config.json is read at runtime, not compiled in: the desktop build
# opens it from the working directory, and the WebAssembly build fetches
# it over HTTP from the directory it was served from. Either way it has
# to sit next to the binary, so copy it into the build directory on
# every build. Without this the build tree keeps whatever stale copy was
# put there by hand, and edits to the tracked file silently do nothing.
config_json.files = $$PWD/config.json
config_json.path  = $$OUT_PWD
COPIES += config_json

# Default rules for deployment
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
