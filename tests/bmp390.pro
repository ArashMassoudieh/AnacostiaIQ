QT += core testlib
QT -= gui
CONFIG += c++17 console testcase
TARGET = bmp390_test
INCLUDEPATH += $$PWD/..
DEFINES += BMP3_FLOAT_COMPENSATION
SOURCES += $$PWD/bmp390_test.cpp $$PWD/../Bmp390Sensor.cpp $$PWD/../third_party/bmp3/bmp3.c
HEADERS += $$PWD/../Sensor.h
