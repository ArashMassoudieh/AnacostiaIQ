# Adding moisture probes to the lab ADS1115

The lab station uses one ADS1115 at I2C address `0x48` on `/dev/i2c-1`.
The converter provides four single-ended analog inputs, A0 through A3. A0,
A1, and A2 are active lab moisture probes. A3 remains available for one
additional analog-output moisture probe.

No C++ change or rebuild is required. `Ads1115Bus` already performs an
independent conversion for channels 0 through 3, and each
`moisture_ads1115` entry in `config.json` selects one channel.

## Electrical connections

Power the station down before changing the wiring. For each added probe:

| Probe connection | ADS1115/Pi connection |
|---|---|
| VCC | 3.3 V |
| GND | Common ADS1115/Pi ground |
| Analog output (`AO`) | One unused input: A1, A2, or A3 |
| Digital output (`DO`) | Leave disconnected |

The ADS1115 full-scale setting does not change the input pin's electrical
limit. With the converter powered from 3.3 V, every analog input must remain
between ground and 3.3 V. Do not power a probe from 5 V unless its analog
output is independently limited to the ADS1115-safe range.

Before connecting a probe, confirm that its selected ADS1115 terminal is
physically empty and is not bridged to another breadboard row or input.
Floating high-impedance inputs can retain a repeatable voltage, so a voltage
reading alone does not indicate a connected sensor. On 2026-09-30, direct
sampling confirmed stable connected signals on A1 (21578 raw / 2.697 V) and
A2 (21370 raw / 2.671 V); A3 remained unconfigured.

## Configure a sensor

Add one object per probe to the `sensors` array in the repository-root
`config.json`. This example assigns a probe to A1:

```json
{
  "_comment": "Additional soil-moisture probe connected to ADS1115 A1.",
  "id": "lab_moisture_a1",
  "type": "moisture_ads1115",
  "unit": "%",
  "name": "Soil Moisture A1",
  "intervalSeconds": 60,
  "samplesPerReading": 1,
  "params": {
    "channel": 1,
    "adcDry": 23000,
    "adcWet": 6400
  }
}
```

Use a unique `id`, display `name`, and channel for every probe:

| ADS input | `channel` | Suggested ID |
|---|---:|---|
| A0 | 0 | `lab_moisture_a0` (active) |
| A1 | 1 | `lab_moisture_a1` (active) |
| A2 | 2 | `lab_moisture_a2` (active) |
| A3 | 3 | `lab_moisture_a3` (available) |

The example's `adcDry` and `adcWet` values are the provisional calibration
used for A1 and A2. They keep the live channels away from a false 0% boundary
alarm, but they are not quantitative calibration. Calibrate every probe
independently; component and soil variation can otherwise make the displayed
percentage misleading.

## Calibrate each probe

1. Start with temporary dry/wet values for which `adcDry` is greater than
   `adcWet`; the application rejects an inverted or zero-width calibration.
2. Run the service with the probe dry and note its `raw` value in the log.
3. Put the sensing portion in water or representative wet soil, wait for the
   value to stabilize, and record the wet raw value.
4. Set that sensor's `adcDry` and `adcWet` to the two recorded endpoints.
5. Repeat for every added channel.

Follow the raw values while calibrating:

```bash
pid=$(systemctl --user show -p MainPID --value anacostiaiqd.service)
journalctl -f _PID="$pid" -o cat | grep --line-buffered MoistureSensorI2C
```

Only the sensing portion of a non-waterproof probe should be immersed. Keep
the electronics and connector dry.

## Validate and activate

Validate the JSON before restarting the monitor:

```bash
cd ~/AnacostiaIQ
python3 -m json.tool config.json >/dev/null
git diff --check
systemctl --user restart anacostiaiqd.service
```

Then confirm that every unique sensor ID reaches healthy state and produces a
plausible reading:

```bash
bash scripts/anacostiaiq-health
pid=$(systemctl --user show -p MainPID --value anacostiaiqd.service)
journalctl _PID="$pid" --since "2 minutes ago" --no-pager -o cat \
  | grep -E "MoistureSensorI2C|Soil Moisture|Health .*moisture"
```

Adding channels does not require a second ADS1115 or a different I2C address.
The conversions are sequential; at the configured 128 samples per second,
four probes sampled once per minute add negligible bus load.
