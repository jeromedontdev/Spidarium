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

v1.1.18 adds explicit ESP32 deep sleep, not a new battery cutoff/charger
implementation. The hardware's own protections remain in charge. Real
unplugged current, discharge/runtime, rail shutdown, charging transitions and
right-button wake require physical evidence; do not infer them from USB tests.

## User behavior

Settings -> Battery shows battery/USB/charging status, a runtime estimate and the
configurable power-saving policy. Settings -> Display holds a 10..100% brightness
slider and the Screen Rotation control. The
brightness preference is saved with the pet; new and migrated saves default to
50%. The runtime estimate is a stated heuristic (nominal 300 minutes at 50%
brightness, scaled by the brightness factor), not a coulomb count, and it reads
"Charging", "On USB Power" or "Battery Status Unavailable" rather than inventing
a number. Rotation is an LVGL transform on a full-size wrapper holding the whole
UI, because the adapter never re-reads MADCTL after init; every layer, including
the menu panel and the sleep overlay, must be parented to
`power_rotation_layer()` or it stays upright while the rest turns. The panel is
square, so taps still map to the same coordinates. A persistent top-right battery gauge shows charge fill/percentage and a
charging bolt; USB/unknown readings remain explicit. Tapping opens voltage,
power source and brightness details. Care shows only Silk as wallet text.

- Profiles: Balanced; Battery Saver caps battery brightness at 35%; RTC Sleep
  caps it at 20% and permits the separately configured idle deep-sleep delay.
  USB does not apply the saver caps.
- Screen timeout: 30 seconds, 1, 2 or 5 minutes, or Never. Default 5 minutes.
  Dim to at most 15% after 60 seconds or half the selected timeout, whichever
  is earlier. Never disables blanking, not idle dimming.
- At timeout, set brightness to 0 and reuse the black touch-wake overlay.
  Tank motion/rendering stop; needs/aging and service polling continue. Queue
  a pet save. Ordinary screen blanking is not deep sleep.
- A touch on the overlay resets inactivity and restores selected brightness,
  without propagating that first touch into a hunt or menu selection.
- At <=10% with a battery present and no USB, cap brightness at 5% and request
  one save per low-battery episode. USB power removes that cap. The five-minute
  configured screen-off policy still wins.

Deep-sleep idle delay: Never (default), 5, 15 or 30 minutes, or 1 hour.
Automatic entry requires RTC Sleep mode, a nonzero delay, valid battery power
without USB, the main tank, and idle persistence/radio workers. RTC wake:
15 minutes (default), 1 hour or 6 hours. Deep Sleep Now is a separate explicit
confirmation and can run on USB. Wake by the right/user button (GPIO18,
active low) or ESP RTC timer, **not touch**. A held wake button rejects entry.
The PCF85063/system clock and saved `last_seen` drive normal offline catch-up
after waking; normal gameplay/timers are not running during deep sleep.
The MCU RTC timer wakes it even without an external RTC backup battery;
retention of external RTC time after losing main power needs its own test.
No flash erase or battery cutoff occurs.

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

Deep-sleep requests run on LVGL, freeze input and copy the pet into a save job.
The internal-stack save worker commits it, drains active audio/power work with
a three-second bound, configures timer/GPIO18 wake and pauses the display with
acknowledgement before blanking and entering sleep. Pending sleep rejects
new persistence/radio jobs. Errors log explicitly, clear pending state and
restore input on LVGL; workers never call `lv_*`. Failed pause/setup does not
silently proceed to sleep. These preferences are appended in save v9.

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
`tools/test_deep_sleep.py COM7` requests a five-second diagnostic nap, observes
USB disappearance/reconnection and verifies `wake_cause=4` (ESP RTC timer),
plus saved habitat preferences. It does not prove physical-button wake or
measure battery current. `nap SECONDS` is queue acceptance only until actual
entry/wake evidence is collected.
