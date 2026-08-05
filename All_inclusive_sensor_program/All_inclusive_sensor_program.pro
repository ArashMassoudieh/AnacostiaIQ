#-------------------------------------------------
# Sensor Project
# Raspberry Pi 5
#-------------------------------------------------

TEMPLATE = app
TARGET = SensorProject

CONFIG += console
CONFIG -= app_bundle

QT -= gui

# C++ Standard
CONFIG += c++17

# Source files
SOURCES += \
    main.cpp \
    ADC.cpp \
    HCSR04.cpp \
    MB7389.cpp

# Header files
HEADERS += \
    ADC.h \
    HCSR04.h \
    MB7389.h

# libgpiod
LIBS += -lgpiodcxx -lgpiod

# Compiler warnings
QMAKE_CXXFLAGS += -Wall -Wextra
