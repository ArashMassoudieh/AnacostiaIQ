/////////////////////////////////////////////////////////////
// MAXBOTIXSENSOR.CPP - MaxBotix MB7389-100 ultrasonic sensor
//
//  Pin connections (Pi to MB7389-100):
//    5V  -> V+
//    GND -> GND
//    Pi UART RX <- sensor serial output (pin 5 on the sensor)
//
//  Serial framing: "Rxxxx\r", xxxx = range in millimetres, 9600 8N1.
/////////////////////////////////////////////////////////////

#include "MaxbotixSensor.h"
#include <QElapsedTimer>
#include <QDebug>
#include <cctype>

#ifdef RasPi
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#endif

MaxbotixSensor::MaxbotixSensor(const QString &id, const QString &unit,
                               const QString &name, const QString &device,
                               double totalLength)
    : Sensor(id, unit, name),
      m_device(device), m_totalLength(totalLength) {
}

MaxbotixSensor::~MaxbotixSensor() {
    cleanup();
}

double MaxbotixSensor::mmToUnit(int rangeMm) const {
    const QString u = unit().toLower();
    if (u == "in")  return rangeMm / 25.4;   // mm -> inches
    if (u == "cm")  return rangeMm / 10.0;   // mm -> centimetres
    return rangeMm;                          // "mm" (or unknown) -> raw mm
}

bool MaxbotixSensor::initialize() {
#ifdef RasPi
    m_fd = open(m_device.toUtf8().constData(), O_RDWR | O_NOCTTY);
    if (m_fd < 0) {
        qWarning() << "MaxbotixSensor: cannot open" << m_device;
        setAvailable(false);
        return false;
    }

    struct termios tty {};
    if (tcgetattr(m_fd, &tty) != 0) {
        qWarning() << "MaxbotixSensor: tcgetattr failed on" << m_device;
        close(m_fd);
        m_fd = -1;
        setAvailable(false);
        return false;
    }

    cfsetispeed(&tty, B9600);
    cfsetospeed(&tty, B9600);

    tty.c_cflag &= ~PARENB;   // no parity
    tty.c_cflag &= ~CSTOPB;   // 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;       // 8 data bits
    tty.c_cflag |= CREAD;     // enable receiver
    tty.c_cflag |= CLOCAL;    // ignore modem control lines

    tty.c_iflag = 0;
    tty.c_oflag = 0;
    tty.c_lflag = 0;

    tty.c_cc[VMIN]  = 0;      // non-blocking-ish: return as data arrives
    tty.c_cc[VTIME] = 1;      // 0.1s inter-byte timeout

    if (tcsetattr(m_fd, TCSANOW, &tty) != 0) {
        qWarning() << "MaxbotixSensor: tcsetattr failed on" << m_device;
        close(m_fd);
        m_fd = -1;
        setAvailable(false);
        return false;
    }

    setAvailable(true);
    return true;
#else
    // No serial hardware on this system (e.g. desktop/dev machine)
    qWarning() << "MaxbotixSensor: no serial on this system "
                  "— sensor unavailable";
    setAvailable(false);
    return false;
#endif
}

void MaxbotixSensor::cleanup() {
#ifdef RasPi
    if (m_fd >= 0) {
        close(m_fd);
        m_fd = -1;
    }
#endif
}

double MaxbotixSensor::measure() {
#ifdef RasPi
    tcflush(m_fd, TCIFLUSH);
    if (m_fd < 0)
        return -1;

    // Parse the streaming "Rxxxx" frames. Unlike the test program's
    // infinite loop, we read at most until a valid 4-digit frame is
    // assembled or READ_TIMEOUT_MS elapses, so a stalled/disconnected
    // sensor can't block the monitoring tick.
    enum State { WaitR, ReadDigits };
    State state = WaitR;
    QString digits;

    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < READ_TIMEOUT_MS) {
        char c;
        int n = read(m_fd, &c, 1);
        if (n <= 0)
            continue;   // no byte this slice; VTIME handles pacing

        if (state == WaitR) {
            if (c == 'R') {
                digits.clear();
                state = ReadDigits;
            }
        } else { // ReadDigits
            if (std::isdigit(static_cast<unsigned char>(c))) {
                digits += c;
                if (digits.size() == 4) {
                    int rangeMm = digits.toInt();

                    // Out-of-range sentinels per the MB7389 datasheet
                    if (rangeMm <= MIN_RANGE_MM || rangeMm >= MAX_RANGE_MM) {
                        qWarning() << "MaxbotixSensor: range out of bounds:"
                                   << rangeMm << "mm";
                        return -1;
                    }

                    double measured = mmToUnit(rangeMm);
                    double depth = m_totalLength - measured;   // convert distance → water depth
                    qDebug() << "MaxbotixSensor: distance =" << measured << "depth =" << depth;
                    return depth;

                }
            } else {
                state = WaitR;   // malformed frame, resync
            }
        }
    }

    qWarning() << "MaxbotixSensor: no valid frame within timeout";
    return -1;
#else
    return -1;
#endif
}
