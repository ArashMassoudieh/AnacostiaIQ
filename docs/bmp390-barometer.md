# BMP390 barometer — lab installation

Hardware: Waveshare BMP390 Barometric Pressure Sensor, SKU 24658 (identified
from Lab Photos/IMG_7066.jpg). Five-pin I2C connector; follow printed labels,
not assumed wire colors. Power down before wiring.

| Module label | Pi/HAT connection | Physical header pin |
|---|---|---|
| VCC | 3.3 V | 1 or 17 |
| GND | Ground | 6 (or another GND) |
| SDA | GPIO2 / SDA1 | 3 |
| SCL | GPIO3 / SCL1 | 5 |
| INT | Leave unconnected | — |

The ADS1115 and BMP390 share SDA/SCL and ground. Branch these connections
securely; do not connect the barometer to ADS analog inputs A0–A3. The default
BMP390 address is 0x77; the ADDR option selects 0x76. Neither conflicts with
ADS1115 at 0x48. No external divider or inverter is needed with this 3.3 V plan.
The supplied connector is **PH2.0, five pins** per Waveshare, not an assumed
JST-XH 2.5 mm connector. Confirm the actual mating connector before buying.

## Software

The collector includes Bosch's official BSD-licensed BMP3 SensorAPI for factory
calibration and pressure/temperature compensation. It accepts only BMP390 chip
ID 0x60, validates the compensated range and retries after I2C failures.
Pressure is **absolute local atmospheric pressure in hPa**, not sea-level
corrected pressure or water pressure. Temperature is **sensor temperature in
degC**; Pi/enclosure heat can bias it above ambient air temperature. Negative
Celsius readings are supported; failed reads are omitted, never replaced by zero.

Append these entries to the lab's existing `sensors` array, without replacing
its pressure-transducer calibration or existing IDs:

```json
[
  {
    "type": "bmp390_pressure",
    "id": "lab_barometer_pressure",
    "name": "Lab atmospheric pressure",
    "unit": "hPa",
    "intervalSeconds": 60,
    "samplesPerReading": 1,
    "params": {"device": "/dev/i2c-1", "address": "0x77"}
  },
  {
    "type": "bmp390_temperature",
    "id": "lab_barometer_temperature",
    "name": "Lab barometer temperature",
    "unit": "degC",
    "intervalSeconds": 60,
    "samplesPerReading": 1,
    "params": {"device": "/dev/i2c-1", "address": "0x77"}
  }
]
```

Both entries share one physical device and driver instance. The normal sensor
pipeline handles sampling, health, durable upload queue and project namespace.
Existing adaptive polling may multiply the 60-second interval by the idle factor.
The device continuously converts at approximately 1.5 Hz; configured polling
controls recording frequency, not the internal conversion frequency.

Build the headless collector with `qmake` (Qt 5) or `qmake6` (Qt 6), then `make`.
Back up the binary and configuration before installing and restarting the user
service `anacostiaiqd`. This integration does not require a Pi reboot.

## Verify

1. Confirm `/dev/i2c-1` exists and user `behzad` belongs to `i2c`.
2. `/usr/sbin/i2cdetect -y 1 0x76 0x77` should identify 0x77 (or configured 0x76).
3. BMP390 chip-ID register 0x00 should read 0x60. An address ACK alone is not
   enough to confirm valid compensated pressure readings.
4. Inspect `~/.local/state/anacostiaiq/health.json` for
   `sensor_lab_barometer_pressure` and `sensor_lab_barometer_temperature`.
5. Confirm fresh records with the two IDs in the upload queue/API under project
   `John-McCormack`. A large existing queue may delay cloud appearance.
6. Compare against an independent local absolute-pressure reference. A weather
   app often displays sea-level pressure and may differ legitimately.

Keep the pressure port exposed to ambient air inside a dry, vented enclosure.
Do not pot the sensor, cover its port with sealant, immerse it, or put it in an
airtight inner box. Keep it away from Pi exhaust and direct solar heating.

Sources:
- https://www.waveshare.com/wiki/BMP390_Barometric_Pressure_Sensor
- https://www.waveshare.com/product/modules/sensors/bmp390-barometric-pressure-sensor.htm
- https://github.com/boschsensortec/BMP3_SensorAPI

Validation and deployment results are recorded separately; successful acquisition
does not establish a calibrated ambient-air temperature measurement.

## Lab commissioning — November 2, 2026

Both channels passed live acquisition and reported healthy at 12:57 EST:
1022.51 hPa and 21.74 degC. The existing upload backlog was about 26,000 records;
local acquisition was confirmed independently of cloud delivery. Rollback archive
on the lab Pi: `/home/behzad/field-setup/bmp390-20261102/before.tar.gz`.

Regression checks: `tests/bmp390.pro` covers signed Celsius (including -1),
NaN failures and pressure bounds; existing moisture precision tests remain
applicable. Native Qt 6 and Raspberry Pi Qt 5 collector builds were verified.

The lab dashboard configuration includes these two series and their health
components. Cloud charts may remain empty while the previous backlog drains.

Next planned hardware: the user has an RCWL-9610A-based ultrasonic board.
Its exact board pinout and power/logic requirements must be identified before
changing HC wiring or software; it is not configured by this BMP390 change.
