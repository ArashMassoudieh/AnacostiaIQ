QT = core

CONFIG += c++17 cmdline

SOURCES += \
        main.cpp

# ADD THIS LINE 👇
LIBS += -lgpiodcxx -lgpiod

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
