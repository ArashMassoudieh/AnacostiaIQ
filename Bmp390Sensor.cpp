#include "Bmp390Sensor.h"
#include <vector>
#ifdef RasPi
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
namespace {
struct Context { int fd; int address; };
BMP3_INTF_RET_TYPE readI2c(uint8_t reg, uint8_t *data, uint32_t size, void *ctx) {
    auto *c = static_cast<Context *>(ctx);
    if (size > 65535) return -1;
    i2c_msg msg[2]{{static_cast<__u16>(c->address),0,1,&reg},
                  {static_cast<__u16>(c->address),I2C_M_RD,static_cast<__u16>(size),data}};
    i2c_rdwr_ioctl_data transfer{msg,2};
    return ioctl(c->fd,I2C_RDWR,&transfer) == 2 ? 0 : -1;
}
BMP3_INTF_RET_TYPE writeI2c(uint8_t reg, const uint8_t *data, uint32_t size, void *ctx) {
    auto *c = static_cast<Context *>(ctx);
    if (size > 65534) return -1;
    std::vector<uint8_t> bytes(size+1); bytes[0]=reg;
    std::copy(data,data+size,bytes.begin()+1);
    i2c_msg msg{static_cast<__u16>(c->address),0,static_cast<__u16>(bytes.size()),bytes.data()};
    i2c_rdwr_ioctl_data transfer{&msg,1};
    return ioctl(c->fd,I2C_RDWR,&transfer) == 1 ? 0 : -1;
}
void delayUs(uint32_t us, void *) { std::this_thread::sleep_for(std::chrono::microseconds(us)); }
}
#endif
Bmp390Bus::Bmp390Bus(QString device,int address):m_device(device),m_address(address) {}
Bmp390Bus::~Bmp390Bus() {
#ifdef RasPi
    if(m_fd>=0) close(m_fd);
    delete static_cast<Context *>(m_dev.intf_ptr);
#endif
}
bool Bmp390Bus::initialize() {
#ifdef RasPi
    if(m_fd>=0) return true;
    if(m_address!=0x76 && m_address!=0x77) return false;
    m_fd=open(m_device.toUtf8().constData(),O_RDWR);
    if(m_fd<0) return false;
    delete static_cast<Context *>(m_dev.intf_ptr);
    m_dev={}; m_dev.intf_ptr=new Context{m_fd,m_address};
    m_dev.intf=BMP3_I2C_INTF; m_dev.read=readI2c; m_dev.write=writeI2c; m_dev.delay_us=delayUs;
    bmp3_settings settings{};
    settings.press_en=BMP3_ENABLE; settings.temp_en=BMP3_ENABLE;
    settings.odr_filter.odr=BMP3_ODR_1_5_HZ;
    settings.op_mode=BMP3_MODE_NORMAL;
    if(bmp3_init(&m_dev)==BMP3_OK && m_dev.chip_id==0x60 &&
       bmp3_set_sensor_settings(BMP3_SEL_PRESS_EN|BMP3_SEL_TEMP_EN|BMP3_SEL_ODR,&settings,&m_dev)==BMP3_OK &&
       bmp3_set_op_mode(&settings,&m_dev)==BMP3_OK) {
        delayUs(700000,nullptr); return true;
    }
    close(m_fd); m_fd=-1;
#endif
    return false;
}
bool Bmp390Bus::read(double &pressureHpa,double &temperatureC) {
#ifdef RasPi
    if(!initialize()) return false;
    bmp3_data data{};
    if(bmp3_get_sensor_data(BMP3_PRESS_TEMP,&data,&m_dev)==BMP3_OK) {
        pressureHpa=data.pressure/100.0; temperatureC=data.temperature;
        if(std::isfinite(pressureHpa) && pressureHpa>=300 && pressureHpa<=1250 &&
           std::isfinite(temperatureC) && temperatureC>=-40 && temperatureC<=85) return true;
    }
    // Reopen and reload calibration on recovery after a disconnected device.
    close(m_fd); m_fd=-1;
#endif
    Q_UNUSED(pressureHpa);
    Q_UNUSED(temperatureC);
    return false;
}
Bmp390Sensor::Bmp390Sensor(const QString &id,const QString &name,std::shared_ptr<Bmp390Bus> bus,bool temperature)
 :Sensor(id,temperature?"degC":"hPa",name),m_bus(bus),m_temperature(temperature) {}
bool Bmp390Sensor::initialize() { bool ok=m_bus && m_bus->initialize(); setAvailable(ok); return ok; }
double Bmp390Sensor::measure() {
    double p,t;
    if(!m_bus || !m_bus->read(p,t)) return invalidReading();
    return m_temperature?t:p;
}
bool Bmp390Sensor::acceptsReading(double value) const {
    return std::isfinite(value) && (m_temperature ? value>=-40 && value<=85 : value>=300 && value<=1250);
}
