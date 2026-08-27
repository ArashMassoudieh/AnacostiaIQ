#include <gpiod.hpp>
#include <chrono>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;

constexpr unsigned int OUT_PIN = 17;
constexpr unsigned int IN_PIN  = 18;

int main()
{
    try {
        gpiod::chip chip("/dev/gpiochip0");

        auto out_req = chip.prepare_request()
                           .set_consumer("gpio-loopback-out")
                           .add_line_settings(
                               OUT_PIN,
                               gpiod::line_settings().set_direction(
                                   gpiod::line::direction::OUTPUT))
                           .do_request();

        auto in_req = chip.prepare_request()
                          .set_consumer("gpio-loopback-in")
                          .add_line_settings(
                              IN_PIN,
                              gpiod::line_settings().set_direction(
                                  gpiod::line::direction::INPUT))
                          .do_request();

        std::cout << "GPIO loopback test\n"
                  << "Temporarily disconnect the HC-SR04 from GPIO17/GPIO18,\n"
                  << "then jumper physical pin 11 (GPIO17) directly to physical pin 12 (GPIO18).\n"
                  << "Do NOT connect 5 V to either GPIO for this test.\n\n";

        bool ok = true;

        for (int cycle = 1; cycle <= 10; ++cycle)
        {
            out_req.set_value(OUT_PIN, gpiod::line::value::INACTIVE);
            std::this_thread::sleep_for(100ms);
            const auto low = in_req.get_value(IN_PIN);

            out_req.set_value(OUT_PIN, gpiod::line::value::ACTIVE);
            std::this_thread::sleep_for(100ms);
            const auto high = in_req.get_value(IN_PIN);

            const bool low_ok = (low == gpiod::line::value::INACTIVE);
            const bool high_ok = (high == gpiod::line::value::ACTIVE);

            std::cout << "Cycle " << cycle
                      << ": LOW read=" << (low_ok ? "LOW" : "HIGH")
                      << ", HIGH read=" << (high_ok ? "HIGH" : "LOW")
                      << (low_ok && high_ok ? "  PASS" : "  FAIL")
                      << '\n';

            if (!low_ok || !high_ok)
                ok = false;
        }

        out_req.set_value(OUT_PIN, gpiod::line::value::INACTIVE);

        std::cout << '\n'
                  << (ok ? "LOOPBACK PASS: GPIO17 output and GPIO18 input path are working.\n"
                         : "LOOPBACK FAIL: check jumper, header pins, or GPIO hardware/configuration.\n");

        return ok ? 0 : 2;
    }
    catch (const std::exception &e)
    {
        std::cerr << "GPIO loopback error: " << e.what() << '\n';
        return 1;
    }
}
