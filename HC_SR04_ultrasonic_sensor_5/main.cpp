#include <gpiod.hpp>
#include <thread>
#include <chrono>
#include <iostream>
#include <math.h>

#include <array>
#include <algorithm>
#include <numeric>

using namespace std;
using namespace std::chrono;

//============================================================
// GPIO Configuration
//============================================================

//constexpr unsigned int TRIG_PIN = 23;
//constexpr unsigned int ECHO_PIN = 24;

// TRIG was GPIO14 (UART0 TXD). Once UART0 is enabled on the Pi for the
// MB7389-100 sensor, GPIO14/15 conflict with the UART peripheral.
// GPIO17 is free in this pin map and is used for the HC-SR04 trigger.
constexpr unsigned int TRIG_PIN = 17;
constexpr unsigned int ECHO_PIN = 18;

// Pipe dimensions in inches
double total_length = 11.0 + 13.0/16.0; //calibrated effective height
double distance_above_ground = total_length;
double dist_underground = total_length - distance_above_ground;

double k = (3.0 + 5.0/16.0) / 12.0;

// Speed of sound:
// 0.0135 inches per microsecond
const double SOUND_SPEED_IN_PER_US = 0.0135;
//22.95 sec per liter
double gravity = 32.174;          // ft/s²
double Cd = 0.25;                // Typical discharge coefficient
double theta_deg = 59.4/2;
double theta_rad = theta_deg * M_PI / 180.0;



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

double getAverageDistance(gpiod::line_request& trig_req,
                          gpiod::line_request& echo_req)
{
    // Keep this diagnostic test short so a disconnected/miswired sensor
    // reports its failure mode quickly instead of waiting through 100 pings.
    constexpr int NUM_SAMPLES = 5;
    constexpr double CLUSTER_WIDTH = 0.25;   // inches

    std::vector<double> samples;

    for (int i = 0; i < NUM_SAMPLES; i++)
    {
        // Ensure TRIG LOW
        trig_req.set_value(TRIG_PIN, gpiod::line::value::INACTIVE);
        std::this_thread::sleep_for(std::chrono::microseconds(10));

        // Send 10 µs trigger pulse
        trig_req.set_value(TRIG_PIN, gpiod::line::value::ACTIVE);
        std::this_thread::sleep_for(std::chrono::microseconds(10));
        trig_req.set_value(TRIG_PIN, gpiod::line::value::INACTIVE);

        // Wait for ECHO HIGH
        if (!wait_for_state(echo_req,
                            gpiod::line::value::ACTIVE,
                            ECHO_PIN,
                            std::chrono::milliseconds(50)))
        {
            std::cerr << "Sample " << (i + 1)
                      << ": timeout waiting for ECHO HIGH\n";
            continue;
        }

        auto start = std::chrono::steady_clock::now();

        // Wait for ECHO LOW
        if (!wait_for_state(echo_req,
                            gpiod::line::value::INACTIVE,
                            ECHO_PIN,
                            std::chrono::milliseconds(50)))
        {
            std::cerr << "Sample " << (i + 1)
                      << ": timeout waiting for ECHO LOW\n";
            continue;
        }

        auto end = std::chrono::steady_clock::now();

        auto duration =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        double distance = (duration * SOUND_SPEED_IN_PER_US) / 2.0;

        samples.push_back(distance);

        std::cout << "Sample " << (i + 1)
                  << ": pulse width = " << duration
                  << " us, distance = " << distance
                  << " in" << std::endl;

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    if (samples.empty())
        return NAN;

    std::sort(samples.begin(), samples.end());

    // Find the largest cluster
    size_t bestStart = 0;
    size_t bestCount = 1;

    for (size_t i = 0; i < samples.size(); i++)
    {
        size_t count = 1;

        for (size_t j = i + 1; j < samples.size(); j++)
        {
            if (samples[j] - samples[i] <= CLUSTER_WIDTH)
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

    double sum = 0.0;

    for (size_t i = bestStart; i < bestStart + bestCount; i++)
        sum += samples[i];

    double average = sum / bestCount;

    std::cout << "Cluster average = "
              << average
              << " in (" << bestCount
              << " samples)" << std::endl;

    return average;
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
                                TRIG_PIN,
                                gpiod::line_settings()
                                    .set_direction(
                                        gpiod::line::direction::OUTPUT))
                            .do_request();

        // Configure ECHO as input
        auto echo_req = chip.prepare_request()
                            .set_consumer("hc-sr04-echo")
                            .add_line_settings(
                                ECHO_PIN,
                                gpiod::line_settings()
                                    .set_direction(
                                        gpiod::line::direction::INPUT))
                            .do_request();

        cout << "HC-SR04 depth monitor started\n";
        cout << "TRIG=GPIO" << TRIG_PIN
             << ", ECHO=GPIO" << ECHO_PIN << '\n';

        for (;;)
        {
            double measured_distance = getAverageDistance(trig_req, echo_req);

            if (std::isnan(measured_distance))
            {
                std::cerr << "Measurement failed: no valid ECHO pulse detected\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }

            // Remaining underground depth
            double depth = total_length - measured_distance;
            if (depth<0){
                depth=0;}

            // Water head above notch (convert inches to feet)
            double H=(depth)/12-k;
            if (H<0){
                H=0;}

            double Flow_Rate =(8.0/15.0)*Cd*sqrt(2.0*gravity)*tan(theta_rad)*pow(H,2.5); // In Ft^3/sec

            if (Flow_Rate<0.001){
                Flow_Rate=0;}

            double gps = Flow_Rate * 7.48052*60;// gallons/min

            cout << "\n";
            cout << "Measured Distance = " << measured_distance << '\n';
            cout << "Total Length      = " << total_length << '\n';
            cout << "Depth             = " << depth << '\n';
            cout << "k (ft)            = " << k << '\n';
            cout << "Head H (ft)       = " << H << '\n';

            cout << "Flow Rate: "
                 << gps
                 << " Gallons/Min" << endl;


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
