# Battery and display management

## Hardware and limits

The Waveshare ESP32-S3-Touch-AMOLED-2.16 has an **AXP2101** PMIC at I2C address
0x34. It shares the BSP I2C bus with the touch/RTC/codec. Sources:

- [Board hardware resources and AXP2101 datasheet](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-2.16/Resources-And-Documents)
- [AXP2101 register/API reference](https://github.com/lewisxhe/XPowersLib/blob/master/src/XPowersAXP2101.hpp)

`main/power.c` identifies chip register 0x03 (expected 0x4A), enables only the
battery-voltage ADC channel (register 0x30 bit 0), and leaves charger current,
charge voltage, thermistor configuration, rails and power-button settings
unchanged. Do not guess charging settings for an unknown attached battery.

Status registers 0x00/0x01 supply battery-presence, valid USB and charging
flags. Voltage is the 13-bit value at 0x34/0x35 in mV; 0xA4 is the PMIC's
percentage. Values over 100 are labelled Gauge Unknown, not clamped into a
fabricated percentage. No battery and I2C failures have explicit UI status.
The percentage is the PMIC estimate, not an independently calibrated fuel gauge.

This is display power saving, **not** ESP32 deep sleep or a new battery cutoff/
charger implementation. The hardware's own battery protections remain in
charge. Real unplugged discharge/runtime and charging-transition testing need
physical access and a suitable battery; do not claim those from USB testing.

## User behavior

Settings -> Battery and Display shows battery/USB/charging status and offers
25%, 50% or 75% brightness. The preference is saved with the pet; new and
migrated saves default to 50%. A persistent top-right battery gauge shows charge fill/percentage and a
charging bolt; USB/unknown readings remain explicit. Tapping opens voltage,
power source and brightness details. Care shows only Silk as wallet text.

- After 60 seconds without touch, dim to at most 15%.
- After 300 seconds, set brightness to 0 and show a black full-screen overlay.
  Tank logic/rendering stop; service polling remains alive. Queue a pet save.
- A touch on the overlay resets inactivity and restores selected brightness,
  without propagating that first touch into a hunt or menu selection.
- At <=10% with a battery present and no USB, cap brightness at 5% and request
  one save per low-battery episode. USB power removes that cap. The five-minute
  screen-off policy still wins.

Screen sleep does not age/decay the spider while hidden. No automatic power-off
or flash erase occurs. A completed game reward or gift purchase follows the
existing copied-save queue; screen sleep also requests a snapshot.

## Thread and memory ownership

A 3,072-byte PSRAM-stack worker reads the PMIC once per second. An atomic packed
snapshot publishes voltage/percentage/status together. LVGL polls it; no I2C
or LCD command occurs from its timers.

Brightness changes run on the worker, using the existing adapter pause/
acknowledgement and resume around the BSP LCD command. The worker publishes
`power_writing()` before rechecking `net_busy()`. The network worker drains
that state before acquiring its own display pause. This prevents one worker
from resuming a display still owned by the other. The power worker skips
changes during active persistence and radio work. Pause/resume/read failures
are logged explicitly.

The black overlay is built once on LVGL and reused. Touch wake and all object
mutations stay on LVGL. The console's `wake` requests that same LVGL action;
it never directly calls LVGL from the serial task.

## Verification

`battery` prints real PMIC telemetry and the screen-sleep flag. `uitest`
verifies low/USB/idle brightness boundaries and the actual overlay press/wake
callback without changing the physical battery. Synthetic tests do not prove
charging or discharge behavior.

For actual inactivity testing, keep **one** serial connection open. A new
connection may reset the board and restart the idle clock. Capture brightness
15 after a minute, brightness 0 and `screen_sleep=1` after five minutes, then
send `wake` and confirm `screen_sleep=0` and restored normal brightness.
See the latest [progress round](../PROGRESS.md) for recorded hardware results.
