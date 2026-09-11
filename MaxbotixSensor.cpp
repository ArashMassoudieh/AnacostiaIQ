/////////////////////////////////////////////////////////////
// MAXBOTIXSENSOR.CPP - MaxBotix MB7389-100 ultrasonic sensor
//
//  Field-proven connections (Pi to MB7389-100):
//    5V  -> V+
//    GND -> GND
//    Pi UART RX <- sensor serial output (pin 5 on the sensor)
//    Pi trigger GPIO -> sensor RX/control input
//
//  Serial framing: "Rxxxx\r", xxxx = range in millimetres, 9600 8N1.
//  Before each reading the sensor RX/control line is driven LOW for
//  145 ms, then HIGH; after another 145 ms the fresh serial frame is read.
/////////////////////////////////////////////////////////////

#include "MaxbotixSensor.h"
#include <QElapsedTimer>
#include <QDebug>
#include <cctype>
#include <thread>
#include <chrono>

#ifdef RasPi
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#endif

MaxbotixSensor::MaxbotixSensor(const QString &id, const QString &unit,
                               const QString &name, const QString &device,
                               double totalLength, const QString &chip,
                               int triggerPin)
    : Sensor(id, unit, name),
      m_device(device), m_totalLength(totalLength),
      m_chip(chip), m_triggerPin(triggerPin) {
}

MaxbotixSensor::~MaxbotixSensor() {
    cleanup();
}

double MaxbotixSensor::mmToUnit(int rangeMm) const {
    const QString u = unit().toLower();
    if (u == "in")  return rangeMm / 25.4;
    if (u == "cm")  return rangeMm / 10.0;
    return rangeMm;
}

bool MaxbotixSensor::initialize() {
#ifdef RasPi
    // Recovery may call initialize() again after a previous failure.
    cleanup();

    if (m_triggerPin < 0) {
        qWarning() << "MaxbotixSensor: invalid trigger pin" << m_triggerPin;
        setAvailable(false);
        return false;
    }

    m_fd = open(m_device.toUtf8().constData(), O_RDWR | O_NOCTTY);
    if (m_fd < 0) {
        qWarning() << "MaxbotixSensor: cannot open" << m_device;
        setAvailable(false);
        return false;
    }

    struct termios tty {};
    if (tcgetattr(m_fd, &tty) != 0) {
        qWarning() << "MaxbotixSensor: tcgetattr failed on" << m_device;
        cleanup();
        setAvailable(false);
        return false;
    }

    cfsetispeed(&tty, B9600);
    cfsetospeed(&tty, B9600);

    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag |= CREAD;
    tty.c_cflag |= CLOCAL;

    tty.c_iflag = 0;
    tty.c_oflag = 0;
    tty.c_lflag = 0;

    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(m_fd, TCSANOW, &tty) != 0) {
        qWarning() << "MaxbotixSensor: tcsetattr failed on" << m_device;
        cleanup();
        setAvailable(false);
        return false;
    }

    tcflush(m_fd, TCIFLUSH);

    try {
        m_gpioChip = std::make_unique<gpiod::chip>(m_chip.toStdString());
        m_trigger = std::make_unique<gpiod::line_request>(
            m_gpioChip->prepare_request()
                .set_consumer("maxbotix_mb7389_trigger")
                .add_line_settings(
                    static_cast<unsigned>(m_triggerPin),
                    gpiod::line_settings()
                        .set_direction(gpiod::line::direction::OUTPUT))
                .do_request());

        // MB7389 RX/control HIGH is the normal/free-run state.
        m_trigger->set_value(static_cast<unsigned>(m_triggerPin),
                             gpiod::line::value::ACTIVE);
    }
    catch (const std::exception &e) {
        qWarning() << "MaxbotixSensor: GPIO initialization failed on"
                   << m_chip << "pin" << m_triggerPin << ":" << e.what();
        cleanup();
        setAvailable(false);
        return false;
    }

    setAvailable(true);
    return true;
#else
    qWarning() << "MaxbotixSensor: no serial/GPIO hardware on this system"
                  " — sensor unavailable";
    setAvailable(false);
    return false;
#endif
}

void MaxbotixSensor::cleanup() {
#ifdef RasPi
    m_trigger.reset();
    m_gpioChip.reset();

    if (m_fd >= 0) {
        close(m_fd);
        m_fd = -1;
    }
#endif
}

double MaxbotixSensor::measure() {
#ifdef RasPi
    if (m_fd < 0 || !m_trigger)
        return -1;

    // Match Sean's field-proven MB7389 acquisition sequence.
    tcflush(m_fd, TCIFLUSH);

    m_trigger->set_value(static_cast<unsigned>(m_triggerPin),
                         gpiod::line::value::INACTIVE);
    std::this_thread::sleep_for(std::chrono::milliseconds(TRIGGER_HOLD_MS));

    m_trigger->set_value(static_cast<unsigned>(m_triggerPin),
                         gpiod::line::value::ACTIVE);
    std::this_thread::sleep_for(std::chrono::milliseconds(MEASUREMENT_WAIT_MS));

    enum State { WaitR, ReadDigits };
    State state = WaitR;
    QString digits;

    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < READ_TIMEOUT_MS) {
        char c;
        int n = read(m_fd, &c, 1);
        if (n <= 0)
            continue;

        if (state == WaitR) {
            if (c == 'R') {
                digits.clear();
                state = ReadDigits;
            }
        } else {
            if (std::isdigit(static_cast<unsigned char>(c))) {
                digits += c;
                if (digits.size() == 4) {
                    const int rangeMm = digits.toInt();

                    if (rangeMm <= MIN_RANGE_MM || rangeMm >= MAX_RANGE_MM) {
                        qWarning() << "MaxbotixSensor: range out of bounds:"
                                   << rangeMm << "mm";
                        return -1;
                    }

                    const double measured = mmToUnit(rangeMm);
                    const double depth = m_totalLength - measured;
                    qDebug() << "MaxbotixSensor: distance =" << measured
                             << "depth =" << depth;
                    return depth;
                }
            } else {
                state = WaitR;
            }
        }
    }

    qWarning() << "MaxbotixSensor: no valid frame within timeout";
    return -1;
#else
    return -1;
#endif
}
