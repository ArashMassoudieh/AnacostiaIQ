#ifndef HCSR04_H
#define HCSR04_H

struct HCSR04Data
{
    double measuredDistance;
    double depth;
    double flowRate;
};

bool initHCSR04();
bool readHCSR04(HCSR04Data& data);

#endif
