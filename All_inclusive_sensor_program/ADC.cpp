#include "ADC.h"

#include <gpiod.hpp>
#include <thread>
#include <chrono>
#include <iostream>
#include <iomanip>

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
// Static GPIO Objects
//============================================================

static std::unique_ptr<gpiod::chip> chip;
static std::unique_ptr<gpiod::line_request> outputs;
static std::unique_ptr<gpiod::line_request> inputs;

//============================================================
// Pulse Helper
//============================================================

void pulseLine(unsigned pin, int us = 50000)
{
    outputs->set_value(pin, gpiod::line::value::ACTIVE);

    this_thread::sleep_for(
        microseconds(us));

    outputs->set_value(pin, gpiod::line::value::INACTIVE);

    this_thread::sleep_for(
        microseconds(us));
}

//============================================================
// Initialize ADC Hardware
//============================================================

bool initADC()
{
    try
    {
        chip = std::make_unique<gpiod::chip>("/dev/gpiochip0");

        outputs = std::make_unique<gpiod::line_request>(
            chip->prepare_request()
                .set_consumer("adc_control")
                .add_line_settings(
                    {WR_PIN, PS_PIN, CLOCK_PIN},
                    gpiod::line_settings()
                        .set_direction(
                            gpiod::line::direction::OUTPUT))
                .do_request());

        gpiod::line_settings input_settings;

        input_settings
            .set_direction(gpiod::line::direction::INPUT)
            .set_bias(gpiod::line::bias::PULL_DOWN);

        inputs = std::make_unique<gpiod::line_request>(
            chip->prepare_request()
                .set_consumer("adc_inputs")
                .add_line_settings(
                    {DATA1_PIN, DATA2_PIN, DATA3_PIN, DATA4_PIN},
                    input_settings)
                .do_request());

        outputs->set_values({
            {WR_PIN,    gpiod::line::value::INACTIVE},
            {PS_PIN,    gpiod::line::value::INACTIVE},
            {CLOCK_PIN, gpiod::line::value::INACTIVE}
        });

        return true;
    }
    catch (const std::exception& e)
    {
        cerr << "ADC initialization failed: "
             << e.what() << endl;

        return false;
    }
}

//============================================================
// Read All Four ADCs
//============================================================

bool readADC(ADCData& adc)
{
    adc.adc1 = 0;
    adc.adc2 = 0;
    adc.adc3 = 0;
    adc.adc4 = 0;

    //--------------------------------------------------------
    // Start conversion
    //--------------------------------------------------------

    outputs->set_value(
        WR_PIN,
        gpiod::line::value::ACTIVE);

    this_thread::sleep_for(milliseconds(500));

    outputs->set_value(
        WR_PIN,
        gpiod::line::value::INACTIVE);

    this_thread::sleep_for(milliseconds(500));

    //--------------------------------------------------------
    // Parallel load
    //--------------------------------------------------------
    outputs->set_value(
        PS_PIN,
        gpiod::line::value::ACTIVE);

    this_thread::sleep_for(
        microseconds(50000));

    pulseLine(CLOCK_PIN);

    outputs->set_value(
        PS_PIN,
        gpiod::line::value::INACTIVE);

    this_thread::sleep_for(
        microseconds(50000));

    //--------------------------------------------------------
    // Read 8 bits
    //--------------------------------------------------------

    for (int i = 0; i < 8; i++)
    {
        adc.adc1 <<= 1;
        adc.adc2 <<= 1;
        adc.adc3 <<= 1;
        adc.adc4 <<= 1;

        adc.adc1 |= (inputs->get_value(DATA1_PIN)
                     == gpiod::line::value::ACTIVE);

        adc.adc2 |= (inputs->get_value(DATA2_PIN)
                     == gpiod::line::value::ACTIVE);

        adc.adc3 |= (inputs->get_value(DATA3_PIN)
                     == gpiod::line::value::ACTIVE);

        adc.adc4 |= (inputs->get_value(DATA4_PIN)
                     == gpiod::line::value::ACTIVE);

        pulseLine(CLOCK_PIN);
    }

    adc.voltage1 = adc.adc1 * 5.0 / 255.0;
    adc.voltage2 = adc.adc2 * 5.0 / 255.0;
    adc.voltage3 = adc.adc3 * 5.0 / 255.0;
    adc.voltage4 = adc.adc4 * 5.0 / 255.0;

    return true;
}
