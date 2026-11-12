# Spidarium v1.1.4

## Physical buttons

The board-edge buttons now work, using the vendor's documented pin map:

| Button | GPIO | Short press | Long press |
|---|---|---|---|
| Left | 0 (BOOT) | volume down | ROM downloader (unchanged) |
| Middle | 16 (PWR) | lock / unlock | power off (unchanged) |
| Right | 18 (user) | volume up | — |

The BSP declares `BSP_CAPS_BUTTONS 0` and never configures them, so
`main/buttons.c` owns the GPIO setup. They are polled from the 20 Hz logic tick
rather than an ISR, because the actions they trigger (the LVGL sleep overlay and
the codec volume write) must not run in interrupt context.

**Long presses are deliberately ignored.** A press held past 1.2 s produces no
app action, so holding BOOT still enters the ROM downloader and holding PWR
still cuts power. The pins are only ever configured as inputs; nothing drives
them low.

Every press is logged (`button N (gpio X) down`, then `short press (Nms)` or
`long press: hardware role`), so a mis-mapped pin is visible in the serial log
instead of silent. A `buttons [lock|unlock|toggle]` console command reports and
drives the lock state for scripted tests.

## Slider readouts

The brightness and volume sliders now update their percentage label while you
drag. Previously the label kept the value the menu was opened with until the row
was rebuilt, so the slider looked like it was not doing anything.

## Spider behaviour

- **Wandering explores the tank.** The wander target was drawn from the middle
  half of the playfield, so every leg pointed at the centre and the spider
  orbited and settled there. It now draws from 80% of the half-extent, and the
  stuck-recovery path picks a fresh random target instead of aiming at the
  middle.
- **Life stages change size.** A spiderling renders at 75%, a juvenile at
  87.5%, and an adult at 100%, on both the device and the browser, so a molt
  reads as growth rather than a label change.

## Browser

- **Splat on a catch.** Pouncing on a bug now bursts a short ring of the
  species colour where it was caught, fading over 700 ms.
- **Smaller HUD button.** The care button matches the device's 44px target
  instead of the oversized browser default.

## Documentation

The architecture and console docs describe this project's own rules.

## Verification

Build exit 0 with zero `main/` warnings, flash exit 0, device suite 47/47,
40-cycle soak `VERDICT: PASS` with 0 faults, `scanloop 5` and `render 500` both
at `largest_drift=0`, browser suite 73/73.

See the [progress log](../PROGRESS.md) for exact evidence.
