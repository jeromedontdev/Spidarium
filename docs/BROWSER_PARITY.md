# Device-coordinate browser port and v1.1.0 verification

## v1.1.18: habitat controls and prey movement

Automatic ambience is disabled on both platforms, superseding the scheduling
below; manual diagnostic textures and foreground effects remain available.
Decor long press tolerates up to 12 pixels of finger jitter; a real drag cancels
the hold. Arrange Decor and Placed Gift provide discoverable alternatives.
Gift movement keeps variant, ownership, effective bonus and Silk unchanged.
Placement release cannot also pet or pounce.

Menus share framed icons, pixel rivets and a gold header ornament clear of Back.
Daily rows have task icons and Backgrounds uses real habitat thumbnails.
Sixteen backgrounds cost 0-48 Silk, bought once; re-equipping owned themes is
free. v8 migration retains the old equipped background plus cork.
Save v9 appends ownership/selection and power preferences without changing
older offsets. The actual compiled schema has 41 fields and a 248-byte payload.

Ground bug frames follow distance traveled, with planted feet during cricket
rests and flee recovery. Winged prey retain timed wing beats. Both renderers
turn the right-facing art in eight headings without antialiasing and display
the full 48x48 asset. Caught prey resolve the splatter, not a living pose.
Six thoracic legs alternate tripods; larvae keep short true legs near the head.
Spider geometry, exports and renderer remain unchanged.

The browser stores the same Battery preferences, but labels them Device
Preference. It does not emulate ESP32 deep sleep, PMIC data or physical wake.
See [battery management](BATTERY_MANAGEMENT.md).

## v1.1.17: quiet environmental audio

Native/browser synthesis now shares eight-second filtered air and ten-second
leaf textures: no automatic chirp, downward plop or pitch sweep. Stateful
integer noise filters, two-second attacks, three-second releases and a gentle
seven-second swell keep the background quiet. Scheduling considers ambience
after twelve seconds, then every 14-20 seconds, skipping busy audio; active
hunting suppresses ambience. Seeded samples match native `audiotest` hashes.

Pounce is a filtered noise whoosh; other foreground effects are quieter and
petting retains a short, soft chirp. Foreground audio fades existing ambience
over 20 ms. Browser volume now controls a master gain; muting fades/stops
already-playing sources, including when Sound settings are open. Hidden tabs
also silence existing sound. Source nodes are disconnected on completion.
Unchanged per-frame settings do not enqueue repeated gain-automation events.
Native ambience reuses its PSRAM buffer, yields to pending effects/mute/radio
between 100 ms chunks, and reports actual codec-write results.

Matching PCM does not promise identical speaker/headphone loudness or
subjective atmosphere. Listening approval remains with the player.

## v1.1.16: shared SNES artwork and chrome

All 119 non-spider PNGs now use the same finite, crisp material ramps: sixteen
quiet terrain backgrounds, all decor/gifts, forty bug frames, thirteen HUD
glyphs and eleven badges. The 39 approved spider exports and both renderers
remain byte-identical.

Native/browser menus and all three games (Bug Hunt, Color Match, Quick Tap)
use flat two-pixel chrome. Bug Hunt targets and decoys use the refreshed prey
PNGs; Color Match and Quick Tap retain their native finite signal palette,
timing and scoring. Browser tests execute all eight bug-art paths, preview/
active color states and wait/feint/green states. Native `uitest` checks tile/
timer styling and actual Bug Hunt image descriptors alongside its existing
eight-round, cancellation and reward tests. Settings Back now returns to the
root instead of reopening Settings, so care and games remain reachable.

The final firmware's actual `save schema` response was captured: **v8**,
**35 fields**, payload sizes **120/236**, `decor_owned` at 160 and twelve
six-byte `decor` slots at 164. The tracked browser/release schema is updated
from that response, replacing the stale v5 source file; the firmware save
layout itself did not change. Packaging rejects a schema with an outdated
version or invalid/incomplete field bounds before creating the release.
The USB save inspector now accepts that v8 schema and 248-byte raw saves,
decodes all twelve little-endian decor slots, and keeps reading older v2/v5/
v6/v7 payload prefixes. Decor is displayed as JSON, not `[object Object]`.
The browser decoder also passed against a read-only actual device export.
Full-flash installation preserves NVS. Exact validation evidence belongs in
the newest [progress round](../PROGRESS.md).

## v1.1.14 follow-up: moving decor, shop icons, and sleep

Decor can now be moved on both targets. The device adds `decor_move()` and a
per-item menu (Blocking / Move / Put Away) opened by a long press on the item;
the browser adds `moveDecor()` and the same panel opened by a 550 ms pointer
hold. Moving keeps the slot, the kind and the blocking flag, and clamps inside
the playfield on both.

The shop no longer jumps pages. `ui_menu_open()` jumps to the page of the first
`selected` row, and the decor shop was marking owned kinds selected, so buying
anything past the third row yanked the player to page 2. The browser panel has
the same rule and had the same bug. Owned state is already in the subtext, so
the flag is gone on both.

Every decor row carries its sprite on both targets, in the shop, the Placed Items
list and the per-item menu.

Sleep is a real toggle on both. A player-initiated sleep holds until energy is
full rather than ending at a fixed threshold, with a six-second floor so the
button always visibly works, and the device's button label flips to **Wake**
while the spider is asleep.

Browser suite 99/99 (was 97). Two new tests cover moving decor without losing its
slot/kind/blocking flag, and a forced sleep holding until energy is full.

## v1.1.13 follow-up: placeable decor

