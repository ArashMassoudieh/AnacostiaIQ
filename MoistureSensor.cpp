/////////////////////////////////////////////////////////////
// MOISTURESENSOR.CPP - Soil moisture sensor (MCP3008 over SPI)
/////////////////////////////////////////////////////////////
#include "MoistureSensor.h"
#include <cmath>
#ifdef RasPi
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#endif

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
    const QString dev = QString("/dev/spidev0.%1").arg(m_spiChannel);
    m_fd = ::open(dev.toUtf8().constData(), O_RDWR);
    if (m_fd < 0) {
        qWarning() << "MoistureSensor: failed to open" << dev;
        setAvailable(false);
        return false;
    }

    uint8_t  mode  = SPI_MODE_0;
    uint8_t  bits  = 8;
    uint32_t speed = 1350000;   // 1.35 MHz
    if (ioctl(m_fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(m_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(m_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        qWarning() << "MoistureSensor: spidev config failed on" << dev;
        ::close(m_fd);
        m_fd = -1;
        setAvailable(false);
        return false;
    }

    setAvailable(true);
    return true;
#else
    qWarning() << "MoistureSensor: no GPIO/SPI on this system "
                  "— moisture sensor unavailable";
    setAvailable(false);
    return false;
#endif
}

void MoistureSensor::cleanup() {
#ifdef RasPi
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
#endif
}

int MoistureSensor::readChannel(int channel) {
#ifdef RasPi
    if (m_fd < 0) return -1;

    uint8_t tx[3];
    tx[0] = 0x01;
    tx[1] = (0x08 | channel) << 4;
    tx[2] = 0x00;
    uint8_t rx[3] = {0, 0, 0};

    struct spi_ioc_transfer tr;
    std::memset(&tr, 0, sizeof(tr));
    tr.tx_buf        = reinterpret_cast<__u64>(tx);
    tr.rx_buf        = reinterpret_cast<__u64>(rx);
    tr.len           = 3;
    tr.speed_hz      = 1350000;
    tr.bits_per_word = 8;

    if (ioctl(m_fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
        qWarning() << "MoistureSensor: SPI transfer failed";
        return -1;
    }

    return ((rx[1] & 0x03) << 8) | rx[2];
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
    if (raw < 0) return -1;            // honest gap rather than fake reading
    double moisture = rawToMoisturePercent(raw);
    qDebug() << "Moisture :" << moisture;
    return std::round(moisture);
#else
    return -1;
#endif
}
