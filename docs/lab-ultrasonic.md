# Lab trigger/echo ultrasonic replacement

On November 2, 2026 the user replaced the lab HC with an RCWL-9610A-design
ultrasonic board and reported powering it from 3.3 V. Existing trigger/echo
acquisition produced fresh readings. No new GPIO driver or protocol change is
required for its current trigger/echo mode.

| Setting | Lab value |
|---|---|
| Sensor ID | `lab_hcsr04_depth` (retained for history continuity) |
| Display name | Lab RCWL-9610A Ultrasonic Head |
| Type | `distance` |
| TRIG | GPIO17, physical pin 11 |
| ECHO | GPIO18, physical pin 12 |
| Poll interval | 5 seconds |
| Samples per reading | 10 |
| Unit | inches |

Keep the existing mounting reference until measured again. The lab's current
`totalLength` is 75.3 inches; this is a configuration value, not a calibration of
the replacement board. Stored output is `max(0, totalLength - measuredRange)`.
Hold a broad flat target at ruler-measured positions and compare inferred range
`totalLength - reportedHead` only for positive, unclipped head values. A zero
head can indicate a target beyond the reference and cannot reconstruct range.

The RCWL chip marking alone is not a guarantee for every breakout's supply or
echo-voltage limits. Verify the exact board and its printed pin labels. The lab
was observed operating at user-reported 3.3 V. The older 5 V HC divider instructions
remain applicable only to that older hardware; do not apply a 5 V output directly
to Pi GPIO. Whether the old divider remains installed on this lab replacement
must be confirmed physically; software cannot determine its wiring or peak voltage.

A SparkFun-distributed HC-SR04-R datasheet shows a board marked RCWL-9610A design
and specifies a 3.3–5 V supply, a trigger pulse of at least 10 microseconds and
measurement cycles longer than 60 ms. It is a reference, not proof of the user's
specific supplier/model. Keep the module in trigger/echo mode; alternate UART/I2C
modes would require a separately verified interface and configuration.

Reference: https://cdn.sparkfun.com/assets/6/7/a/d/1/Ultrasonic_Ranging_Module_HC-SR04-R__SEN-28798.pdf

Hardware tutorial sources and illustrations are kept in the shared JM Bioretention
Dropbox, not committed here. Only software-facing configuration and operating
notes belong in this repository.
