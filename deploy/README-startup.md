# AnacostiaIQ startup and recovery

AnacostiaIQ supports two Raspberry Pi runtime modes:

- `headless`: `anacostiaiqd` runs as a systemd user service.
- `gui`: the desktop application is restored by graphical-session autostart.

Both modes use the same sensor classes, configuration, cloud writer, local upload queue, and automatic sensor-recovery logic.

## Runtime mode state

The mode that actually starts is written to:

```text
~/.local/state/anacostiaiq/mode
```

A shared runtime guard also holds:

```text
~/.local/state/anacostiaiq/hardware.lock
```

so GUI and headless cannot deliberately own the GPIO/UART sensor hardware at the same time. Because the application itself records its mode after acquiring the hardware lock, the saved state reflects what was actually running, not only what a helper script requested.

## Install

From the repository root:

```bash
bash scripts/install-startup.sh
```

The installer preserves the mode already running on first installation. If neither mode is running, it defaults to `headless`. It enables systemd user lingering so headless acquisition can start after boot without an interactive login.

## Switch modes

```bash
scripts/anacostiaiq-mode gui
scripts/anacostiaiq-mode headless
scripts/anacostiaiq-mode status
```

When `gui` is selected, the headless service is stopped before the GUI is started. When `headless` is selected, the GUI is terminated before the systemd service is restarted.

## Reboot / power-loss behavior

If the saved mode is `headless`, the enabled systemd user service starts `anacostiaiqd` at boot. The service uses `Restart=always` with a 5-second restart delay.

If the saved mode is `gui`, the headless service is skipped and the GUI is started by the desktop autostart entry when the user's graphical session starts.

For GUI restoration after an unattended power outage, the Raspberry Pi must enter its graphical desktop session automatically (for example, desktop autologin). Without a graphical session there is no display on which a GUI application can be restored; the saved `gui` state remains intact and is honored at the next graphical login.

Network availability is intentionally not a prerequisite for starting either mode.

## Internet/API outage recovery

Every cloud record is first appended to a persistent local queue:

```text
~/.local/state/anacostiaiq/upload-queue.jsonl
```

The shared `DatabaseWriter` then sends queued records oldest-first. Failed writes stay on disk and are retried with exponential backoff from 5 seconds up to 5 minutes. When the API becomes reachable again, the backlog is flushed automatically.

This is at-least-once delivery: after an abrupt crash immediately after the server accepted a record, that record can be sent again after restart. This is preferable to silently losing field measurements; the server can later add idempotency/deduplication if strict exactly-once storage is required.

## Sensor recovery

The shared `Sensor` base class provides the same recovery behavior to GUI and headless builds on Raspberry Pi:

- an initialization failure starts a 30-second reinitialization loop;
- three consecutive completely invalid readings mark an initialized sensor unavailable;
- the sensor's `cleanup()` is called before each recovery attempt;
- successful reinitialization clears the failure count and resumes normal polling.

This applies to HC-SR04, MaxBotix, and moisture/ADC sensor classes that use the common `Sensor` interface.

## Remaining field-hardening step

A Raspberry Pi hardware/system watchdog is intentionally not enabled automatically by the installer. It should be added and tested separately after the application-level recovery behavior has been validated on the target Pi, because watchdog settings can force automatic reboots if configured incorrectly.
