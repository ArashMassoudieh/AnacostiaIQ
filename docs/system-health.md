# AnacostiaIQ system health telemetry

The station service publishes health alongside measurements through the existing
`/sensor` API and writes a local snapshot to:

`~/.local/state/anacostiaiq/health.json`

## State codes

| Value | State |
|---:|---|
| 0 | healthy |
| 1 | degraded |
| 2 | offline |
| 3 | critical |

## Components

The headless service currently evaluates:

- every configured sensor (`health_sensor_<sensor-id>`), including HC-SR04,
  MaxBotix and moisture channels regardless of whether the moisture probe uses
  the legacy GPIO ADC or ADS1115 transport;
- the monitoring application heartbeat (`health_application`);
- cloud/API delivery (`health_cloud`);
- persistent upload queue growth (`health_upload_queue`);
- root filesystem free space (`health_storage`);
- Raspberry Pi CPU temperature (`health_cpu_temperature`);
- aggregate station state (`health_overall`).

Health telemetry is published immediately when a component changes level and
then refreshed for every component at the configured heartbeat interval
(normally five minutes). This prevents a continuously healthy component from
aging out of the remote dashboard's lookback window. Health records use the same
persistent upload queue as normal measurements and are prioritized by the
DatabaseWriter, so health history is retained during an Internet/API outage.

The local JSON snapshot also contains the current human-readable reason, such
as `sensor_unavailable`, `api_retrying`, `stale_120s`, queue depth, free disk
space, or CPU temperature. Remote health records preserve the same reason in
the API record's text/unit field while retaining the numeric 0--3 value for
backward compatibility. Both the web dashboard and email monitor display it.

## Default thresholds

- sensor: two consecutive completely-invalid readings -> degraded; unavailable
  or stale for more than max(60 s, 3 x configured poll interval) -> offline;
- moisture calibration limits: valid 100% readings remain healthy, with the
  informational reason `wet_limit_reached` after the configured boundary count
  (default one). Rain can legitimately reach the wet reference. Valid 0%
  readings receive a degraded `dry_limit_reached_check_calibration` warning.
  Neither endpoint becomes offline solely because it repeats. A limit is not proof of a
  disconnected probe. Unavailable/stale acquisition and recovery failures
  retain their normal severity and reason. This check is ADC-independent;
- cloud: one or two consecutive failed writes -> degraded; three or more ->
  offline;
- upload queue: 500 pending -> degraded; 2000 pending -> critical;
- disk: <= 500 MB free -> degraded; <= 100 MB -> critical;
- CPU: >= 70 C -> degraded; >= 80 C -> critical.

All of these defaults can be overridden under `health.thresholds` in the
station configuration. Missing settings retain the defaults, so existing field
configuration files remain compatible.
The legacy `moistureOfflineBoundaryReadings` key remains accepted for config
compatibility but no longer escalates calibrated endpoints to offline.

## Portal alarms

`FrontEnd/web/health.html` interprets the station-scoped `health_*` records,
shows active degraded/offline/critical components, detects a missing or stale
`health_application` heartbeat, and can issue browser notifications on state
transitions and recovery. The dashboard station list is configured through
`FrontEnd/config.json`.

The server-side email monitor persists transition state across restarts. Set
`ANACOSTIAIQ_ALERT_REPEAT_SECONDS` to a positive interval to repeat an alert
only while an outage remains active; the default `0` disables reminders.
Recipients, monitored components, polling interval, heartbeat staleness and
SMTP destination are configured in the monitor environment file.

The field GPIO conflict between HC-SR04 and the UART has been removed: HC-SR04
uses GPIO17 for TRIG and GPIO18 for ECHO, while the MaxBotix UART input remains
on GPIO15/RXD0. On the field Pi this header UART has been directly verified as
`/dev/ttyAMA0`; `/dev/serial0` produced no frames. The MB7389 `R5000` sentinel
means no target is detected, so production reports zero ponding depth while
retaining healthy sensor communication.

### Field email monitor deployment

Run the monitor on the always-on API server so a Pi outage does not stop
notifications. The replacement field station is `jm_cua_twin_pi_staging` in
project `John-McCormack`. Set `ANACOSTIAIQ_PROJECT` and
`ANACOSTIAIQ_ALERT_TIMESTAMPS_UTC=true`: field telemetry uses naive UTC
timestamps. The example environment lists the replacement station components;
`overall` is omitted to avoid duplicate aggregate alarms.

Checks run every 60 seconds. An application heartbeat older than 600 seconds
raises an offline alarm. Three consecutive failed API probes raise a separate
API alarm; station states are retained during that outage. A healthy transition
sends one recovery message. The initial run establishes a silent baseline.
Repeated reminders are disabled. SMTP settings and recipients stay in a private
mode-600 environment file and must never be committed.

For a standalone server installation, copy the script to
`~/anacostiaiq-alerts/health_email_monitor.py`, adapt the service ExecStart to
that path, and create `~/.local/state/anacostiaiq` before starting the unit.
Enable user lingering so the user service starts at boot without a login.
Use `--test-email` with the same private environment to verify SMTP acceptance
before enabling the continuous service. Keep other copies of the monitor
disabled to prevent duplicate messages.

Email display times use `ANACOSTIAIQ_ALERT_TIMEZONE` (default
`America/New_York`), with EST/EDT and the UTC offset shown explicitly.
Telemetry queries and heartbeat calculations retain their UTC convention.
