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
- moisture boundary detection: repeated calibrated 0% or 100% readings are
  treated as suspicious after one reading and offline after three identical
  boundary readings; this check is ADC-transport-independent;
- cloud: one or two consecutive failed writes -> degraded; three or more ->
  offline;
- upload queue: 500 pending -> degraded; 2000 pending -> critical;
- disk: <= 500 MB free -> degraded; <= 100 MB -> critical;
- CPU: >= 70 C -> degraded; >= 80 C -> critical.

All of these defaults can be overridden under `health.thresholds` in the
station configuration. Missing settings retain the defaults, so existing field
configuration files remain compatible.

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
