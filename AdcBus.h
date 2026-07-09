/////////////////////////////////////////////////////////////
// ADCBUS.H - Shared ADC0804 + CD4014 acquisition bus
//
//  Up to four ADC0804 converters are read in lockstep. Each one
//  feeds a CD4014 parallel-in / serial-out shift register, and all
//  the registers are driven by three lines shared across the whole
//  bank:
//
//    WR      -> starts a conversion on every ADC at once
//    P/S     -> HIGH latches each ADC's byte into its CD4014
//    CLOCK   -> shifts every CD4014 one bit, together
//
//  Each converter then reports on its own data line (the CD4014 Q8
//  output), so one conversion + eight clocks yields one byte per
//  channel. A single read therefore samples every channel at the
//  same instant.
//
//  Because the control lines are shared, they cannot be owned by an
//  individual sensor — libgpiod hands out each line exclusively. The
//  bus owns them, and MoistureSensor holds a shared_ptr to it plus
//  the data pin identifying its channel. Pins and timing come from
//  the "adc" block in config.json.
//
//  Not thread-safe: sensors are polled from the Qt main thread.
/////////////////////////////////////////////////////////////

#ifndef ADCBUS_H
#define ADCBUS_H

#include <QString>
#include <QVector>
#include <QHash>
#include <QElapsedTimer>

#ifdef RasPi
#include <gpiod.hpp>
#include <memory>
#endif

class AdcBus {
public:
    AdcBus(const QString &chip, int wrPin, int psPin, int clockPin,
           int conversionDelayMs, int pulseWidthUs, int cacheMs);
    ~AdcBus();

    // Register a channel. Must be called before initialize(), since
    // the data lines are requested from the kernel in one batch.
    // Duplicate pins are ignored with a warning.
    void addChannel(int dataPin);

    // Request the GPIO lines. Idempotent: the first caller does the
    // work, later callers get the same answer. Returns false when
    // there's no GPIO or the request failed — callers stay usable,
    // read() then just reports no data.
    bool initialize();
    bool isAvailable() const { return m_available; }

    // Latest byte (0-255) for a channel, or -1 if unavailable, not
    // registered, or the acquisition failed. Triggers a conversion
    // unless the previous one is still within the cache window, in
    // which case that sample is reused — the channels were sampled
    // simultaneously anyway, and sensors sharing an interval fire on
    // the same event-loop turn.
    int read(int dataPin);

private:
    bool convertAndShift();   // one conversion -> a byte per channel
    void cleanup();

    QString m_chip;
    int m_wrPin;
    int m_psPin;
    int m_clockPin;
    int m_conversionDelayMs;
    int m_pulseWidthUs;
    int m_cacheMs;

    QVector<int>    m_dataPins;    // channel data lines, registration order
    QHash<int, int> m_values;      // dataPin -> byte from the last conversion
    QElapsedTimer   m_sampleAge;   // time since that conversion
    bool            m_haveSample = false;

    bool m_initialized = false;
    bool m_available   = false;

#ifdef RasPi
    void pulse(gpiod::line_request &req, unsigned int pin);

    std::unique_ptr<gpiod::chip>          m_chipObj;
    std::unique_ptr<gpiod::line_request>  m_ctrlReq;   // WR, P/S, CLOCK
    std::unique_ptr<gpiod::line_request>  m_dataReq;   // every channel line
#endif
};

#endif // ADCBUS_H
