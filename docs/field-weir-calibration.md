# Field inlet weir calibration

Restored 2026-11-18 from Sean Morgenstern's commit `669e96c` (2026-08-05,
“Calibrated code with averaging/outlier elimination”). The combined sensor
program later retained these values. HC measures the inlet weir box serving
Gutter 1, not Cell 1 ponding depth.

| Parameter | Recovered value |
|---|---:|
| Effective sensor height | 11.8125 in |
| Crest offset above box reference | 3.3125 in |
| Sensor-to-crest reference | 8.5 in |
| V-notch included angle | 59.4 degrees |
| Discharge coefficient | 0.25 |

`head_in = max(0, 8.5 - measured_range_in)`.

The existing daemon uses the V-notch equation
`Q = (8/15) Cd sqrt(2g) tan(angle/2) head_m^2.5`, then converts m³/s to L/s.
The historical 0.001 cfs cutoff corresponds to approximately 0.922635 in head.
Below that threshold the restored flow series is withheld rather than reporting
an unsupported zero. The original script instead forced such flows to zero.

The flow series is `staging_inflow_flow_rate`; it is separate from legacy
`inflow_flow_rate`. `staging_hcsr04_depth` retains its identity but now reports
head above the crest. Before this restoration it used a provisional 48 in
reference. Do not mix the two periods as one calibrated head record. For positive,
unclipped old readings, subtract 39.5 in and clamp at zero to recover the new
reference; clipped old zeros cannot reconstruct range.

This restores a documented calibration, assuming the original sensor mounting
and weir geometry were retained. It does not constitute a new surveyed or
flow-tested calibration. The source's “22.95 sec per liter” note has no paired
head measurement and cannot independently validate Cd.

No model forcing was changed by this restoration. Before assimilation uses HC,
map inlet flow to the model inlet and avoid counting the same gutter runoff twice.

## Raw data retention

HC now also publishes `staging_hcsr04_depth_raw`: the mean measured range from
that same acquisition batch, before reference subtraction and clipping. No
second sensor instance or GPIO request is created. This is range in inches,
not echo duration, and is averaged over valid pings, not every individual ping.

The field already retains MaxBotix range (`staging_maxbotix_depth_raw`),
moisture A0/A1/A2 voltages (`*_raw`), and pressure loop current
(`staging_pressure_loop_current_raw`, mA). These are sensor-level inputs to
calibration, not necessarily native ADC counts or original UART frames.
Separate moisture/pressure raw entries sample independently from their
calibrated counterparts; do not assume exact paired acquisition timestamps.

## Dashboard and recovered history

The dashboard uses `staging_inflow_weir_head`, an additional calibrated-head
output selected with `weirFlow.headOutputSensorId`. Health continues to use
`staging_hcsr04_depth`, preserving existing sensor alarm mappings. The original
mixed-reference series is retained for traceability but is not the displayed
head chart. Flow uses `staging_inflow_flow_rate`. Pressure is shown as measured
loop current (mA), since its field depth calibration is still provisional.

11,352 readings from 2026-11-10 17:29 UTC through 2026-11-18 14:37:37 UTC
were reconstructed using the documented 48-inch reference. These range
estimates are stored separately as `staging_hcsr04_range_reconstructed`.
Calibrated head and above-threshold flow were then calculated into the display
series at the original timestamps. Original head and directly measured raw
records were not overwritten. Recovered values retain source rounding and
possible clipping/averaging limitations; they are not original raw samples.
Earlier configuration periods were excluded because their provenance was not
verified. Source data, reconstruction, and import scripts are archived in
Dropbox under `04 Modeling and Analysis/JM Sensor Assimilation/2026-11-18 HC Reconstruction`.