Backgrounds became ground texture only on both targets, and everything that sits
on the floor is now a decor item the player buys with Silk and places. The
catalogue is identical: sixteen kinds, the same names, costs, unlock levels,
footprint radii and blocking flags, in the same order. `DECOR_KINDS` in
`site/lib/device-engine.js` is checked against `k_decor_kinds[]` in
`main/decor.c` by a test that parses the C table, so the two cannot drift.

Placement mirrors the gift flow on both: pick a kind in the menu, the menu
closes, the next tank tap commits the position, and the corner X cancels. The
device stores twelve slots in `game_state_t` (`decor_owned` at offset 160,
`decor` at 164, save version 8); the browser stores the same twelve slots in its
save blob and validates them on restore.

Blocking is a steering input on both, not a positional shove. The device's
`decor_avoidance()` in `behaviour.c` and the browser's inline block in `steer()`
both measure the berth from the footprint edge, only steer when the item is
ahead (`dot >= 0.15`), and turn perpendicular on the side the spider already
leans. Both also refuse a step that would land inside a blocking footprint, so a
player who drops a rock on the spider cannot trap it there.

Non-blocking decor is ignored by steering on both targets. That is the point of
the flag: the spider is meant to walk over a leaf pile.

Browser suite 97/97 (was 93). Four new tests cover the catalogue against the
native table, purchase/placement/blocking/hit-testing, the empty-slot default
and save round trip, and a blocking item actually stopping the spider.

## v1.1.10 follow-up

Petting exists on both targets with the same numbers: a tap within
`PET_RADIUS_PX` (56 px) of the spider raises mood by `PET_MOOD_GAIN` (6), bond by
1, and pays `PET_XP` (2) plus the `first_pet` achievement the first time.
`PET_COOLDOWN_MS` (1.2 s) refuses a repeat on both.
Both use a separate `petted_once` flag rather than testing `last_pet != 0`. The
clock legitimately reads 0 early in a session, so the `!= 0` form never engaged
the guard and a rapid double-tap petted twice. That was a real bug on both
targets, found by a test that tapped twice in the same frame.

The daily day prefers a real clock on both targets: the device reads the RTC and
falls back to the in-game counter when it is not valid, and the browser uses
`Date.now()`. Both honour an explicit `daily_override`, which is how the console
and the tests drive the loop without waiting a day. The device needs the override
because its RTC refuses dates before 2000, so a small day number cannot be
written to it.

The menu layout matches on both: Settings is the last row of the main menu, and
Daily and Treats have no redundant full-width Back row because the corner Back
button already exists.

Browser suite 88/88 (was 84). Four new tests cover petting, the hit radius, the
cooldown, the daily task it advances, and the wall-clock day.

## v1.1.9 follow-up

The daily loop and the treats shop exist on both targets with the same numbers.
A check-in pays `DAILY_XP_BASE + (streak-1) * DAILY_XP_PER_DAY` XP and the same
shape of Silk, capped at `DAILY_STREAK_MAX` (7); missing a day resets the streak
to 1. The rotating task is `day % DAILY_TASK_COUNT`, so it is deterministic on
both targets and a reload cannot reroll it. Completing it pays a flat
`DAILY_TASK_XP` / `DAILY_TASK_SILK`.

Both targets snapshot the task baseline **on the day rollover**, not lazily on
first read. The lazy version was a real bug: a player who cleaned twice and then
opened the menu had the baseline recorded as 2, so progress read 0/2 forever.
Both also call the snapshot once per tick, which covers a migrated or restored
save that has no baseline at all.

`DAILY_NEVER` (0xFFFFFFFF) is the "never claimed" sentinel on both targets,
because 0 is a real in-game day. Both additionally treat a zero streak as
unclaimed, so a save whose `daily_day` happens to equal today is self-healing
rather than permanently locked out.
Treats are consumables on both targets: Juicy Fly 6 Silk / +25 hunger, Silk
Ribbon 8 / +30 mood, Warm Pebble 10 / +35 energy. A purchase is refused when the
balance is short, and the refusal leaves state untouched.

The save payload grew from 136 to 152 bytes and `SAVE_VERSION` moved 5 to 6. A v5
blob is a valid prefix of the v6 struct, so both targets accept it and default the
new fields rather than discarding the pet. The browser engine accepts v1-v6.

Browser suite 84/84 (was 78). Six new tests cover the streak arithmetic and its
cap, the reset-on-skip behaviour, the deterministic task rotation, the
rollover-not-lazy baseline, treat costs and refusals, and a v5 restore.

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

## v1.1.11 follow-up

Petting XP is capped per in-game day on both targets: `PET_XP_DAILY_MAX` (10)
XP, which is five pets at `PET_XP` (2). The cap is on the currency, not the
affection -- mood, bond, the pet counter and the "Pet 4 Times" daily task all
still advance on every pet. Both targets reset the budget when the in-game day
changes, using the same RTC-first day the daily loop uses, and both persist the
budget (`pet_xp_day` / `pet_xp_today`) so a reboot does not reopen it.

Petting has a visible reaction on both targets: a `PET_REACT_PX` (9 px) rise
and fall over `PET_REACT_MS` (520 ms) with the front legs forced into the happy
wave. It is a timed overlay, not a state change, so a pet never interrupts a
hunt or a sleep. The device holds it in the canvas renderer because
`ui_hud_tick()` recomputes the emote every frame; the browser holds it in
`_engine.pet_react_until` and reads it in `spiderOptions()`.

The menu pager is two bare chevron arrows on both targets, 96x56 like the corner
Back button, with the page counter centred between them. The header accent rule
fades from dark on the left to bright on the right on both, so its bright end
runs away from the Back button rather than under it.