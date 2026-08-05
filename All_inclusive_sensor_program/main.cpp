#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>

#include "ADC.h"
#include "HCSR04.h"
#include "MB7389.h"

using namespace std;
using namespace std::chrono;

int main()
{
    //--------------------------------------------------------
    // Initialize Sensors
    //--------------------------------------------------------

    if (!initADC())
    {
        cerr << "Failed to initialize ADC module." << endl;
        return 1;
    }

    if (!initHCSR04())
    {
        cerr << "Failed to initialize HC-SR04 module." << endl;
        return 1;
    }

    if (!initMB7389())
    {
        cerr << "Failed to initialize MB7389 module." << endl;
        return 1;
    }

    cout << "\n==========================================" << endl;
    cout << "      Multi-Sensor Monitoring System" << endl;
    cout << "==========================================" << endl;

    ADCData adc;
    HCSR04Data ultrasonic;
    MB7389Data maxbotix;

    //--------------------------------------------------------
    // Main Loop
    //--------------------------------------------------------

    while (true)
    {
        bool adcOK = readADC(adc);
        bool usOK = readHCSR04(ultrasonic);
        bool mbOK = readMB7389(maxbotix);

        cout << "\n==========================================" << endl;

        //---------------- ADC ----------------

        if (adcOK)
        {
            cout << fixed << setprecision(3);

            cout << "ADC 1 : "
                 << setw(3) << static_cast<int>(adc.adc1)
                 << "   "
                 << adc.voltage1 << " V" << endl;

            cout << "ADC 2 : "
                 << setw(3) << static_cast<int>(adc.adc2)
                 << "   "
                 << adc.voltage2 << " V" << endl;

            cout << "ADC 3 : "
                 << setw(3) << static_cast<int>(adc.adc3)
                 << "   "
                 << adc.voltage3 << " V" << endl;

            cout << "ADC 4 : "
                 << setw(3) << static_cast<int>(adc.adc4)
                 << "   "
                 << adc.voltage4 << " V" << endl;
        }
        else
        {
            cout << "ADC Read Failed" << endl;
        }

        cout << endl;

        //---------------- HC-SR04 ----------------

        if (usOK)
        {
            cout << fixed << setprecision(2);

            cout << "HC-SR04 Distance : "
                 << ultrasonic.measuredDistance
                 << " in" << endl;

            cout << "HC-SR04 Depth    : "
                 << ultrasonic.depth
                 << " in" << endl;

            cout << "HC-SR04 Flow     : "
                 << ultrasonic.flowRate
                 << " GPM" << endl;
        }
        else
        {
            cout << "HC-SR04 Read Failed" << endl;
        }

        cout << endl;

        //---------------- MB7389 ----------------

        if (mbOK)
        {
            cout << fixed << setprecision(2);

            cout << "MB7389 Distance  : "
                 << maxbotix.measuredDistance
                 << " in" << endl;

            cout << "MB7389 Depth     : "
                 << maxbotix.depth
                 << " in" << endl;

            if (maxbotix.targetTooClose)
            {
                cout << "WARNING: Target within minimum range."
                     << endl;
            }

            if (maxbotix.noTarget)
            {
                cout << "WARNING: No target detected."
                     << endl;
            }
        }
        else
        {
            cout << "MB7389 Read Failed" << endl;
        }

        cout << "=========================================="
             << endl;

        //----------------------------------------------------
        // Delay before next complete acquisition
        //----------------------------------------------------

        this_thread::sleep_for(seconds(1));
    }

    shutdownMB7389();

    return 0;
}
