QT = core
CONFIG += c++17 cmdline

SOURCES += \
        main.cpp

LIBS += -lgpiod -lgpiodcxx
INCLUDEPATH += /usr/include

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
