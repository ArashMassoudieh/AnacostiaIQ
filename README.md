# AnacostiaIQ

Stormwater monitoring station software for the CUA Digital Twin project.

A single C++/Qt codebase builds two applications that share the same sensor
classes, `config.json`, cloud writer, upload queue and health monitor:

| Build | Target | What it is |
|---|---|---|
| `AnacostiaIQ` | `AnacostiaIQ.pro` | Desktop GUI with live charts (Qt Widgets + Qt Charts) |
| `anacostiaiqd` | `headless/anacostiaiqd.pro` | Console daemon for a Raspberry Pi with no display, run under systemd |

Readings from the configured sensors (HC-SR04 ultrasonic, MaxBotix MB7389-100
over UART, soil moisture via an ADS1115 I²C ADC) and a weather forecast
(Open-Meteo or NOAA) are pushed to the cloud API declared in `config.json`.
Every record is queued on disk first, so an Internet outage delays delivery
instead of losing measurements.

The web dashboard that visualizes this data is a separate Qt/WebAssembly app in
[`FrontEnd/`](FrontEnd/README.md).

---

## 1. Which machine are you on?

- **Ubuntu desktop/laptop** — development, GUI testing, building. No GPIO
  hardware, so sensors report as unavailable; everything else (config parsing,
  weather, cloud upload, charts) works. See [§2](#2-install-on-ubuntu).
- **Raspberry Pi** — the actual field/lab station. Needs the `RasPi` build so
  GPIO, UART and I²C code is compiled in. See [§3](#3-install-on-a-raspberry-pi).

---

## 2. Install on Ubuntu

Tested on Ubuntu 24.04 LTS with Qt 6.4.

### 2.1 Dependencies

```bash
sudo apt update
sudo apt install -y build-essential git qt6-base-dev qt6-base-dev-tools qt6-charts-dev
```

No libgpiod is needed here — the GPIO code is compiled out unless `RasPi` is
defined.

### 2.2 Clone

```bash
git clone <repository-url> ~/AnacostiaIQ
```

### 2.3 Build the GUI

Use `qmake6` explicitly. On Ubuntu the bare `qmake` command is a qtchooser
wrapper that still points at Qt 5 and fails with
`could not exec '/usr/lib/qt5/bin/qmake'`.

```bash
cd ~/AnacostiaIQ && mkdir -p build-desktop && cd build-desktop && qmake6 .. && make -j"$(nproc)"
```

Run it (qmake copies `config.json` next to the binary):

```bash
cd ~/AnacostiaIQ/build-desktop && ./AnacostiaIQ
```

### 2.4 Build the headless daemon (optional on a desktop)

```bash
cd ~/AnacostiaIQ/headless && qmake6 anacostiaiqd.pro && make -j"$(nproc)" && ./anacostiaiqd --help
```

It prints `building WITHOUT GPIO — sensors will report unavailable`, which is
expected on x86.

---

## 3. Install on a Raspberry Pi

This is the deployment path. Build **in-source** at the repository root: the
systemd unit and the autostart entry look for `@REPO_DIR@/headless/anacostiaiqd`
and `@REPO_DIR@/AnacostiaIQ`, and `.gitignore` already covers both.

### 3.1 Clone to `~/AnacostiaIQ`

`scripts/anacostiaiq-mode` defaults its repository path to `$HOME/AnacostiaIQ`.
Clone there, or export `ANACOSTIAIQ_REPO=/your/path` wherever you invoke the
scripts.

```bash
git clone <repository-url> ~/AnacostiaIQ
```

### 3.2 Enable the hardware interfaces

```bash
sudo raspi-config nonint do_i2c 0
sudo raspi-config nonint do_serial_hw 0
sudo raspi-config nonint do_serial_cons 1
```

On Raspberry Pi OS 12 and newer, `do_serial_hw`/`do_serial_cons` replace the older
combined `do_serial`; if raspi-config rejects them, use the interactive menu
(Interface Options) instead.

That enables I²C (for the ADS1115), enables the serial **port** (for the
MaxBotix) and disables the serial **login console**, which would otherwise hold
the UART open. Reboot afterwards.

Add your user to the hardware groups and log out and back in:

```bash
sudo usermod -aG gpio,i2c,dialout "$USER"
```

Note which device node the MaxBotix is on. On the CUA units the GPIO header
UART is `/dev/ttyAMA0`, **not** `/dev/serial0` — `config.json` says so, and
`scripts/anacostiaiq-check` reports what `/dev/serial0` actually resolves to on
your board. Verify before trusting the default.

#### MaxBotix electrical interface and station-specific triggering

Do **not** assume that MaxBotix pin 5 can be connected directly to GPIO15 just
because both ends use 9600-baud serial. Raspberry Pi GPIO is 3.3-V-only, and
some MaxBotix units expose an RS-232-style, idle-low waveform whose polarity is
the inverse of the Pi UART's idle-high logic. Treat pin 5 as potentially
reaching the sensor supply voltage until it has been measured.

The CUA lab MB7389-100 was verified on 2026-09-18: a three-second GPIO15 edge
capture contained 1,176 transitions, and offline polarity inversion recovered
31 valid `R####\r` frames. A direct `/dev/ttyAMA0` read produced repeatable
garbage because the waveform idled low. Before enabling that sensor, install an
active inverter with a 3.3-V-safe output, for example:

- a 2N3904 NPN stage with a 10-kohm base resistor and a 10-kohm collector
  pull-up to 3.3 V; or
- a 3.3-V-powered logic inverter whose input is explicitly rated to accept the
  sensor's possible 5-V signal.

Connect sensor ground and Pi ground, route sensor pin 5 to the inverter input,
and route the inverter's 3.3-V output to GPIO15/RXD0. A resistor divider or a
generic BSS138 bidirectional level-shifter board does not invert the waveform
and therefore does not solve this fault.

Triggering is station-specific. The lab sensor produced valid frames while
free-running, so its configuration must omit `triggerPin`. The field sensor
uses GPIO25 as `triggerPin`; do not copy that setting to the lab unit. After
wiring an interface, verify that GPIO15 idles high and inspect the raw UART
before enabling the sensor:

```bash
pinctrl get 15
stty -F /dev/ttyAMA0 9600 cs8 -cstopb -parenb raw -echo
timeout 3 dd if=/dev/ttyAMA0 bs=1 status=none | od -An -tx1c
```

The byte stream must contain repeated ASCII `R####` records terminated by
carriage return (`0d`).

### 3.3 Dependencies

```bash
sudo apt update
sudo apt install -y build-essential git qt6-base-dev qt6-base-dev-tools qt6-charts-dev i2c-tools
```

`qt6-charts-dev` is only needed for the GUI build; a headless-only station can
skip it.

### 3.4 libgpiod — version 2 is required

`DistanceSensor`, `MaxbotixSensor` and `AdcBus` use the **libgpiod v2** C++ API
(`gpiod::line_request`, `gpiod::line::value`). Check what your OS ships:

```bash
gpiodetect --version
```

- **2.x** (Raspberry Pi OS 13 / Debian trixie and newer) — just install the
  headers:

  ```bash
  sudo apt install -y libgpiod-dev
  ```

- **1.6.x** (Raspberry Pi OS 12 / Bookworm) — the packaged headers will **not**
  compile this code. Build v2 from source:

  ```bash
  sudo apt install -y autoconf autoconf-archive automake libtool pkg-config m4 g++
  ```

  ```bash
  git clone https://github.com/brgl/libgpiod.git -b v2.1.x /tmp/libgpiod && cd /tmp/libgpiod && ./autogen.sh --enable-tools=yes --enable-bindings-cxx --prefix=/usr/local && make -j"$(nproc)" && sudo make install && sudo ldconfig
  ```

  If the compiler still picks up the old headers, remove the distro package
  (`sudo apt remove libgpiod-dev`) or put `/usr/local` ahead of it on the
  include path.

### 3.5 Build

The headless `.pro` detects ARM and defines `RasPi` automatically. The GUI
`.pro` does not — pass it on the `qmake6` line.

```bash
cd ~/AnacostiaIQ/headless && qmake6 anacostiaiqd.pro && make -j"$(nproc)"
```

The build log should say `building WITH GPIO (RasPi defined)`. For the GUI:

```bash
cd ~/AnacostiaIQ && qmake6 AnacostiaIQ.pro DEFINES+=RasPi && make -j"$(nproc)"
```

### 3.6 Configure the station

Edit `config.json` in the repository root before first run — both builds read
it, and the systemd unit passes this exact file:

- `station.id` / `station.name` — must be unique per Pi; the dashboard and the
  email alerts key off `station.id`.
- `app.apiUrl` — the cloud endpoint.
- `sensors[]` — GPIO/BCM pins, device nodes, poll intervals and calibration.
  Each entry carries a `_comment` documenting how that sensor is wired.
- `weather` — `openmeteo` (latitude/longitude) or `noaa` (office + grid).
- `health.thresholds` — alarm levels, all overridable without recompiling.

### 3.7 Install startup and recovery

```bash
bash ~/AnacostiaIQ/scripts/install-startup.sh
```

This writes a systemd **user** service and a desktop autostart entry, enables
lingering so the service starts at boot without a login, and records the
runtime mode. Details and the recovery behavior are in
[deploy/README-startup.md](deploy/README-startup.md).

Only one mode may own the sensor hardware at a time, enforced by a lock in
`~/.local/state/anacostiaiq/`:

```bash
scripts/anacostiaiq-mode status
scripts/anacostiaiq-mode headless
scripts/anacostiaiq-mode gui
```

GUI mode restores itself only if the Pi boots into a graphical session
(enable desktop autologin), otherwise the saved mode waits for the next login.

### 3.8 Verify

```bash
scripts/anacostiaiq-check
```

A read-only diagnostic: runtime mode, service state, hardware interfaces,
upload queue depth and recent sensor evidence. Live logs:

```bash
journalctl --user -u anacostiaiqd.service -f
```

---

## 4. Health monitoring and alerts

The station publishes health telemetry through the same API and writes a
snapshot to `~/.local/state/anacostiaiq/health.json`. Components, state codes
and default thresholds are documented in
[docs/system-health.md](docs/system-health.md).

An optional email alarm monitor runs on any always-on machine (typically the
server, not the Pi) and needs Python 3 only — no third-party packages:

```bash
bash scripts/install_health_email_monitor.sh
```

Then fill in SMTP settings in `~/.config/anacostiaiq/health-email.env` and set
`ANACOSTIAIQ_EMAIL_ALERTS_ENABLED=true`. Credentials live in that file, which
is `chmod 600` and never committed.

---

## 5. Repository layout

| Path | Contents |
|---|---|
| `*.cpp` / `*.h` (root) | Shared sensor, config, cloud, health and GUI code |
| `headless/` | `anacostiaiqd` daemon sources and project file |
| `FrontEnd/` | Qt/WebAssembly web dashboard ([build guide](FrontEnd/README.md)) |
| `scripts/` | Startup installer, mode switcher, diagnostics, email monitor |
| `deploy/` | systemd and autostart templates, [startup guide](deploy/README-startup.md) |
| `docs/` | [System health telemetry reference](docs/system-health.md) |
| `config.json` | Station configuration read by both builds |
| `HC_SR04_*`, `MB7389-100_*`, `led_blink_2/`, `All_inclusive_sensor_program/`, `Automated_Multi-ADC_sensing_1/` | Standalone bench-test programs used to validate each sensor |

---

## 6. Troubleshooting

| Symptom | Cause |
|---|---|
| `qmake: could not exec '/usr/lib/qt5/bin/qmake'` | Use `qmake6`, not `qmake`. |
| `gpiod.hpp: No such file` or C++ API errors on `line_request` | libgpiod v1 headers installed; see [§3.4](#34-libgpiod--version-2-is-required). |
| `building WITHOUT GPIO` on a Pi | Building the GUI without `DEFINES+=RasPi`, or a non-ARM cross build. |
| Sensors unavailable, no errors | `RasPi` not defined — the code stubs out hardware access by design. |
| Second instance exits with code 2 | The other mode holds `~/.local/state/anacostiaiq/hardware.lock`. Use `scripts/anacostiaiq-mode`. |
| No MaxBotix data or repeatable garbage bytes | Check the serial console and `/dev/ttyAMA0` vs `/dev/serial0` first. Then verify idle polarity and voltage: the lab MB7389 requires an active inverter with a 3.3-V-safe output. Confirm with `scripts/anacostiaiq-check` and the raw-UART procedure in §3.2. |
| Readings stop reaching the dashboard | Check the queue: `wc -l ~/.local/state/anacostiaiq/upload-queue.jsonl`. A growing file means the API is unreachable; the backlog flushes automatically. |

---

## License

See [LICENSE.md](LICENSE.md).
