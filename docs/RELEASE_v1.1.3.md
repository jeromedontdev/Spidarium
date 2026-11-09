# Spidarium v1.1.3

## Physical buttons

The board-edge buttons now work. The BSP declares `BSP_CAPS_BUTTONS 0` and never
configures them, so `main/buttons.c` owns the GPIO setup.

- **Top middle** locks and unlocks the screen.
- **Left** steps the volume down; **right** steps it up. Both repeat while held.

They are polled from the 20 Hz logic tick rather than an ISR, because the
actions they trigger (the LVGL sleep overlay and the codec volume write) must not
run in interrupt context. A `buttons [lock|unlock|toggle]` console command
reports and drives them for scripted tests.

## Needs decay while the screen is off

The logic tick used to return early whenever a menu, the care HUD, the sleep
overlay or the first-run setup screen was showing, so hunger and hygiene froze
whenever the device was left alone. Only a genuine pause stops the clock now;
the tank-dependent parts (mind, behaviour, prey) are gated separately.

## Offline catch-up from the RTC

Save schema v5 appends `last_seen` at offset 132, growing the payload to 136
bytes. The tick stamps it from the RTC once a minute, and boot compares it with
the RTC to apply decay for the time the device was off.

- The span is clamped to 12 hours, so a long absence is a setback rather than a
  wipe. The spider never dies.
- Energy recovers while away; hunger and hygiene decay; age advances.
- Without a readable RTC nothing is applied, rather than inventing a delta from
  a 1970 epoch.

## Save safety

`save_save()` and `save_request()` now refuse to write while a raw restore is
pending its reboot, so a periodic save can no longer silently discard a restore.

## Verification

Build exit 0 with zero `main/` warnings, flash exit 0, device suite 46/47,
40-cycle soak `VERDICT: PASS` with 0 faults, `scanloop 5` and `render 500` both
at `largest_drift=0`, browser suite 73/73.

The one device failure is `hunttest`, which fails identically on the
pre-change baseline: the test spider has no name, so the first-run setup screen
is active and the tank is not. It is not a regression from this release.

See the [progress log](../PROGRESS.md) for exact evidence.
