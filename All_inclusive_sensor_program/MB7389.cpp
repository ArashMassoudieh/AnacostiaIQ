#include "MB7389.h"

#include <iostream>
#include <string>
#include <memory>
#include <thread>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cctype>

#include <gpiod.hpp>

using namespace std;

//==================================================
// GPIO Configuration
//==================================================

constexpr unsigned TRIGGER_PIN = 25;

//==================================================
// Unit Selection
//==================================================

constexpr bool USE_IMPERIAL = true;

//==================================================
// Tank Dimensions
//==================================================

static constexpr double total_length = 48.0;

//==================================================
// Static Variables
//==================================================

static int uart_fd = -1;

static std::unique_ptr<gpiod::chip> chip;
static std::unique_ptr<gpiod::line_request> trigger;

//==================================================
// Configure UART
//==================================================

static bool configureUART(int fd)
{
    struct termios tty{};

    if (tcgetattr(fd, &tty) != 0)
        return false;

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

    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 1;

    return (tcsetattr(fd, TCSANOW, &tty) == 0);
}

//==================================================
// Initialize Sensor
//==================================================

bool initMB7389(const std::string& device)
{
    uart_fd = open(device.c_str(), O_RDWR | O_NOCTTY);

    if (uart_fd < 0)
    {
        cerr << "Unable to open " << device << endl;
        return false;
    }

    if (!configureUART(uart_fd))
    {
        cerr << "Unable to configure UART" << endl;
        close(uart_fd);
        uart_fd = -1;
        return false;
    }

    tcflush(uart_fd, TCIFLUSH);

    try
    {
        chip = std::make_unique<gpiod::chip>("/dev/gpiochip0");

        trigger = std::make_unique<gpiod::line_request>(
            chip->prepare_request()
                .set_consumer("mb7389_trigger")
                .add_line_settings(
                    TRIGGER_PIN,
                    gpiod::line_settings()
                        .set_direction(gpiod::line::direction::OUTPUT))
                .do_request());

        // RX HIGH = Free-run
        trigger->set_value(TRIGGER_PIN,
                           gpiod::line::value::ACTIVE);
    }
    catch (const std::exception& e)
    {
        cerr << "GPIO initialization failed: "
             << e.what() << endl;
        return false;
    }

    return true;
}

//==================================================
// Read One Measurement
//==================================================

bool readMB7389(MB7389Data& data)
{
    // Clear any old serial data
    tcflush(uart_fd, TCIFLUSH);

    // Pull RX LOW
    trigger->set_value(TRIGGER_PIN,
                       gpiod::line::value::INACTIVE);

    std::this_thread::sleep_for(
        std::chrono::milliseconds(145));

    // Bring RX HIGH to begin a new measurement
    trigger->set_value(TRIGGER_PIN,
                       gpiod::line::value::ACTIVE);

    // Wait for measurement to complete
    std::this_thread::sleep_for(
        std::chrono::milliseconds(145));

    enum State
    {
        WaitR,
        ReadDigits
    };

    State state = WaitR;
    string digits;
    char c;

    while (true)
    {
        int bytes = read(uart_fd, &c, 1);

        if (bytes <= 0)
            continue;

        switch (state)
        {
        case WaitR:

            if (c == 'R')
            {
                digits.clear();
                state = ReadDigits;
            }

            break;

        case ReadDigits:

            if (isdigit(static_cast<unsigned char>(c)))
            {
                digits += c;

                if (digits.size() == 4)
                {
                    data.rangeMM = stoi(digits);

                    if (USE_IMPERIAL)
                        data.measuredDistance =
                            data.rangeMM / 25.4;
                    else
                        data.measuredDistance =
                            data.rangeMM;

                    data.depth =
                        total_length -
                        data.measuredDistance;

                    if (data.depth < 0.0)
                        data.depth = 0.0;

                    data.targetTooClose =
                        (data.rangeMM <= 500);

                    data.noTarget =
                        (data.rangeMM >= 5000);

                    return true;
                }
            }
            else
            {
                state = WaitR;
            }

            break;
        }
    }
}

//==================================================
// Cleanup
//==================================================

void shutdownMB7389()
{
    trigger.reset();
    chip.reset();

    if (uart_fd >= 0)
    {
        close(uart_fd);
        uart_fd = -1;
    }
}
