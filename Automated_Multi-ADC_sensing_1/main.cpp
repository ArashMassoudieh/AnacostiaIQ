//============================================================
// Automated Multi-ADC Sensing
//
// Raspberry Pi 5
// ADC0804 + CD4014
//
// Four ADC0804/CD4014 circuits share:
//
// WR      -> GPIO2
// P/S     -> GPIO27
// CLOCK   -> GPIO4
//
// Data Outputs:
// ADC #1 Q8 -> GPIO 5
// ADC #2 Q8 -> GPIO 6
// ADC #3 Q8 -> GPIO 13
// ADC #4 Q8 -> GPIO 19
//============================================================

#include <gpiod.hpp>

#include <iostream>
#include <iomanip>

#include <thread>
#include <chrono>

#include <cstdint>

using namespace std;
using namespace std::chrono;

//============================================================
// GPIO Definitions
//============================================================

constexpr unsigned WR_PIN    = 2;
constexpr unsigned PS_PIN    = 27;
constexpr unsigned CLOCK_PIN = 4;

constexpr unsigned DATA1_PIN = 5;
constexpr unsigned DATA2_PIN = 6;
constexpr unsigned DATA3_PIN = 13;
constexpr unsigned DATA4_PIN = 19;

//============================================================
// Pulse helper
//============================================================

void pulseLine(gpiod::line_request& req,
               unsigned pin,
               int us = 50000)
{
    req.set_value(pin, gpiod::line::value::ACTIVE);

    std::this_thread::sleep_for(
        std::chrono::microseconds(us));

    req.set_value(pin, gpiod::line::value::INACTIVE);

    std::this_thread::sleep_for(
        std::chrono::microseconds(us));
}
//============================================================
// Main
//============================================================

