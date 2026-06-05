//====================================
// Pin connections (PIN NUMBERS: Pi 5 -> HC-SR04)
// 5V -> VCC (2 -> 1)
// GPIO 17 -> TRIG (11 -> 2)
// GPIO 27 -> ECHO (13 -> 3)
// GND -> GND (6 -> 4)
//====================================

#include <gpiod.hpp>
#include <thread>
#include <chrono>
#include <iostream>

using namespace std;
using namespace std::chrono;

// Pipe dimensions in inches
double total_length = 48;
double distance_above_ground = 20;
double dist_underground =total_length - distance_above_ground;

// Speed of sound:
// 0.0135 inches per microsecond
const double SOUND_SPEED_IN_PER_US = 0.0135;

// Wait for GPIO line state with timeout
bool wait_for_state(gpiod::line_request& req,
                    gpiod::line::value target,
                    unsigned int line,
                    milliseconds timeout)
{
    auto start = steady_clock::now();

    while (req.get_value(line) != target)
    {
        if (steady_clock::now() - start > timeout)
        {
            return false;
        }

        // Reduce CPU usage
        this_thread::sleep_for(microseconds(10));
    }

    return true;
}

int main()
{
    try {
        // Open GPIO chip
        gpiod::chip chip("/dev/gpiochip0");

        // Configure TRIG as output
        auto trig_req = chip.prepare_request()
                            .set_consumer("hc-sr04-trig")
                            .add_line_settings(
                                17,
                                gpiod::line_settings()
                                    .set_direction(
                                        gpiod::line::direction::OUTPUT))
                            .do_request();

        // Configure ECHO as input
        auto echo_req = chip.prepare_request()
                            .set_consumer("hc-sr04-echo")
                            .add_line_settings(
                                27,
                                gpiod::line_settings()
                                    .set_direction(
                                        gpiod::line::direction::INPUT))
                            .do_request();

        cout << "HC-SR04 depth monitor started\n";

        for (;;)
        {
            // Ensure TRIG LOW
            trig_req.set_value(17, gpiod::line::value::INACTIVE);
            this_thread::sleep_for(microseconds(2));

            // Send 10 us trigger pulse
            trig_req.set_value(17, gpiod::line::value::ACTIVE);
            this_thread::sleep_for(microseconds(10));
            trig_req.set_value(17, gpiod::line::value::INACTIVE);

            // Wait for ECHO HIGH
            if (!wait_for_state(echo_req,
                                gpiod::line::value::ACTIVE,
                                27,
                                milliseconds(50)))
            {
                cerr << "Timeout waiting for ECHO HIGH\n";
                this_thread::sleep_for(milliseconds(100));
                continue;
            }

            auto start = steady_clock::now();

            // Wait for ECHO LOW
            if (!wait_for_state(echo_req,
                                gpiod::line::value::INACTIVE,
                                27,
                                milliseconds(50)))
            {
                cerr << "Timeout waiting for ECHO LOW\n";
                this_thread::sleep_for(milliseconds(100));
                continue;
            }

            auto end = steady_clock::now();

            // Pulse duration in microseconds
            auto duration =
                duration_cast<microseconds>(end - start).count();

            // Total round-trip distance in inches
            double two_way_distance =
                duration * SOUND_SPEED_IN_PER_US;

            // One-way measured distance
            double measured_distance =
                two_way_distance / 2.0;

            // Remaining underground depth
            double depth =total_length-measured_distance ;

            cout << "Measured distance: "
                 << measured_distance
                 << " inches | ";

            cout << "Underground length: "
                 << dist_underground
                 << " inches | ";

            cout << "Water Depth: "
                 << depth
                 << " inches" << endl;

            // Delay between measurements
            this_thread::sleep_for(milliseconds(500));
        }
    }
    catch (const std::exception& e)
    {
        cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
