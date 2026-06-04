#include <gpiod.hpp>
#include <thread>
#include <chrono>
#include <iostream>

int main()
{
    try {
        gpiod::chip chip("/dev/gpiochip0");
        auto req = chip.prepare_request()
                       .set_consumer("blink")
                       .add_line_settings(17, gpiod::line_settings()
                                                  .set_direction(gpiod::line::direction::OUTPUT))
                       .do_request();

        for (bool on = true; ; on = !on)
        {
            req.set_value(17, on ? gpiod::line::value::ACTIVE
                                 : gpiod::line::value::INACTIVE);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
