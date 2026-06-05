#include <gpiod.hpp>
#include <thread>
#include <chrono>
#include <iostream>

using namespace std;
using namespace std::chrono;

int main()
{
    try {
        gpiod::chip chip("/dev/gpiochip0");

        // Configure TRIG as output
        auto trig_req = chip.prepare_request()
                            .set_consumer("hc-sr04-trig")
                            .add_line_settings(
                                17,
                                gpiod::line_settings()
                                    .set_direction(gpiod::line::direction::OUTPUT))
                            .do_request();

        // Configure ECHO as input
        auto echo_req = chip.prepare_request()
                            .set_consumer("hc-sr04-echo")
                            .add_line_settings(
                                27,
                                gpiod::line_settings()
                                    .set_direction(gpiod::line::direction::INPUT))
                            .do_request();

        for (;;)
        {
            // Ensure TRIG starts low
            trig_req.set_value(17, gpiod::line::value::INACTIVE);
            this_thread::sleep_for(microseconds(2));

            // Send 10 us trigger pulse
            trig_req.set_value(17, gpiod::line::value::ACTIVE);
            this_thread::sleep_for(microseconds(10));
            trig_req.set_value(17, gpiod::line::value::INACTIVE);

            // Wait for ECHO to go HIGH
            while (echo_req.get_value(27) ==
                   gpiod::line::value::INACTIVE);

            auto start = high_resolution_clock::now();

            // Wait for ECHO to go LOW
            while (echo_req.get_value(27) ==
                   gpiod::line::value::ACTIVE);

            auto end = high_resolution_clock::now();

            // Calculate pulse duration
            auto duration =
                duration_cast<microseconds>(end - start).count();

            // Distance in cm
            double distance = duration * 0.0343 / 2.0;

            cout << "Distance: "
                 << distance
                 << " cm" << endl;

            this_thread::sleep_for(milliseconds(500));
        }
    }
    catch (const std::exception& e) {
        cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
