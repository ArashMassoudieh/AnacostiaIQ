/////////////////////////////////////////////////////////////
// ADS1115BUS.H - Shared ADS1115 16-bit I2C ADC
//
//  Replaces the ADC0804 + CD4014 bank for the New Build station.
//  One ADS1115 breakout exposes four single-ended channels (A0-A3)
//  over a two-wire I2C bus (SDA on GPIO2, SCL on GPIO3 — no other
//  GPIO lines involved, unlike AdcBus's shared WR/P/S/CLOCK lines).
//
//  Unlike AdcBus, the four channels are NOT sampled simultaneously:
//  the ADS1115 does one single-shot conversion per channel, so each
//  read() triggers its own conversion and waits for it. A short
//  per-channel cache (mirroring AdcBus's cacheMs) avoids repeating a
//  conversion when sensors sharing a poll interval read the same
//  channel on the same event-loop turn.
//
//  Register layout and conversion timing come from the ADS1115
//  datasheet; see the .cpp for the bit-level "why".
/////////////////////////////////////////////////////////////

#ifndef ADS1115BUS_H
#define ADS1115BUS_H

#include <QString>
#include <QHash>
#include <QElapsedTimer>

class Ads1115Bus {
public:
    // device      — I2C bus device, e.g. "/dev/i2c-1"
    // address     — 7-bit I2C address (0x48 with ADDR tied to GND)
    // fsVoltage   — desired full-scale range; snapped to the nearest
    //               ADS1115 PGA setting (6.144, 4.096, 2.048, 1.024,
    //               0.512, or 0.256 V). 4.096 V comfortably covers a
    //               3.3-V-powered analog probe with headroom.
    // dataRateSps — samples/sec (8-860); 128 is the datasheet default
    //               and gives an ~8 ms conversion.
    Ads1115Bus(const QString &device, int address, double fsVoltage,
               int dataRateSps, int cacheMs);
    ~Ads1115Bus();

    // Opens the I2C device and sets the slave address. Idempotent.
    // Returns false when there's no I2C bus or the open/ioctl failed
    // — callers stay usable, read() then just reports no data.
    bool initialize();
    bool isAvailable() const { return m_available; }

    // Raw signed 16-bit conversion result for a single-ended channel
    // (0-3), or -1 on failure/unavailable/out-of-range channel.
    // Triggers a fresh conversion unless a cached one for this exact
    // channel is still within the cache window.
    int read(int channel);

private:
    bool convertChannel(int channel, int &raw);

    QString m_device;
    int     m_address;
    double  m_fsVoltage;
    int     m_dataRateSps;
    int     m_cacheMs;

    struct CachedValue {
        int value = -1;
        QElapsedTimer age;
    };
    QHash<int, CachedValue> m_cache;

    bool m_initialized = false;
    bool m_available   = false;

#ifdef RasPi
    int m_fd = -1;
#endif
};

#endif // ADS1115BUS_H
