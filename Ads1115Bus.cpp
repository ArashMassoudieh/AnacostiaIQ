/////////////////////////////////////////////////////////////
// ADS1115BUS.CPP - Shared ADS1115 16-bit I2C ADC
/////////////////////////////////////////////////////////////

#include "Ads1115Bus.h"
#include <QDebug>
#include <cmath>

#ifdef RasPi
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <thread>
#include <chrono>
#endif

#ifdef RasPi
namespace {

// ADS1115 register pointers.
constexpr quint8 REG_CONVERSION = 0x00;
constexpr quint8 REG_CONFIG     = 0x01;

// PGA full-scale options the ADS1115 actually supports, in the order
// their 3-bit config code increases (0b000-0b101). Picking the
// nearest-but-not-smaller entry keeps the requested range covered
// rather than silently clipping it.
constexpr double kFsOptions[6] = {6.144, 4.096, 2.048, 1.024, 0.512, 0.256};

quint16 pgaCode(double fsVoltage) {
    for (int i = 0; i < 6; ++i) {
        if (fsVoltage >= kFsOptions[i] - 1e-6)
            return static_cast<quint16>(i);
    }
    return 5; // smallest range, i.e. most sensitive
}

// Data-rate codes (0b000-0b111) map to 8/16/32/64/128/250/475/860 SPS.
quint16 dataRateCode(int sps) {
    static const int table[8] = {8, 16, 32, 64, 128, 250, 475, 860};
    quint16 best = 4; // 128 SPS, the datasheet power-on default
    int bestDelta = std::abs(sps - table[4]);
    for (int i = 0; i < 8; ++i) {
        const int delta = std::abs(sps - table[i]);
        if (delta < bestDelta) {
            bestDelta = delta;
            best = static_cast<quint16>(i);
        }
    }
    return best;
}

} // namespace
#endif // RasPi

Ads1115Bus::Ads1115Bus(const QString &device, int address, double fsVoltage,
                       int dataRateSps, int cacheMs)
    : m_device(device), m_address(address), m_fsVoltage(fsVoltage),
      m_dataRateSps(dataRateSps), m_cacheMs(cacheMs) {
}

Ads1115Bus::~Ads1115Bus() {
#ifdef RasPi
    if (m_fd >= 0)
        close(m_fd);
#endif
}

bool Ads1115Bus::initialize() {
    if (m_initialized)
        return m_available;
    m_initialized = true;

#ifdef RasPi
    m_fd = open(m_device.toUtf8().constData(), O_RDWR);
    if (m_fd < 0) {
        qWarning() << "Ads1115Bus: cannot open" << m_device;
        m_available = false;
        return false;
    }
    if (ioctl(m_fd, I2C_SLAVE, m_address) < 0) {
        qWarning() << "Ads1115Bus: cannot address device at"
                   << Qt::hex << m_address << "on" << m_device;
        close(m_fd);
        m_fd = -1;
        m_available = false;
        return false;
    }
    m_available = true;
    return true;
#else
    qWarning() << "Ads1115Bus: no I2C on this system — ADC unavailable";
    m_available = false;
    return false;
#endif
}

bool Ads1115Bus::convertChannel(int channel, int &raw) {
#ifdef RasPi
    // Config register (datasheet Table 8), MSB first:
    //   bit15      OS        1 = start a single conversion
    //   bits14-12  MUX       100+channel = AINx vs GND, single-ended
    //   bits11-9   PGA       full-scale range (see pgaCode())
    //   bit8       MODE      1 = single-shot (power down between reads)
    //   bits7-5    DR        data rate (see dataRateCode())
    //   bits4-2    COMP_*    comparator mode/polarity/latch — unused
    //   bits1-0    COMP_QUE  11 = disable the comparator/ALERT pin
    const quint16 mux = static_cast<quint16>(0b100 + channel);
    const quint16 config =
        (1u << 15) |
        (mux << 12) |
        (pgaCode(m_fsVoltage) << 9) |
        (1u << 8) |
        (dataRateCode(m_dataRateSps) << 5) |
        0b00000011;

    quint8 writeBuf[3] = {
        REG_CONFIG,
        static_cast<quint8>(config >> 8),
        static_cast<quint8>(config & 0xFF)
    };
    if (write(m_fd, writeBuf, 3) != 3) {
        qWarning() << "Ads1115Bus: config write failed for channel" << channel;
        return false;
    }

    // Conversion takes ~1/dataRateSps; wait a little longer so a slow
    // conversion isn't read early.
    const int waitMs = std::max(2, (1000 / std::max(1, m_dataRateSps)) + 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));

    quint8 pointerBuf[1] = {REG_CONVERSION};
    if (write(m_fd, pointerBuf, 1) != 1) {
        qWarning() << "Ads1115Bus: pointer write failed for channel" << channel;
        return false;
    }

    // Qualified as ::read — Ads1115Bus already has a member named
    // read(int channel), which would otherwise hide the POSIX syscall.
    quint8 readBuf[2] = {0, 0};
    if (::read(m_fd, readBuf, 2) != 2) {
        qWarning() << "Ads1115Bus: conversion read failed for channel" << channel;
        return false;
    }

    raw = static_cast<qint16>((readBuf[0] << 8) | readBuf[1]);
    return true;
#else
    Q_UNUSED(channel);
    Q_UNUSED(raw);
    return false;
#endif
}

int Ads1115Bus::read(int channel) {
    if (!m_available || channel < 0 || channel > 3)
        return -1;

    CachedValue &cached = m_cache[channel];
    if (cached.age.isValid() && cached.age.elapsed() < m_cacheMs)
        return cached.value;

    int raw = -1;
    if (!convertChannel(channel, raw)) {
        cached.value = -1;
        cached.age.start();
        return -1;
    }

    cached.value = raw;
    cached.age.start();
    return raw;
}
