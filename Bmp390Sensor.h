#ifndef BMP390SENSOR_H
#define BMP390SENSOR_H
#include "Sensor.h"
#include "third_party/bmp3/bmp3.h"
#include <memory>
#include <limits>

// One physical bus/device is shared by its pressure and temperature channels.
class Bmp390Bus {
public:
    Bmp390Bus(QString device, int address);
    ~Bmp390Bus();
    bool initialize();
    bool read(double &pressureHpa, double &temperatureC);
private:
    QString m_device;
    int m_address;
    int m_fd = -1;
    bmp3_dev m_dev{};
};
class Bmp390Sensor : public Sensor {
public:
    Bmp390Sensor(const QString &id, const QString &name,
                 std::shared_ptr<Bmp390Bus> bus, bool temperature);
    bool initialize() override;
    double measure() override;
    bool acceptsReading(double value) const override;
    double invalidReading() const override { return std::numeric_limits<double>::quiet_NaN(); }
private:
    std::shared_ptr<Bmp390Bus> m_bus;
    bool m_temperature;
};
#endif
