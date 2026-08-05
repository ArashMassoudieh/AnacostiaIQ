#ifndef ADC_H
#define ADC_H

#include <cstdint>

//============================================================
// ADC Measurement Structure
//============================================================

struct ADCData
{
    uint8_t adc1;
    uint8_t adc2;
    uint8_t adc3;
    uint8_t adc4;

    double voltage1;
    double voltage2;
    double voltage3;
    double voltage4;
};

//============================================================
// Function Prototypes
//============================================================

// Initialize GPIO and ADC hardware.
// Returns true if initialization succeeds.
bool initADC();

// Perform one complete ADC acquisition.
// The ADCData structure is filled with the latest readings.
// Returns true if the acquisition succeeds.
bool readADC(ADCData& data);

#endif // ADC_H
