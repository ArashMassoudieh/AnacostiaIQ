# Two MaxBotix sensors on the lab Raspberry Pi

The lab station now has two distinct MB7389-100 sensors. They must use separate UART receivers and separate sensor IDs.

| Sensor | Electrical interface | Pi receive pin | Device | Sensor ID |
|---|---|---|---|---|
| Original installed lab unit | 5 V supply and verified 2N3904 inverter | GPIO15, physical pin 10 | `/dev/ttyAMA0` | `lab_maxbotix_depth_1` |
| New unit, label B7604 | 3.3 V supply and direct TTL serial | GPIO5, physical pin 29 | `/dev/ttyAMA2` | `lab_maxbotix_depth_2` |

Do not join the two serial outputs. The original unit keeps its measured inverter circuit. The new unit uses no inverter or resistors in the planned 3.3 V TTL connection.

## Enable UART2 on Raspberry Pi 5

Add this line to `/boot/firmware/config.txt`:

```ini
dtoverlay=uart2-pi5
```

Reboot, then confirm that `/dev/ttyAMA2` exists and GPIO5 has the UART receive function:

```bash
ls -l /dev/ttyAMA0 /dev/ttyAMA2
pinctrl get 5
```

The service user must belong to `dialout`.

## New sensor wiring

Power the Pi off before changing wiring.

- MB7389 pin 6 V+ -> Pi 3.3 V
- MB7389 pin 7 GND -> Pi GND
- MB7389 pin 5 TTL serial -> GPIO5, physical pin 29
- MB7389 pin 4 -> leave open for continuous ranging
- pins 1-3 -> unused for this integration

The MB7389 datasheet permits a 2.7-5.5 V supply. This station uses 3.3 V for the new unit so its TTL output stays in the Pi GPIO voltage domain. Validate useful range after installation because the manufacturer notes that operation below 5 V may reduce some stated performance.

## Raw UART validation

Stop the service before reading a UART directly, and restart it afterwards:

```bash
systemctl --user stop anacostiaiqd.service
stty -F /dev/ttyAMA2 9600 cs8 -cstopb -parenb raw -echo
timeout 3 dd if=/dev/ttyAMA2 bs=1 status=none | od -An -tx1c
systemctl --user start anacostiaiqd.service
```

A healthy stream contains repeated ASCII `R####` records followed by carriage return (`0d`). Then check both health components:

```bash
scripts/anacostiaiq-health
python3 -m json.tool ~/.local/state/anacostiaiq/health.json
```

Expected components are `sensor_lab_maxbotix_depth_1` and `sensor_lab_maxbotix_depth_2`.
