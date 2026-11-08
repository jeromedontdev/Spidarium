# Spidarium v1.1.2

## Settings

- **Battery and Display are separate menus.** Settings is now WiFi, SD Card,
  About, Battery, Display, Sound, Auto Feed, Save Screenshot, Wipe Spider.
  Battery holds status, the runtime estimate and the automatic power-saving
  policy; Display holds the brightness slider and Screen Rotation.
- **Volume slider.** Sound now has a 0..100 volume slider plus on/off. The value
  is saved with the pet (save schema v4, `volume` at offset 131).
- **Brightness slider.** Replaces the 25/50/75 presets with a 10..100 slider.
  `save_raw_valid()` now accepts the full range instead of only the presets.
- **Runtime estimate.** Battery shows a stated heuristic: the PMIC percentage
  scaled by a nominal 300-minute full-charge runtime and a brightness factor.
  It reads "Charging", "On USB Power" or "Battery Status Unavailable" rather
  than inventing a number.
- **Screen rotation.** Upright / Sideways Left / Upside Down / Sideways Right.
  Rotation is an LVGL transform on a full-size wrapper, because the adapter
  never re-reads MADCTL after init and `bsp_display_rotation_set()` had no
  visible effect. Every layer — playfield, HUD, sleep overlay, menu panel and
  backdrop — is parented to `power_rotation_layer()`, so the menus turn with the
  playfield.
- **Auto Feed is one switch** driving both wild prey and unattended hunting.
- **Wipe Spider** replaces New Tank and is always the last Settings row.

## Minigames

- **Bug Hunt** shuffles the target every 1.5 s easing to ~1 s (was 700 ms easing
  to 420 ms), and the decoy species are shuffled so the answer is no longer the
  only unique bug on screen.
- **Quick Tap** is a reaction test again: a 620 ms window easing to ~300 ms with
  a 900–2500 ms wait (was 850 ms easing to 465 ms with a 1100–3500 ms wait).
- **Color Match** keeps its 2000 ms window; the shared non-reaction ramp was
  retuned from `2600 - round*160` to `2000 - round*110`.
- **Rewards scale with score:** `2 + score*4` XP and `score*3` Silk (was
  `4 + score*3` XP and `2 + score*2` Silk, which paid a flat 2 Silk even for a
  blank run).

## Art and HUD

- **Poop is a mound**, not a single green pixel: four stacked brown lobes on the
  device and in the browser.
- **Battery icon** moved right and up to `(412, 8)` on both platforms.
- **Front legs lead the gait** (1.55x / 1.15x swing and bend), so the spider no
  longer looks like it drags its front pair.
- **Hunt is less forgiving:** `CATCH_PERFECT_PX` 44 -> 30 and `CATCH_GOOD_PX`
  78 -> 54.

## Browser

- **Squircle shell.** The viewport, screen and canvas use a 44px radius matching
  the device panel, including in fullscreen.
- **Page ear** is a flush corner triangle: an 80px box at `(-30,-30)` so the
  diagonal passes through the screen origin and the rounded shell clips it to
  the same curve.
- Battery and Display are separate menus, matching the device.

## Save format

Save schema v4 appends `volume` at offset 131. The byte fits existing tail
padding, so the payload stays 132 bytes and the legacy 120-byte payload is
unchanged. v2/v3 blobs migrate `volume` to the compiled default; values above
100 are rejected.

## Verification

Build exit 0 with zero `main/` warnings, flash exit 0, device suite 47/47,
40-cycle soak `VERDICT: PASS` with 0 faults, `scanloop 5` and `render 500` both
at `largest_drift=0`, browser suite 73/73.

See the [progress log](../PROGRESS.md) for exact evidence and the
[feature guide](README.md) for current screenshots.
