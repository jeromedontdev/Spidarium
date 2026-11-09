# Spidarium v1.1.7

## Screen lock is reliable

Two bugs made the middle button feel unreliable:

1. `power_sleep()` set `s_sleeping = true` itself. `power_tick()` then compared
   the computed state against `s_sleeping`, found them equal, and skipped the
   branch that shows the overlay — so the screen often did not sleep at all.
   `power_sleep()` now only sets the sticky `s_locked` flag and lets
   `power_tick()` drive the overlay.
2. The long-press threshold was 1200 ms, so an ordinary deliberate press was
   silently treated as a hold and produced no action. It is now 700 ms, which
   still leaves BOOT and PWR their hardware roles for a real hold.

## Volume slider follows the buttons

`ui_menu_refresh()` was declared and called but its body had lost the
`refresh_page()` call, so pressing the volume buttons changed the audio level
without moving the slider. It now rebuilds the page, and the percentage label
updates with it.

## Verification

Build exit 0 with zero `main/` warnings, flash exit 0, device suite 47/47,
40-cycle soak `VERDICT: PASS` with 0 faults, `scanloop 5` and `render 500` both
at `largest_drift=0`, browser suite 73/73.

Lock verified on hardware: `locked=0` -> `buttons lock` -> `locked=1`, still
`locked=1` after 3 s, then `locked=0` after `buttons unlock`.

See the [progress log](../PROGRESS.md) for exact evidence.
