# AnacostiaIQ startup and mode recovery

AnacostiaIQ supports two runtime modes on the Raspberry Pi:

- `headless`: `anacostiaiqd` runs as a systemd user service.
- `gui`: the desktop application is restored by graphical-session autostart.

The last selected mode is stored at:

```text
~/.local/state/anacostiaiq/mode
```

The mode manager guarantees that the GUI and headless process are not deliberately started together, avoiding competing GPIO/UART ownership.

## Install

From the repository root:

```bash
bash scripts/install-startup.sh
```

The installer preserves the mode that is already running on first installation. If neither mode is running, it defaults to `headless`. It enables systemd user lingering so headless acquisition can start after boot without an interactive login.

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

For GUI restoration after an unattended power outage, the Raspberry Pi must itself be configured to enter the desktop session automatically (for example, desktop autologin). Without a graphical login/session there is no display on which a GUI application can be restored; the saved `gui` state remains intact and will be honored at the next graphical login.

Network availability is intentionally not a prerequisite for starting either mode. Sensor acquisition should be able to run while networking is unavailable. Persistent buffering/retry of failed cloud writes is a separate reliability layer and should be implemented in the shared database writer.
