#include "HCSR04.h"

#include <gpiod.hpp>
#include <thread>
#include <chrono>
#include <cmath>
#include <iostream>

#include <vector>
#include <algorithm>
#include <cmath>

using namespace std;
using namespace std::chrono;

//============================================================
// GPIO Configuration
//============================================================

constexpr unsigned int TRIG_PIN = 14;
constexpr unsigned int ECHO_PIN = 18;

//============================================================
// Pipe Dimensions
//============================================================

static constexpr double total_length = 11.0 + 13.0 / 16.0;
static constexpr double distance_above_ground = total_length;
static constexpr double dist_underground =
    total_length - distance_above_ground;

static constexpr double SOUND_SPEED_IN_PER_US = 0.0135;

static constexpr double gravity = 32.174;
static constexpr double Cd = 0.25;

static constexpr double theta_deg = 59.4 / 2.0;
static constexpr double theta_rad =
    theta_deg * M_PI / 180.0;

// calibration offset (feet)
static constexpr double k =
    (3.0 + 5.0 / 16.0) / 12.0;

//============================================================
// Static GPIO Objects
//============================================================

static std::unique_ptr<gpiod::chip> chip;
static std::unique_ptr<gpiod::line_request> trig_req;
static std::unique_ptr<gpiod::line_request> echo_req;

//============================================================
// Wait for GPIO State
//============================================================

static bool wait_for_state(
    gpiod::line_request& req,
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

        this_thread::sleep_for(microseconds(10));
    }

    return true;
}

static double getAverageDistance(
    gpiod::line_request& trig_req,
    gpiod::line_request& echo_req)
{
    constexpr int NUM_SAMPLES = 100;
    constexpr double CLUSTER_WIDTH = 0.05;

    std::vector<double> samples;

    for (int i = 0; i < NUM_SAMPLES; i++)
    {
        trig_req.set_value(TRIG_PIN,
                           gpiod::line::value::INACTIVE);
        this_thread::sleep_for(microseconds(10));

        trig_req.set_value(TRIG_PIN,
                           gpiod::line::value::ACTIVE);
        this_thread::sleep_for(microseconds(10));

        trig_req.set_value(TRIG_PIN,
                           gpiod::line::value::INACTIVE);

        if (!wait_for_state(
                echo_req,
                gpiod::line::value::ACTIVE,
                ECHO_PIN,
                milliseconds(50)))
            continue;

        auto start = steady_clock::now();

        if (!wait_for_state(
                echo_req,
                gpiod::line::value::INACTIVE,
                ECHO_PIN,
                milliseconds(50)))
            continue;

        auto end = steady_clock::now();

        auto duration =
            duration_cast<microseconds>(
                end - start).count();

        double distance =
            duration * SOUND_SPEED_IN_PER_US / 2.0;

        samples.push_back(distance);

        this_thread::sleep_for(milliseconds(20));
    }

    if (samples.size() < 5)
        return NAN;

    std::sort(samples.begin(), samples.end());

    size_t bestStart = 0;
    size_t bestCount = 1;

    for (size_t i = 0; i < samples.size(); i++)
    {
        size_t count = 1;

        for (size_t j = i + 1;
             j < samples.size();
             j++)
        {
            if (samples[j] - samples[i]
                <= CLUSTER_WIDTH)
                count++;
            else
                break;
        }

        if (count > bestCount)
        {
            bestCount = count;
            bestStart = i;
        }
    }

    double sum = 0;

    for (size_t i = bestStart;
         i < bestStart + bestCount;
         i++)
    {
        sum += samples[i];
    }

    return sum / bestCount;
}

//============================================================
// Initialize Sensor
//============================================================

bool initHCSR04()
{
    try
    {
        chip = std::make_unique<gpiod::chip>("/dev/gpiochip0");

        trig_req = std::make_unique<gpiod::line_request>(
            chip->prepare_request()
                .set_consumer("hc-sr04-trig")
                .add_line_settings(
                    TRIG_PIN,
                    gpiod::line_settings()
                        .set_direction(
                            gpiod::line::direction::OUTPUT))
                .do_request());

        echo_req = std::make_unique<gpiod::line_request>(
            chip->prepare_request()
                .set_consumer("hc-sr04-echo")
                .add_line_settings(
                    ECHO_PIN,
                    gpiod::line_settings()
                        .set_direction(
                            gpiod::line::direction::INPUT))
                .do_request());

        trig_req->set_value(
            TRIG_PIN,
            gpiod::line::value::INACTIVE);

        return true;
    }
    catch (const std::exception& e)
    {
        cerr << "HC-SR04 initialization failed: "
             << e.what() << endl;
        return false;
    }
}

//============================================================
// Read Sensor
//============================================================

bool readHCSR04(HCSR04Data& data)
{
    double measured =
        getAverageDistance(*trig_req, *echo_req);

    if (std::isnan(measured))
        return false;

    data.measuredDistance = measured;

    data.depth =
        total_length - data.measuredDistance;

    if (data.depth < 0)
        data.depth = 0;

    double H = data.depth / 12.0 - k;

    if (H < 0)
        H = 0;

    double flow =
        (8.0 / 15.0) *
        Cd *
        sqrt(2.0 * gravity) *
        tan(theta_rad) *
        pow(H, 2.5);

    data.flowRate =
        flow * 7.48052 * 60.0;

    if (flow < 0.001)
        flow = 0;

    return true;
}
