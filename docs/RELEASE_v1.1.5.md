# Spidarium v1.1.5

## Physical buttons — correct pin map

The button map was wrong in v1.1.3 and v1.1.4. This release uses the vendor's
documented pins:

| Button | GPIO | Short press | Long press |
|---|---|---|---|
| Left | 0 (BOOT) | volume down | ROM downloader (unchanged) |
| Middle | 16 (PWR) | lock / unlock | power off (unchanged) |
| Right | 18 (user) | volume up | — |

**Long presses are deliberately ignored.** A press held past 1.2 s produces no
app action, so holding BOOT still enters the ROM downloader and holding PWR
still cuts power. The pins are only ever configured as inputs; nothing drives
them low.

Every press is logged — `button N (gpio X) down`, then `short press (Nms)` or
`long press: hardware role` — so a mis-mapped pin is visible in the serial log
instead of silent.

GPIO16 is not a normal GPIO on every ESP32-S3 revision (it is the PSRAM/SPIHD
pin on octal-PSRAM parts), so each pin is configured defensively: if it cannot
be read as an input the module logs it and carries on with the others.

## Also in this release

- **Slider readouts update while dragging.** The brightness and volume
  percentage labels used to keep the value the menu was opened with.
- **Wandering explores the tank.** The wander target was drawn from the middle
  half of the playfield, so every leg pointed at the centre and the spider
  orbited and settled there. It now draws from 80% of the half-extent, and the
  stuck-recovery path picks a fresh random target.
- **Life stages change size.** Spiderling 75%, juvenile 87.5%, adult 100%, on
  both the device and the browser.
- **Browser splat on a catch**, and the care button matches the device's 44px
  target.

## Verification

Build exit 0 with zero `main/` warnings, flash exit 0, device suite 47/47,
40-cycle soak `VERDICT: PASS` with 0 faults, `scanloop 5` and `render 500` both
at `largest_drift=0`, browser suite 73/73.

Hardware log confirms the map: `buttons ready: left=0 mid=16 right=18`.

See the [progress log](../PROGRESS.md) for exact evidence.
