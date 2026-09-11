# AnacostiaIQ system health telemetry

The field service publishes health alongside measurements through the existing
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
  MaxBotix and moisture/ADC channels;
- the monitoring application heartbeat (`health_application`);
- cloud/API delivery (`health_cloud`);
- persistent upload queue growth (`health_upload_queue`);
- root filesystem free space (`health_storage`);
- Raspberry Pi CPU temperature (`health_cpu_temperature`);
- aggregate station state (`health_overall`).

Health telemetry is published immediately when a component changes level and
then as a heartbeat every five minutes. It uses the same persistent upload queue
as normal measurements, so health history is not lost during an Internet/API
outage.

The local JSON snapshot also contains the current human-readable reason, such
as `sensor_unavailable`, `api_retrying`, `stale_120s`, queue depth, free disk
space, or CPU temperature.

## Default thresholds

- sensor: two consecutive completely-invalid readings -> degraded; unavailable
  or stale for more than max(60 s, 3 x configured poll interval) -> offline;
- cloud: one or two consecutive failed writes -> degraded; three or more ->
  offline;
- upload queue: 500 pending -> degraded; 2000 pending -> critical;
- disk: <= 500 MB free -> degraded; <= 100 MB -> critical;
- CPU: >= 70 C -> degraded; >= 80 C -> critical.

## Portal alarms

The station-side work intentionally uses the existing sensor API so it can be
validated without requiring a simultaneous server migration. The web portal
still needs an alarm layer that interprets `health_*` records, shows active
faults, detects a missing `health_application` heartbeat, sends one notification
per state transition, and sends a recovery notification when the component
returns to healthy.
