#ifndef MB7389_H
#define MB7389_H

#include <string>

struct MB7389Data
{
    int rangeMM;

    double measuredDistance;
    double depth;

    bool targetTooClose;
    bool noTarget;
};

bool initMB7389(
    const std::string& device = "/dev/serial0");

bool readMB7389(MB7389Data& data);

void shutdownMB7389();

#endif
