# One-minute acquisition and summarized history

Applied to the field and lab Pis on November 4, 2026. Distance and MaxBotix sensors now publish once per 60 seconds, averaging five accepted measurements taken in a short burst. Failed samples are excluded; if no sample succeeds, no valid measurement is published. This is not a continuous average over the whole minute.

Pressure/current channels publish every 60 seconds and retain their previous three-sample setting. Moisture and BMP390 channels remain at 60 seconds with their previous settings. Adaptive interval scaling is disabled for a fixed cadence. Calibration, sensor IDs, hardware mapping, camera schedule, and historical records are unchanged.

Both Pis saved before/after configs under `/home/behzad/field-setup/one-minute-sampling-20261104T171922Z/`. The repository default config carries the same timing policy; preserve each station's active wiring and calibration when applying it.

## History API

Raw history remains the default. Add `interval_seconds=60` (or an integer from 60 through 86400) to a sensor GET query with `start`, `end`, and optional `project` to request arithmetic means grouped into aligned time buckets. Each bucket reports `sample_count`, `minimum`, `maximum`, and `interval_seconds`. Missing buckets are gaps, not invented zeroes. Health events cannot be averaged.

The API streams source records page by page to bound memory. Summaries reduce response size and browser work; they do not precompute database rollups or remove the need to read matching source records.

## Dashboards

Both dashboards request raw readings for ranges up to six hours. Larger ranges select 1-, 5-, 15-, 30-, 60-, 360-, or 1440-minute buckets, targeting roughly 2,000 points (very long ranges can exceed that target). Titles use “Latest” and a point count; the chart tooltip explains the averaging interval and how to inspect original readings. In summarized views, the displayed latest value is an average, not an instantaneous reading.

Session caching is scoped to the summary resolution. Auto-refresh fetches only the new tail plus the complete last time bucket, replacing partial means instead of double-counting. Narrowing the range to six hours or less requests the original records again. A bucket timestamp represents its start, not a separate measurement at that exact time. The partially overlapping first bucket can fall outside the selected chart bounds.

## Validation

Run `python3 -m unittest discover -s tests -p test_history_summary.py -v`. Tests cover means/counts/extrema, gaps, invalid values, units, explicit time zones, and preserving input records. The API was also checked against paginated fake database records and real raw/summary history responses before deployment.
