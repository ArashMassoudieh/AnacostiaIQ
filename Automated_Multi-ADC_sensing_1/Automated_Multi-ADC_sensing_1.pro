QT -= gui

CONFIG += console
CONFIG -= app_bundle

CONFIG += c++17

TARGET = Automated_Multi-ADC_sensing_1
TEMPLATE = app

SOURCES += \
    main.cpp

LIBS += -lgpiodcxx -lgpiod

QMAKE_CXXFLAGS += -Wall -Wextra
