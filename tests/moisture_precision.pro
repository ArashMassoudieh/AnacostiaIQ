QT += core network testlib
QT -= gui
CONFIG += c++17 console testcase
CONFIG -= app_bundle
TEMPLATE = app
TARGET = moisture_precision_test
INCLUDEPATH += $$PWD/..
SOURCES += $$PWD/moisture_precision_test.cpp \
           $$PWD/../MoistureSensorI2C.cpp \
           $$PWD/../HealthMonitor.cpp
HEADERS += $$PWD/../Sensor.h $$PWD/../HealthMonitor.h
