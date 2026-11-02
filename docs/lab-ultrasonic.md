# Lab trigger/echo ultrasonic replacement

On November 2, 2026 the user replaced the lab HC with an RCWL-9610A-design
ultrasonic board and confirmed powering it from 3.3 V, with TRIG directly
to GPIO17 and ECHO directly to GPIO18, without the old divider. Existing trigger/echo
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

## Confirmed wiring and reference photos

The user confirmed direct TRIG17/ECHO18 connections on the board at 3.3 V.
No ECHO divider is used in this lab arrangement. The old 5 V HC instructions
remain historical references, not the wiring for this replacement. Software
cannot measure the physical peak ECHO voltage; retain proper protection for
any different board that outputs 5 V.

The previously saved SparkFun HC-SR04-33 / SEN-24049 product link has board
front and rear reference photographs. The supplier rear photograph is marked
RCWL-9610 design; the lab board was described by the user as RCWL-9610A design.
These are product-reference photographs, not photographs of the installed lab
wiring. Use the actual board markings for identification.

Reference: https://www.sparkfun.com/catalog/product/view/id/8141

The current trigger/echo driver is retained. A 10-microsecond trigger and spacing
greater than 60 ms between samples are compatible with this interface. Alternate
UART/I2C modes, if used later, require a separately verified configuration.

## Commissioning observation

At 14:13 EST on November 2, 2026, five consecutive readings were 9.07–9.23 inches
of reported head over approximately 20 seconds, with healthy acquisition. This
is a short stability observation, not a ruler-based accuracy calibration.

Hardware tutorial sources and illustrations are kept in the shared JM Bioretention
Dropbox, not committed here. Only software-facing configuration and operating
notes belong in this repository.
