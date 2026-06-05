//==================================================
// Pin connections (Pi 5 to MB7389-100 Sensor)
// 5V -> V+ (2 -> 6)
// GND -> GND (6 -> 7)
// GPIO 15 -> Wire serial pin (10 -> 5)
//==================================================

#include <iostream>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cctype>

using namespace std;

//==================================================
// UNIT SELECTION
//==================================================
constexpr bool USE_IMPERIAL = true;

//==================================================
// PIPE DIMENSIONS
//
// Enter these values in the units selected above.
//
// Imperial Example:
// total_length = 48;      // inches
// distance_above_ground = 20;
//
// Metric Example:
// total_length = 1219.2;  // mm
// distance_above_ground = 508;
//==================================================
double total_length = 48;
double distance_above_ground = 20;

double dist_underground =
    total_length - distance_above_ground;

//==================================================
// UART CONFIGURATION
//==================================================
bool configure_uart(int fd)
{
    struct termios tty {};

    if (tcgetattr(fd, &tty) != 0)
    {
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

    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 1;

    return tcsetattr(fd, TCSANOW, &tty) == 0;
}

//==================================================
// MAIN
//==================================================
int main(int argc, char* argv[])
{
    try
    {
        string uart_device = "/dev/ttyAMA0";

        if (argc > 1)
        {
            uart_device = argv[1];
        }

        int uart_fd =
            open(uart_device.c_str(),
                 O_RDWR | O_NOCTTY);

        if (uart_fd < 0)
        {
            cerr << "Unable to open "
                 << uart_device << endl;
            return 1;
        }

        if (!configure_uart(uart_fd))
        {
            cerr << "Unable to configure UART"
                 << endl;

            close(uart_fd);
            return 1;
        }

        const string units =
            USE_IMPERIAL
                ? "inches"
                : "millimeters";

        cout << "MB7389-100 depth monitor started"
             << endl;

        cout << "Reading from "
             << uart_device
             << endl;

        cout << "Units: "
             << units
             << endl;

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
            int bytes_read =
                read(uart_fd, &c, 1);

            if (bytes_read <= 0)
            {
                continue;
            }

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
                        int range_mm =
                            stoi(digits);

                        double measured_distance;

                        if (USE_IMPERIAL)
                        {
                            measured_distance =
                                range_mm / 25.4;
                        }
                        else
                        {
                            measured_distance =
                                range_mm;
                        }

                        double depth =
                            total_length -
                            measured_distance;

                        if (depth < 0)
                        {
                            depth = 0;
                        }

                        cout << "Measured distance: "
                             << measured_distance
                             << " "
                             << units
                             << " | ";

                        cout << "Underground length: "
                             << dist_underground
                             << " "
                             << units
                             << " | ";

                        cout << "Water depth: "
                             << depth
                             << " "
                             << units;

                        if (range_mm <= 500)
                        {
                            cout << " | WARNING: "
                                 << "Target within "
                                 << "minimum range";
                        }
                        else if (range_mm >= 5000)
                        {
                            cout << " | WARNING: "
                                 << "No target detected";
                        }

                        cout << endl;

                        state = WaitR;
                    }
                }
                else
                {
                    // malformed frame
                    state = WaitR;
                }

                break;
            }
        }

        close(uart_fd);
    }
    catch (const exception& e)
    {
        cerr << "Error: "
             << e.what()
             << endl;

        return 1;
    }

    return 0;
}
