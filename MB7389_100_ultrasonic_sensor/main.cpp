//==================================================
// Pin connections (Pi 5 to MB7389-100 Sensor)
// 5V -> V+ (2 -> 6)
// GND -> GND (6 -> 7)
// GPIO 15 -> Wire serial pin (10 -> 5)
//==================================================

#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

using namespace std;
using namespace std::chrono;

//==================================================
// UNIT SELECTION
//==================================================
constexpr bool USE_IMPERIAL = false;

//==================================================
// PIPE DIMENSIONS
// Enter values in the units selected above.
//==================================================
double total_length = 700;
double distance_above_ground = 200;
double dist_underground =
    total_length - distance_above_ground;

//==================================================
// UART SETUP
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
    tty.c_cflag |= CREAD | CLOCAL;

    tty.c_lflag = 0;
    tty.c_iflag = 0;
    tty.c_oflag = 0;

    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 1;

    return tcsetattr(fd, TCSANOW, &tty) == 0;
}

int main()
{
    try
    {
        int uart_fd =
            open("/dev/ttyAMA0",
                 O_RDWR | O_NOCTTY);

        if (uart_fd < 0)
        {
            cerr << "Unable to open UART\n";
            return 1;
        }

        if (!configure_uart(uart_fd))
        {
            cerr << "Unable to configure UART\n";
            close(uart_fd);
            return 1;
        }

        const string units =
            USE_IMPERIAL ? "inches" : "millimeters";

        cout << "MB7389-100 depth monitor started\n";
        cout << "Units: " << units << "\n";

        string frame;
        char c;

        for (;;)
        {
            int bytes_read =
                read(uart_fd, &c, 1);

            if (bytes_read <= 0)
            {
                continue;
            }

            // MB7389 frame ends with CR
            if (c == '\r' || c == '\n')
            {
                if (!frame.empty())
                {
                    if (frame[0] == 'R')
                    {
                        try
                        {
                            int range_mm =
                                stoi(frame.substr(1));

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
                                 << " " << units
                                 << " | ";

                            cout << "Underground length: "
                                 << dist_underground
                                 << " " << units
                                 << " | ";

                            cout << "Water depth: "
                                 << depth
                                 << " " << units
                                 << endl;
                        }
                        catch (...)
                        {
                            cerr << "Invalid frame: "
                                 << frame
                                 << endl;
                        }
                    }

                    frame.clear();
                }
            }
            else
            {
                frame += c;
            }
        }

        close(uart_fd);
    }
    catch (const exception& e)
    {
        cerr << "Error: "
             << e.what()
             << '\n';
        return 1;
    }

    return 0;
}
