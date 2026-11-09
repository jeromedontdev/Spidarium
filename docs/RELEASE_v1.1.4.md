# Spidarium v1.1.4

## Physical buttons

- **Left** steps the volume down, **right** steps it up, both repeating while
  held. The pin map was wrong in v1.1.3: the left button was wired to the
  volume-up action and the middle button to nothing useful.
- **Middle** locks and unlocks the screen.
- Every press is logged (`button N (gpio X) pressed`) so a mis-mapped pin is
  visible in the serial log instead of silent.

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

"Pocket Tank" is now mentioned only once, in the README credit, as inspiration.
The architecture and console docs describe this project's own rules.

## Verification

Build exit 0 with zero `main/` warnings, flash exit 0, device suite 47/47,
40-cycle soak `VERDICT: PASS` with 0 faults, `scanloop 5` and `render 500` both
at `largest_drift=0`, browser suite 73/73.

See the [progress log](../PROGRESS.md) for exact evidence.
