#include <gpiod.hpp>
#include <thread>
#include <chrono>
#include <iostream>

using namespace std;
using namespace std::chrono;

double total_length=300; //length of pipe in cm
double distance_above_ground=50;//distance above the level of the ground in cm. should be more than 50 cm
double dist_underground=total_length-distance_above_ground;//distance undergound in cm

// Wait for a GPIO line to reach a target state with timeout
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

        // Prevent 100% CPU busy-waiting
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

        cout << "HC-SR04 distance monitor started\n";

        for (;;)
        {
            // Ensure TRIG starts LOW
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

            // Distance in cm
            double two_way_distance = duration * 0.0343;

            //Depth in cm
            double depth = dist_underground-two_way_distance/2;

            cout << "Depth: "
                 << depth
                 << " cm" << endl;

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