int main()
{
    try
    {
        //----------------------------------------------------
        // Open GPIO chip
        //----------------------------------------------------

        gpiod::chip chip("/dev/gpiochip4");

        //----------------------------------------------------
        // Output GPIOs
        //----------------------------------------------------

        auto outputs =
            chip.prepare_request()
                .set_consumer("adc_control")
                .add_line_settings(
                    {
                        WR_PIN,
                        PS_PIN,
                        CLOCK_PIN
                    },
                    gpiod::line_settings()
                        .set_direction(
                            gpiod::line::direction::OUTPUT))
                .do_request();

        outputs.set_values({
            {WR_PIN,    gpiod::line::value::INACTIVE},
            {PS_PIN,    gpiod::line::value::INACTIVE},
            {CLOCK_PIN, gpiod::line::value::INACTIVE}
        });

        //----------------------------------------------------
        // Input GPIOs
        //----------------------------------------------------

        gpiod::line_settings input_settings;

        input_settings
            .set_direction(gpiod::line::direction::INPUT)
            .set_bias(gpiod::line::bias::PULL_DOWN);

        auto inputs =
            chip.prepare_request()
                .set_consumer("adc_inputs")
                .add_line_settings(
                  {
                    DATA1_PIN,
                    DATA2_PIN,
                    DATA3_PIN,
                    DATA4_PIN
                },
                input_settings)
                .do_request();


        //----------------------------------------------------
        // Initialize outputs
        //----------------------------------------------------

        outputs.set_value(
            WR_PIN,
            gpiod::line::value::INACTIVE);

        outputs.set_value(
            PS_PIN,
            gpiod::line::value::INACTIVE);
        this_thread::sleep_for(microseconds(50000));

        outputs.set_value(
            CLOCK_PIN,
            gpiod::line::value::INACTIVE);

        cout << endl;
        cout << "==========================================" << endl;
        cout << " Four ADC0804 Parallel Acquisition Ready" << endl;
        cout << "==========================================" << endl;

        cout << "\nGPIO idle states\n";

        cout << "ADC1: "
             << (inputs.get_value(DATA1_PIN) ==
                 gpiod::line::value::ACTIVE)
             << endl;

        cout << "ADC2: "
             << (inputs.get_value(DATA2_PIN) ==
                 gpiod::line::value::ACTIVE)
             << endl;

        cout << "ADC3: "
             << (inputs.get_value(DATA3_PIN) ==
                 gpiod::line::value::ACTIVE)
             << endl;

        cout << "ADC4: "
             << (inputs.get_value(DATA4_PIN) ==
                 gpiod::line::value::ACTIVE)
             << endl;



        //----------------------------------------------------
        // Storage for ADC values
        //----------------------------------------------------

        uint8_t adcValue1;
        uint8_t adcValue2;
        uint8_t adcValue3;
        uint8_t adcValue4;

        while (true)
        {
            //------------------------------------------------
            // Clear previous readings
            //------------------------------------------------

            adcValue1 = 0;
            adcValue2 = 0;
            adcValue3 = 0;
            adcValue4 = 0;

            //------------------------------------------------
            // Start ADC conversions
            //------------------------------------------------

            cout << endl;
            cout << "Starting conversion..." << endl;

            //--------------------------------------------------
            // Start ADC conversion
            //--------------------------------------------------


            outputs.set_value(
                WR_PIN,
                gpiod::line::value::ACTIVE);

            std::cout << "WR HIGH\n";

            std::this_thread::sleep_for(
                std::chrono::milliseconds(500));

            outputs.set_value(
                WR_PIN,
                gpiod::line::value::INACTIVE);

            std::cout << "WR LOW\n";

            std::this_thread::sleep_for(
                std::chrono::milliseconds(500));


            //--------------------------------------------------
            // Parallel load
            //--------------------------------------------------

            // Load ADC outputs into the CD4014

            outputs.set_value(
                PS_PIN,
                gpiod::line::value::ACTIVE);

            std::this_thread::sleep_for(
                std::chrono::microseconds(50000));

            // Latch the parallel inputs
            pulseLine(outputs, CLOCK_PIN);

            outputs.set_value(
                PS_PIN,
                gpiod::line::value::INACTIVE);

            std::this_thread::sleep_for(
                std::chrono::microseconds(50000));

            //------------------------------------------------
            // Shifting begins here...
            //------------------------------------------------
            //------------------------------------------------
            // Read all four ADCs simultaneously
            //------------------------------------------------

            cout << endl;
            cout << "Bits:" << endl;

            for (int i = 0; i < 8; i++)
            {
                //------------------------------------------------
                // Read one bit from each CD4014
                //------------------------------------------------

                int bit1 =
                    (inputs.get_value(DATA1_PIN)
                     == gpiod::line::value::ACTIVE);

                int bit2 =
                    (inputs.get_value(DATA2_PIN)
                     == gpiod::line::value::ACTIVE);

                int bit3 =
                    (inputs.get_value(DATA3_PIN)
                     == gpiod::line::value::ACTIVE);

                int bit4 =
                    (inputs.get_value(DATA4_PIN)
                     == gpiod::line::value::ACTIVE);

                cout
                    << bit1
                    << " "
                    << bit2
                    << " "
                    << bit3
                    << " "
                    << bit4
                    << endl;

                //------------------------------------------------
                // Shift previous bits left
                //------------------------------------------------

                adcValue1 <<= 1;
                adcValue2 <<= 1;
                adcValue3 <<= 1;
                adcValue4 <<= 1;

                //------------------------------------------------
                // Store newest bit
                //------------------------------------------------

                adcValue1 |= bit1;
                adcValue2 |= bit2;
                adcValue3 |= bit3;
                adcValue4 |= bit4;

                //------------------------------------------------
                // Advance every CD4014 together
                //------------------------------------------------

                pulseLine(outputs, CLOCK_PIN);
            }

            //------------------------------------------------
            // Convert to voltages
            //------------------------------------------------

            double voltage1 =
                adcValue1 * 5.0 / 255.0;

            double voltage2 =
                adcValue2 * 5.0 / 255.0;

            double voltage3 =
                adcValue3 * 5.0 / 255.0;

            double voltage4 =
                adcValue4 * 5.0 / 255.0;

            //------------------------------------------------
            // Display results
            //------------------------------------------------

            cout << endl;
            cout << "==========================================" << endl;

            cout
                << fixed
                << setprecision(3);

            cout
                << "ADC 1 : "
                << setw(3)
                << static_cast<int>(adcValue1)
                << "    "
                << voltage1
                << " V"
                << endl;

            cout
                << "ADC 2 : "
                << setw(3)
                << static_cast<int>(adcValue2)
                << "    "
                << voltage2
                << " V"
                << endl;

            cout
                << "ADC 3 : "
                << setw(3)
                << static_cast<int>(adcValue3)
                << "    "
                << voltage3
                << " V"
                << endl;

            cout
                << "ADC 4 : "
                << setw(3)
                << static_cast<int>(adcValue4)
                << "    "
                << voltage4
                << " V"
                << endl;

            cout << "==========================================" << endl;

            //------------------------------------------------
            // Wait before next acquisition
            //------------------------------------------------

            this_thread::sleep_for(
                seconds(1));
        }   // End while(true)
    }
    catch (const std::exception& e)
    {
        std::cerr
            << "ERROR: "
            << e.what()
            << std::endl;

        return 1;
    }

    return 0;
}
