/////////////////////////////////////////////////////////////
// MAXBOTIXSENSOR.CPP - MaxBotix MB7389-100 ultrasonic sensor
//
//  Pin connections (Pi to MB7389-100):
//    5V  -> V+
//    GND -> GND
//    Pi UART RX <- sensor serial output (pin 5 on the sensor)
//    Pi GPIO (triggerPin) -> sensor RX/control, if triggerPin >= 0
//
//  Serial framing: "Rxxxx\r", xxxx = range in millimetres, 9600 8N1.
//
//  Trigger protocol (when triggerPin is configured) matches Sean
//  Morgenstern's proven reference implementation exactly: the line
//  idles HIGH ("free-run"), and each reading pulls it LOW for 145ms
//  then back HIGH for 145ms before the sensor transmits a frame.
/////////////////////////////////////////////////////////////

#include "MaxbotixSensor.h"
#include <QElapsedTimer>
#include <QDebug>
#include <cctype>

#ifdef RasPi
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <poll.h>
#include <thread>
#include <chrono>
#endif

MaxbotixSensor::MaxbotixSensor(const QString &id, const QString &unit,
                               const QString &name, const QString &device,
                               double totalLength, int triggerPin,
                               const QString &chip)
    : Sensor(id, unit, name),
      m_device(device), m_totalLength(totalLength),
      m_triggerPin(triggerPin), m_chipPath(chip) {
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

    // VMIN=1/VTIME=1 (blocking single-byte reads) rather than VMIN=0's
    // "return every ~100ms even with nothing" mode — confirmed by a
    // side-by-side test against Sean's reference program that the RP1
    // UART on this Pi 5 reliably delivers bytes under the former and
    // not the latter. The overall per-measure() timeout is enforced
    // separately with poll() below rather than relying on VTIME.
    tty.c_cc[VMIN]  = 1;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(m_fd, TCSANOW, &tty) != 0) {
        qWarning() << "MaxbotixSensor: tcsetattr failed on" << m_device;
        close(m_fd);
        m_fd = -1;
        setAvailable(false);
        return false;
    }

    if (m_triggerPin >= 0) {
        try {
            m_chip = std::make_unique<gpiod::chip>(m_chipPath.toStdString());

            m_triggerReq = std::make_unique<gpiod::line_request>(
                m_chip->prepare_request()
                    .set_consumer("anacostiaiq-mb7389-trigger")
                    .add_line_settings(
                        m_triggerPin,
                        gpiod::line_settings().set_direction(
                            gpiod::line::direction::OUTPUT))
                    .do_request());

            // Idle HIGH = free-run, matching the reference implementation.
            m_triggerReq->set_value(m_triggerPin, gpiod::line::value::ACTIVE);
        }
        catch (const std::exception &e) {
            qWarning() << "MaxbotixSensor: trigger GPIO init failed:" << e.what();
            m_triggerReq.reset();
            m_chip.reset();
            close(m_fd);
            m_fd = -1;
            setAvailable(false);
            return false;
        }
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
    m_triggerReq.reset();
    m_chip.reset();
    if (m_fd >= 0) {
        close(m_fd);
        m_fd = -1;
    }
#endif
}

double MaxbotixSensor::measure() {
#ifdef RasPi
    if (m_fd < 0)
        return -1;

    // Drop whatever accumulated between ticks: the sensor streams
    // continuously, and a stale frame would report an old range.
    tcflush(m_fd, TCIFLUSH);

    if (m_triggerReq) {
        // Pull the line LOW then back HIGH to start a new measurement —
        // the timing the sensor actually requires before it transmits.
        m_triggerReq->set_value(m_triggerPin, gpiod::line::value::INACTIVE);
        std::this_thread::sleep_for(std::chrono::milliseconds(TRIGGER_PULSE_MS));
        m_triggerReq->set_value(m_triggerPin, gpiod::line::value::ACTIVE);
        std::this_thread::sleep_for(std::chrono::milliseconds(TRIGGER_PULSE_MS));
    }

    // Parse the streaming "Rxxxx" frames. Unlike the test program's
    // infinite loop, we read at most until a valid 4-digit frame is
    // assembled or READ_TIMEOUT_MS elapses, so a stalled/disconnected
    // sensor can't block the monitoring tick. The bound is enforced by
    // poll() up front; the actual read() only ever runs once poll()
    // says a byte is already waiting, so it returns immediately.
    enum State { WaitR, ReadDigits };
    State state = WaitR;
    QString digits;

    QElapsedTimer timer;
    timer.start();

    while (true) {
        const int remaining = READ_TIMEOUT_MS - static_cast<int>(timer.elapsed());
        if (remaining <= 0)
            break;

        struct pollfd pfd{};
        pfd.fd = m_fd;
        pfd.events = POLLIN;
        const int pret = poll(&pfd, 1, remaining);
        if (pret <= 0)
            continue;   // timed out or interrupted; loop re-checks elapsed()
        if (!(pfd.revents & POLLIN))
            continue;

        char c;
        int n = read(m_fd, &c, 1);
        if (n <= 0)
            continue;   // shouldn't happen once poll() says data is ready

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
