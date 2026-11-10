# Device-coordinate browser port and v1.1.0 verification

## v1.1.8 follow-up

Clean pays for the mess it removes on both targets, using one formula:
`score = dirt + (100 - hygiene)`, `xp = ceil(score * 3 / 24)` capped at 3. A
spider dropping is 12 dirt and a bug speckle 6, so one speckle pays 1 XP, two
pay 2, and three or more pay 3. Clean is never disabled on either target; a
spotless tank simply pays nothing, and repeated taps cannot farm it because the
mess is gone after the first one. A tank holding only bugs is still cleaned --
the bugs are shooed out -- for 0 XP.

Bugs left in the tank now foul it on both targets: a pale speckle appears near
the bug every `BUG_POOP_MS` (2.5 min), worth half a dropping. The browser
renderer branches on `poop.bug` so a speckle is drawn small and cool
(`#8d8a7a` / `#b5b2a2` / `#d8d5c6`) while a dropping stays warm brown. Poop
entries carry optional `dirt` and `bug` fields; a save without them defaults to
a spider dropping, so older saves keep working.

Browser suite 77/77 (was 73). The shipped bundle was verified in a real browser:
the scaling table, the anti-bypass case, and the pale palette (0 pixels on a
clean tank, 24 for three speckles, 0 for a dropping).

## v1.1.1 follow-up

The current website uses mint promotional fangs (player colors unchanged),
an unboxed HUD-only battery at (400,132), and an explicitly positioned square
playfield whose ear is flush in desktop and fullscreen layouts.
Settings > Save Screenshot downloads only the 480x480 canvas as PNG.
Native Settings saves a clean playfield RGB565 BMP to SD; serial can capture
active menus, clean playfield or the alpha spider. See
[capture instructions](serial_console.md#screen-capture).

v1.1.1 build/flash passed with no main-code warnings; hardware 39/39,
host 21/21, browser 73/73, 40-second stability PASS with zero faults.
Capture comparison proves menu pixels and pet state unchanged; six SD images
were completely read back and matched. Warm screenshot writes and `render 500`
have zero internal/DMA drift (largest 63,488); five scans have zero drift
(largest 69,632). The compiled 24-field save schema is unchanged.
Browser PNG output was decoded as 480x480 with the exact PNG signature while
Settings remained visible. Diagnostic captures remain uncommitted.

The evidence below records the immutable original v1.1.0 publication.

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
locally with its OFL notice. Promotional canvases and the preview PNG use mint
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

## Publication

- Source/tag: `e6ef5a26d078aea46a547a4d715266baf0d273a3`, `v1.1.0`.
- [Published release](https://github.com/jeromedontdev/Spidarium/releases/tag/v1.1.0)
  contains the full-flash ZIP, standalone OTA application and project LICENSE.
- Public ZIP SHA-256:
  `0977d79ccb06fbccad18d13f1dde679867e12ad30c7ea152d82f63177baed09c`.
  Actual unauthenticated download matches; LICENSE, compiled schema and
  verification report are present.
- [Pages run 42440255817](https://github.com/jeromedontdev/Spidarium/actions/runs/42440255817)
  completed successfully. Only the new `v1.1.0` tag was added to the environment
  policy; existing `main`/`v1.0.0` permissions remain intact.
- [Live website](https://jeromedontdev.github.io/Spidarium/site/) verified:
  new UI/engine, purple preview, local font and project license match source;
  all five firmware hashes and all 134 PNGs match. Compiled schema values are
  identical; Windows/Linux line-ending differences are not schema differences.
- Public browser confirms Fullscreen, native-coordinate controls and absence
  of the old side-panel game. Existing progress is not reset for screenshots.
- Original v1.0.0 tag/archive remain unchanged. Publication evidence is a
  documentation-only follow-up; the v1.1.0 code tag is not moved.
