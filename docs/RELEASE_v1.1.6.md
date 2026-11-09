# Spidarium v1.1.6

## Screen lock now works

The middle button (GPIO16, PWR) sets a sticky lock flag. `power_tick()` used to
recompute the sleep state from the idle timer every frame and immediately undo
the lock, so the button appeared to do nothing. The idle timer can no longer
clear a manual lock; only a touch or `power_wake()` releases it.

## Sliders follow the hardware buttons

Pressing the volume buttons while the Sound menu is open now moves the slider
and updates the percentage. `volume_step()` calls the new `ui_menu_refresh()`,
which rebuilds the current page from the item table on the LVGL task.

The slider readout also updates while dragging, which it previously only did
when the row was rebuilt.

## Also in this release

- **Correct button pin map** (from v1.1.5): left GPIO0 volume down, middle
  GPIO16 lock, right GPIO18 volume up. Long presses are ignored so BOOT and PWR
  keep their hardware roles.
- **Wandering explores the tank** instead of orbiting the centre.
- **Life stages change size**: spiderling 75%, juvenile 87.5%, adult 100%.
- **Browser splat on a catch**, and the care button matches the device's 44px
  target.
- **"Pocket Tank"** appears only in the README credit, as inspiration.

## Verification

Build exit 0 with zero `main/` warnings, flash exit 0, device suite 47/47,
40-cycle soak `VERDICT: PASS` with 0 faults, `scanloop 5` and `render 500` both
at `largest_drift=0`, browser suite 73/73.

Lock verified on hardware: `locked=0` -> `buttons lock` -> `locked=1`, still
`locked=1` after 3 s, then `locked=0` after `buttons unlock`.

See the [progress log](../PROGRESS.md) for exact evidence.
