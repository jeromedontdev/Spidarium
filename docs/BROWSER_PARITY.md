# Device-coordinate browser port and v1.1.0 verification

## Contract

The browser is a JavaScript port of the device game, not an ESP32 instruction-set
or LVGL emulator. `site/lib/device-engine.js` and related modules port the native
numeric FSM, 20 Hz needs/behaviour/prey updates, distance-driven gait, pounce/
accuracy/catch resolution, progression/achievements and eighteen gifts.
`device-ui.js` uses the 480x480 native-coordinate setup, care HUD, page ear,
three-row menu pagination and three eight-round minigames. The browser viewport
scales the entire world uniformly; desktop maximum is 800x800 logical CSS pixels.
Fullscreen preserves the square and exposes Exit Fullscreen.

The game uses actual background/prey/gift/badge PNGs and the direct pixel port
of `cspider.c`, never obsolete spider animation sheets. Montserrat is bundled
locally with its OFL notice. Promotional canvases and the preview PNG use purple
fangs; player colors and real device captures are not recolored.

## Platform adaptations

- PMIC voltage/charging and brightness: explicitly unavailable in the browser,
  not fake percentages or simulated charger readings.
- WiFi/SNTP/OTA: browser menu routes to actual local USB tools. No fake radio
  connection; credentials are never stored in browser progress.
- SD Card: downloads/restores browser JSON; actual board binary/SD backups are
  separate and continue through the serial/firmware paths.
- Local saves: retain `spidarium.browser-habitat.v1`; valid older browser
  progress migrates. Invalid schemas, nonfinite/out-of-range values, corrupt
  JSON and unavailable/quota-limited storage report errors instead of silently
  resetting or overwriting the original. No offline decay. Clearing browser
  storage/private browsing can remove progress: download a backup.
- Web Audio uses native 16 kHz sweep/envelope/noise synthesis, unlocked only
  by a browser gesture. Device audio configuration remains untouched.
- Native MOLT's existing transition behavior is preserved, rather than
  inventing a browser-only exit. This port does not silently fix native bugs.

## Manual/default automation

Auto Feed and Auto Hunt are independent Settings checkboxes, OFF in new and
migrated saves. Manual Feed adds prey; drag/release pounces. Wandering and gift
visits continue. The former ungated game-level wild/visitor spawning block
was removed; the gated `prey_tick` path owns all automatic spawning.

Raw save v3 retains header12/payload132/raw144. Flags use former padding bytes
129/130 and are exposed by actual compiled schema fields, `minVersion:3`.
Legacy v2 always migrates OFF regardless of padding. Invalid v3 flags reject.
Never restore a v3 backup onto older firmware.

## Verified on COM7 and in the browser

- Wrapper build exits 0; no `main/` warnings. Wrapper flash exits 0.
- Pet-preserving hardware suite: **39/39 PASS**.
- 40-second stability: **PASS**, zero faults/errors.
- `mem`: largest internal/DMA block **63,488 bytes**, above 51,200 baseline.
- `render 500`: largest-block drift **0**.
- `scanloop 5`: **5 successes**, largest-block drift **0** (69,632 before/after).
- Compiled schema: **24 fields**, version3; valid/invalid raw restores,
  overwrite guard and reboot/readback PASS. Both Settings checkboxes were
  enabled through the real callback, persisted across reboot, then the exact
  original pet/options were restored. Console headroom after restores: **3,932**.
- Browser suite: **71/71 PASS**, including engine parity, corruption protection,
  save migration, v2 padding, checkbox validation, minigame deadlines/feints/
  memory/input guard and native PCM timing.
- Host packaging/capture/console tests: **19/19 PASS**.
- Browser interaction: blank taps leave menus open; three-row Settings page
  shows independent checkboxes and preserves the page when toggled; Auto Feed
  survives reload; minigame completion pays once; fullscreen enters/exits with
  a square viewport; observed desktop viewport800 and maximum800.
- Desktop/mobile/HUD screenshots refreshed from the actual browser without
  resetting existing progress.

Initial soak caught attempted association with an unreachable stored network
and WiFi teardown timeout. Boot now primes allocation only, preserving stored
credentials and on-demand scans; the final soak and repeated scans pass.

## Boundaries still requiring physical/manual acceptance

Physical browser USB flashing, unplugged battery runtime and subjective touch/
art acceptance are not certified by simulated browser transport tests.
Network availability and HTTPS OTA endpoint behavior depend on the chosen
network/server. Existing hardware progress/credentials were never erased.
