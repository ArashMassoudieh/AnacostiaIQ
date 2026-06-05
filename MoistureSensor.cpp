/////////////////////////////////////////////////////////////
// MOISTURESENSOR.CPP - Soil moisture sensor (MCP3008 over SPI)
/////////////////////////////////////////////////////////////

#include "MoistureSensor.h"
#include <cmath>

MoistureSensor::MoistureSensor(const QString &id, const QString &unit,
                               const QString &name, int spiChannel, int adcChannel)
    : Sensor(id, unit, name),
      m_spiChannel(spiChannel), m_adcChannel(adcChannel) {
}

MoistureSensor::~MoistureSensor() {
    cleanup();
}

bool MoistureSensor::initialize() {
#ifdef RasPi
    if (wiringPiSPISetup(m_spiChannel, 1350000) == -1) { // 1.35 MHz
        qWarning() << "MoistureSensor: wiringPi SPI setup failed on channel"
                   << m_spiChannel;
        setAvailable(false);
        return false;
    }
    setAvailable(true);
    return true;
#else
    // No GPIO/SPI on this system (e.g. desktop/dev machine)
    qWarning() << "MoistureSensor: no GPIO on this system "
                  "— moisture sensor unavailable";
    setAvailable(false);
    return false;
#endif
}

void MoistureSensor::cleanup() {
}

int MoistureSensor::readChannel(int channel) {
#ifdef RasPi
    unsigned char buf[3];

    buf[0] = 0x01;
    buf[1] = (0x08 | channel) << 4;
    buf[2] = 0x00;

    wiringPiSPIDataRW(m_spiChannel, buf, 3);

    return ((buf[1] & 0x03) << 8) | buf[2];
#else
    Q_UNUSED(channel);
    return -1;
#endif
}

double MoistureSensor::rawToMoisturePercent(int raw) {
    // ADC range 0–1023.
    const int DRY = 645;
    const int WET = 160;

    double pct = 100.0 * (DRY - raw) / (DRY - WET);
    return qBound(0.0, pct, 100.0);
}

double MoistureSensor::measure() {
#ifdef RasPi
    int raw = readChannel(m_adcChannel);

    double moisture = rawToMoisturePercent(raw);

    qDebug() << "Moisture :" << moisture;

    return std::round(moisture);
#else
    // No GPIO/SPI on this system — no real reading available
    return -1;
#endif
}
