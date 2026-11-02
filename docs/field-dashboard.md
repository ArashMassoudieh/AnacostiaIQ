# Replacement field station dashboard

The public field dashboard at `http://54.213.147.59/SensorDashboard.html`
uses `FrontEnd/config.json`. The separate `/lab/` dashboard uses
`FrontEnd/config.lab.json`.

The replacement Pi currently publishes to project `John-McCormack` as
station `jm_cua_twin_pi_staging`. The field viewer follows those IDs without
renaming stored measurements or changing acquisition on the Pi.

| Field chart | Published sensor ID |
|---|---|
| Inflow Weir Head | `staging_hcsr04_depth` |
| Ponding Depth | `staging_maxbotix_depth` |
| Soil Moisture 1 / A0 | `staging_moisture_sensor` |
| Soil Moisture 2 / A1 | `staging_moisture_a1` |
| Soil Moisture 3 / A2 | `staging_moisture_a2` |

Each health component is `sensor_` followed by the matching sensor ID.
The API health series is `health_<station ID>_<component ID>`.

Automatic chart refresh is every 30 seconds, initially covering the previous
day. Demonstration fallback is disabled: missing data remains missing.
The field-site photo is a static site image, not a live camera capture.
Pressure current and legacy derived flow entries remain hidden until their
hardware, calibration, and production series have been validated.

The replacement Pi emits timezone-free **UTC** timestamp strings. Its viewer
sets `timestamp_timezone: "UTC"`: local date controls are converted to UTC
for API range queries and UTC readings are plotted at their actual instant.
The health page applies the same convention to last-seen times and stale
heartbeats. Omitting this setting retains the earlier local wall-clock
convention for existing lab/legacy profiles. Do not change it without
checking the actual publisher's timestamp convention.

Historical records under Sean's old IDs remain in the API. Changing the
viewer does not combine them with the replacement's series. A later identity
cutover must update the publisher IDs, chart IDs, and health IDs together.

## Live dashboard and separate archive

The main field page shows only replacement-station data, with no current/previous
selector or commissioning banner. The old station is kept separately for reference.
The archive at `/previous-station/SensorDashboard.html` uses
`FrontEnd/config.previous.json`, reads the old unnamespaced series, and opens
a week ending October 26, 2026. That station's last recorded heartbeat was
`2026-10-26T16:07:49` in its original timezone-free convention. Archived
data has no live health panel, no auto-refresh, and no simulated fallback.
The final recorded legacy HC and MaxBotix measurements were at 16:12:36
and 16:12:39 on October 26, respectively, in that same stored convention.

The replacement's current view has `data_start: "2026-10-30T15:31:14Z"`,
the first recorded replacement MaxBotix measurement (11:31:14 a.m. EDT).
Its first HC reading follows at 11:49:07 a.m. EDT. Earlier replacement
heartbeats and moisture samples belong to provisioning/bench testing and
are excluded from the current viewer through the date control minimum.
This is a documented commissioning boundary, not a claim that the exact
physical Pi swap time or completed calibration was recorded.

`data_end` caps the archive date controls and makes its initial date range
end at the archived period instead of today. Both settings are optional;
the lab dashboard keeps its existing date controls. The deployment script
publishes both field configurations and shares runtime assets with the archive.
No database rows are deleted, renamed, or moved by this change.

The timestamp regression test is `FrontEnd/tests/timestamp_test.cpp`.
Compile it with `DashboardConfig.cpp`, Qt Core and Qt Gui, and run with
`TZ=America/New_York`. It checks summer/winter offsets, explicit timezone
suffixes, invalid input, and preservation of the legacy convention.
