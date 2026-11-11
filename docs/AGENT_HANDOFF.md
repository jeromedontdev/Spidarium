# Spidarium agent handoff

## Start here

Read the [operating manual](../.github/copilot-instructions.md), then
[whole-game PSRAM/threading policy](PSRAM_THREADING_ARCHITECTURE.md).
For task-shaped recipes — changing a menu, adding a save field, adding a UI
layer, tuning constants, capturing screenshots, verifying and publishing — see
the [agent playbook](AGENT_PLAYBOOK.md).
Historical claims that WiFi cannot work on this board are superseded: real
hardware scans now succeed. Final acceptance evidence belongs in the latest
[progress round](../PROGRESS.md), not older round summaries.

## Implemented surfaces

- v1.1.23 uses six-tile (3x2) grids for Treats/Backgrounds/Borders/Decor/Gifts;
  other menus remain three-card lists. Use explicit `ui_menu_open_grid()`.
  Reuse at most six tile objects per open, never create objects per frame.
  Gifts have individual thumbnails and a full-effect preview; Back restores
  the grid page without spending Silk. The care entry is a 34px folded corner
  in a transparent 56x56 target at12/12, native and browser; browser M still
  works and exposes expanded state. Website hero is a static woodland beetle
  over Fairy Glade; both spider renderers and all 337 PNGs remain unchanged.
  Save v12 and catalogue IDs/prices are unchanged. See
  [release evidence](RELEASE_v1.1.23.md) and the latest progress round.

- v1.1.22 doubles to 68 decor, 12 treats, 36 gifts and 48 backgrounds; all old
  IDs/art remain unchanged. Borders offers 48 independently purchased matching
  frames plus free None; owned frames reapply free with any floor.
  Save v12: 268-byte prefix, 52 compiled fields, six-byte decor tail now at 268.
  v10/v11 tails still read at 260. Former padding 245 packs gift IDs32..35 in
  its low nibble and decor IDs64..67 high; u16 at246 stores background32..47.
  Decor32..63 uses the full existing extra word. Border words at260/264 and
  selection266; 255=None. Earlier saves ignore old padding and start frameless.
  Runtime keeps 48px menu thumbnails, not all 96 full scenes/frames: one reusable
  172,800-byte PSRAM QOI workspace decodes on explicit rebuild/test, composites
  frame into the existing floor canvas. No per-frame allocations/worker LVGL.
  `background 0..47`, `border 0..47|none`, `open borders` use real shop rules.

- v1.1.21 adds three selectable original scores and independent music gain.
  Sound page 2 holds Music Volume and Soundtrack. Master volume/mute still win;
  effect volume is not reduced by music gain. Save v11 uses former padding
  bytes 254/255 for music volume/track; fixed prefix 260 and decor tail unchanged.
  Older saves migrate to volume 100/track 0, with 47 compiled schema fields.
  SD menus retain a third result/status row. Screenshot capture keeps one LVGL
  snapshot alive while the persistence worker reads it, then destroys it on
  LVGL after SD completion; never allocate a second 460,800-byte copy.
  Browser feedback appears inside the game/fullscreen, with honest download
  request wording and browser-file/device-card distinctions.

- v1.1.19 [expanded habitat and music](RELEASE_v1.1.19.md): six treats,
  thirty-four decor kinds and twenty-four
  backgrounds, including a house, dark cave and night scene. Preserve old IDs
  and all approved spider art. Contextual long press replaces redundant move
  entries. Double tap dims just the enclosure, not menus or screen brightness.
  Original ambient music has an independent Sound switch and master mute still
  wins; the pet effect is a soft consonant bell/purr rather than a sweep.
  Save v10 keeps old offsets, appends music/lights/extra ownership and a count,
  then six-byte extra decor records. Fixed prefix 260 bytes, raw 272 plus tail;
  45 compiled fields. Never truncate tails in console/browser/SD backups.
  Extra records, snapshots and console lines are PSRAM-backed; only the internal
  save worker touches flash. Opaque background decoding now uses RGB565 rather
  than RGB565A8, keeping all 24 sources at the previous 16-source memory cost.

- v1.1.18: [habitat controls, prey gait and RTC sleep](RELEASE_v1.1.18.md).
  Automatic ambience is disabled, superseding v1.1.17 scheduling. Keep foreground
  effects and manual sound diagnostics. Decor/gifts have explicit movement
  menus plus long press; closed backdrops hide before deferred deletion so they
  cannot intercept the next pointer event. Menus use framed icons and a short
  gold ornament at x120, clear of Back. Background purchases/ownership and power
  preferences share save v9 (248-byte payload, 260-byte raw, 41 compiled fields);
  v8 migrates the equipped old NVS background plus free cork without charging.
  Update the compiled schema, browser decoder and raw console capacity together.
  RTC sleep snapshots on LVGL, commits on the internal save worker, drains
  workers and pauses the display before sleeping; no worker calls LVGL.
  Timer/right GPIO18 wake only, not touch. Automatic sleep is opt-in and battery
  only. See [battery policy](BATTERY_MANAGEMENT.md) for unmeasured boundaries.
  Living prey now have fixed thoracic leg roots, alternating tripod feet and
  direction-aware image rotation, with ground gait driven by travel rather
  than a clock while stopped. Image rotation works without LV_USE_MATRIX;
  do not change the approved spider or enable that unrelated option.

- v1.1.17: [quiet environmental audio](BROWSER_PARITY.md#v1117-quiet-environmental-audio).
  Replace intermittent pitch sweeps with slow air/leaf textures, not louder
  melodic effects. Native/browser seeded PCM matches `audiotest`; the short pet
  chirp is explicit, never automatically scheduled. Stream through the existing
  audio worker/PSRAM buffer; long ambience must fade promptly for queued
  foreground sound, mute and radio drains. Browser mute/volume applies while
  menus are open. See the latest progress round for measured evidence; speaker
  listening approval is not inferred from waveform or codec-write checks.

- v1.1.16: [complete SNES art/chrome release](RELEASE_v1.1.16.md).
  All 119 non-spider assets now share the [style contract](ART_STYLE.md):
  quiet terrain, shaded decor/gifts, material-lit prey, splatters, icons and
  badges. Both spider renderers and all 39 exports remain unchanged.
  Native/browser/website chrome uses flat navy/mint/gold, two-pixel stepped
  corners, hard borders and highlights, not gradients or blurred shadows.
  Keep touch geometry, keyboard coverage, readable Montserrat and distinct
  disabled/selected/destructive/focus states. The generator is authoritative;
  ordinary validation now protects **all 158 source PNGs**, superseding the
  historical 84-image preservation subset. Generate an approved redesign
  before validation. `asset_build.py --png-only --preview PATH.png` reviews
  every non-spider asset; CI checks art before website publication.

- v1.1.1: [HUD and screenshot update](RELEASE_v1.1.1.md). Battery chrome is
  transparent, HUD-only and safely inset; browser ear has an explicit origin
  and square viewport. Promotional fangs are mint, not player colors.
  Settings > Save Screenshot and `screenshot sd` share playfield capture.
  LVGL temporarily hides/restores HUD and menu objects within one callback,
  copies RGB565 into PSRAM and destroys the LVGL buffer on its own task.
  The serialized save worker owns the copied pixels after successful enqueue,
  writes a numbered RGB565 BMP with exclusive creation, flush/fsync and exact
  readback, then frees pixels. Failure leaves UI restored and reports an error;
  no card is formatted. `screenshot menu` includes the current menu;
  `screenshot playfield` excludes UI, and `capturetest` compares restoration.
  See [serial diagnostics](serial_console.md#screen-capture).

- v1.1.0: [browser parity and measured evidence](BROWSER_PARITY.md).
  Native-coordinate browser HUD/setup/menus/minigames, fullscreen, local
  progress migration with explicit errors, and separate default-off Auto Feed/
  Auto Hunt checkboxes on both platforms. The native numeric FSM maps through
  `STATE_NAMES` for rendering. Never introduce fake PMIC/WiFi/SD responses.
  Save schema v3 retains the 132-byte payload, appending checkboxes in old
  padding at 129/130; v2 padding must migrate OFF. The actual compiled schema
  contains 24 fields and `minVersion:3` for both flags. Future browser/schema
  changes require real capture and exact restore tests, not guessed offsets.
  Root LICENSE permits personal, non-commercial use; retain all third-party
  notices and do not describe it as OSI open source.
- v1.1.2 (Round 46): save schema v4 appends `volume` at offset 131, which still
  fits the existing tail padding, so the payload stays 132 bytes and the legacy
  120-byte payload is unchanged. The compiled schema now has 25 fields with
  `minVersion:4` for `volume`. Settings is WiFi, SD Card, About, Battery,
  Display, Sound, Auto Feed, Save Screenshot, Wipe Spider (destructive, last).
  Auto Feed is one switch driving both `auto_feed` and `auto_hunt`. Sound and
  Display use `ui_menu_set_item_slider()`; Battery shows a heuristic runtime
  estimate and Display holds the Screen Rotation control. Rotation is an LVGL
  transform on a full-size wrapper holding the whole UI, because the adapter
  never re-reads MADCTL after init and `bsp_display_rotation_set()` had no
  visible effect. Every layer — playfield, HUD, sleep overlay, menu panel and
  backdrop — must be parented to `power_rotation_layer()`; anything parented
  straight to `lv_screen_active()` stays upright while the rest turns. The panel
  is square, so taps still map to the same coordinates. Do not claim a measured
  runtime without hardware evidence.
- First release: [v1.0.0 notes](RELEASE_v1.0.0.md),
  [measured verification](RELEASE_VERIFICATION.md) and [actual screenshots](README.md).
  Website source lives in `site/`; Pages consumes the official, hash-verified
  five-image ZIP rather than local firmware or saves. Native browser anatomy
  follows `cspider.c`; generic Spider is not a replacement name for the user's pet.
  Browser simulation is approximate, not an ESP32 emulator.
  Release code is `0f16802`; live publication evidence is in the verification
  guide. The `github-pages` environment permits `main` and the explicitly
  approved `v1.0.0` tag. Approve the next official tag without removing existing
  protections, and update version-specific package/website validation together.
- Round 43 adds three-prey Feed guards, shared silk-bordered cards, readable
  translucent identity panel, corner battery gauge/details, Sound in Settings,
  gray/inert pagination and distinct achievement assets. There are 158 PNGs,
  including eleven badges, eight species-specific splatters, sixteen ground
  textures and sixteen placeable decor sprites. Preserve the 84 accepted
  non-spider images. The 39 spider PNGs are a generated export of
  `main/cspider.c` and regenerate with it; they are not live anatomy and not
  preservation-protected.
- Browser save tooling uses the compiled schema and validated raw export/restore.
  Restore pauses the tank and blocks subsequent saves until explicit reboot;
  never resave the old live state over a restored blob. Screenshot capture is
  marshalled to LVGL, with immutable bytes transferred/freed by acknowledgement.
  Bulk protocol uses the existing USB-JTAG driver with TX drain checks, not
  ordinary large VFS printf writes or a second UART driver.

- [Menus](MENU_ARCHITECTURE.md): three taller cards per page, larger navigation,
  red Wipe Spider (last row) under Settings, top-left page ear/aligned status bars and unobstructed drag hunting,  QWERTY keyboard with Show/Hide WiFi password,
  shared first-run name editor, manual SSID, saved networks, OTA URL,
  About, battery controls and prey selection. No flash writes or SD mounting in menu callbacks.
- Play chooses Bug Hunt, Color Match or Quick Tap without immediate repeats.
  Eight ramped rounds pay 8-32 base XP and 2-18 Silk plus care/achievement effects
  only on completion; Back cancels and deletes its LVGL timer. Score tests use
  mock rewards. Bug sprites shift, Color Match ramps from one to three
  remembered colors in order and Quick
  Tap has orange/red feints. Hunt accuracy XP is 18/10 plus 5/3 Silk.
  Care shows current XP and next gift/prey unlock.
- [Networking](WIFI_ARCHITECTURE.md): three saved networks; one copied job at
  a time; bounded scan/join/SNTP/HTTPS OTA; stop and deinit on every exit.
- RTC: PCF85063A on BSP I2C, UTC restoration at boot and verified calendar
  writes after player-requested SNTP. Boot primes network allocations with
  init/stop/deinit before LCD creation, but never associates with a saved
  network or runs SNTP automatically.
- [Gifts](GIFTS_CATALOG.md): eighteen larger habitat items, earned Silk wallet,
  one-time purchase/owned placement, unlocks through level 40 and spider visits.
  `gifttest` checks bonuses, economy and visit rewards without persisting tests.
  Keep IDs/filenames stable. The v2 payload gains appended Silk/ownership/
  brightness fields; old pet payloads and SD backups migrate rather than reset.
- [Battery](BATTERY_MANAGEMENT.md): AXP2101 worker/atomic telemetry, stored
  brightness, 60s dim/300s screen-off, touch/serial wake and low-battery save.
  Power/network display ownership is serialized. Charger settings are untouched;
  USB telemetry is not proof of unplugged discharge/runtime.
- [Artwork](ART_STYLE.md): deterministic non-spider assets in a **16-bit
  SNES-era** direction — crescent shading, three-to-five tones per material, a
  hard terminator, bounce light, an ink rim on every silhouette, no gradients.
  Eight prey species, eighteen gifts, sixteen placeable decor sprites and
  thirteen HUD icons. [Inventory](ASSETS_INVENTORY.md) and
  [asset commands](../assets/README.md) define the maintenance contract. The
  spider is drawn procedurally by `main/cspider.c` (ported in
  `site/lib/spider-renderer.js`); the 39 `spider_*.png` are a generated export of
  that renderer, not preserved art. Sickness dulls the palette rather than
  applying canvas opacity, so the spider stays opaque against pale floors. The
  terrain/decor redraw replaces smooth noise with crisp edge material clusters
  and quiet three-tone centers; placeable props and all eighteen gift
  furnishings use 8-12 tones. Gift furniture is part of the decor art scope,
  not an unchanged icon set. The spider and
  its 39 exports are byte-identical to their pre-redesign state.
  [ART_HANDOFF_SNES.md](ART_HANDOFF_SNES.md) is the current maintenance brief,
  not the superseded proposal to add hundreds of interpolated colors.
- Sixteen selectable habitats use one reusable 460,800-byte PSRAM canvas, not a
  web/prop object tree. Backgrounds are **ground texture only** — substrate,
  trail, grain, tonal drift — with no painted props, so the spider always has a
  clean floor behind it. Sixteen authored 240x240 packed background assets add
  2,764,800 bytes of explicit decoded PSRAM, scaled 2x into the existing canvas
  at selection. Preference IDs remain stable in a separate NVS key, written by
  the persistence worker.
- [Decor](ART_STYLE.md): sixteen kinds bought with Silk (12-68) and unlocked from
  level 1 to 9, placed by tapping the tank into twelve slots. Each is blocking or
  non-blocking. Blocking is a **steering input**, not a positional shove:
  `decor_avoidance()` in `behaviour.c` measures the berth from the footprint edge
  and only steers when the item is ahead, and `advance()` refuses a step that
  would land inside a blocking footprint. Non-blocking decor is ignored entirely.
  The catalogue order is shared with the browser and checked by a test that
  parses the C table. Save version 8; `decor_owned` at offset 160, `decor` at
  164, `game_state_t` 236 bytes. Placement mode lives in file-scope globals, not
  in the persisted struct.
- Prey IDs 0..3 and levels 1/2/4/6 are preserved. New roach/mealworm/fruitfly/
  waxworm unlock at 10/13/16/20. All explicit/wild/daily spawns obey eligibility;
  never reset the user's level merely to demonstrate locks.
- Drag direction is toward the finger, not the inverted legacy comments.
  Held gestures freeze AI/prey; stationary taps do not shoot. Pounce landing
  synchronizes subpixel position. Canvas gait is distance-driven with bounded
  local joint poses; duplicate renders cannot advance it.
- Care now uses a translucent page ear at (-1,-1), with a transparent
  44px touch target and four status boxes centered as a group at y=8.
  No word label or full-hunger press bubble. Tap the ear again to close;
  X is shown only for gift cancellation, not when the HUD is open.
- Round 30 darkens all enclosure floors (central RGB channels <=96, validated),
  replaces grid webs with small anchored silk sleeping sacs, and restores
  clearly bent/highlighted knee joints while preserving distance-driven gait.
  Build/full flash, 39 console checks, all eight scene diagnostics, packed art
  verification and 40-second stability soak passed. See latest progress.
- Round 31 (superseded by Round 32) replaced arbitrary knees with fixed 18/20-pixel two-link geometry,
  mirrored headward front and abdomenward rear bend branches. Signed direction,
  link-length and outward-order checks caught an inward sad/sleep knee; reduced
  contraction corrects it without weakening assertions. The dot is 2.5x larger
  and moved left along the usable circular edge. Final build/full COM7 flash,
  39/39 checks, zero render/scan drift and 40-second stability passed.
- Round 32 restores the original angular fan legs, radius-16 hips, 12.76/15.84
  segments and original pair phase offsets. Keep distance-based gait timing;
  bound original emote angles to prevent knee inversion. Golden-coordinate
  checks prove the neutral shape matches the original code, not the rejected
  IK design. Remove the circular-safe-corner assumption: dot center (30,30),
  status row y=8, level strip y=60. Build/full COM7 flash, 39/39 checks,
  zero scan/render drift and 40-second stability PASS; physical taste remains
  the user's acceptance check.
- The latest HUD revision supersedes Round 32's dot: a triangular page fold
  facing inward (lower-right half), lighter navy 60%-opacity face/shadow and
  a muted 20%-opacity crease.
  A light X appears only to cancel a gift. Preserve
  centered stat boxes and the original spider anatomy.

## Ownership

Latest care/menu revision: explicit Back only (blank taps are absorbed),
no disabled dashes, icon-bearing achievements, concise WiFi rows and
capitalized built-in text. Creature objects are grouped below the HUD.
Fullness decay is 3/minute, with temperament/gift modifiers; the >=95 gate
remains. Three quiet ambient effects use the existing audio worker and mute.
SD backup requires `CONFIG_FATFS_LFN_HEAP=y` and `CONFIG_FATFS_MAX_LFN=64` in
live config/defaults; the previous short-name configuration caused fopen to
reject `spidarium-save.bin`. Backup snapshots/commits/writes/verifies on the
save worker, with status returned to the UI.

`game.c` owns live gameplay and menu state on the LVGL adapter task. Logic
(20 Hz) and rendering (30 Hz) are timers on that task, not separate tasks.
`net.c` owns a 12 KB internal-stack worker. `save.c` owns a four-entry copied-job
queue and 4 KB internal-stack worker; SD jobs use that same queue and publish
copied restored state. Queue/stack allocation happens at boot before LVGL starts,
not in a save callback. Console save acknowledgements propagate queue rejection.
Flash writes require internal stacks even with XIP
enabled: a real NVS write proved the cache-disabled assertion on PSRAM stacks.
Audio and USB-JTAG console workers have explicit PSRAM stacks; synthesized PCM
uses a reusable 12,800-byte PSRAM buffer. Workers never call `lv_*`.
Modals skip gameplay/tank work while keeping the service timer/polling alive.
HUD labels only invalidate on actual changes at at most 4 Hz.

Networking pauses the whole adapter with acknowledgement and drains active
persistence/audio before starting the radio. Touch and composition temporarily stop;
the last frame stays visible. Pending saves wait until networking finishes.
Pending audio also waits. Main LVGL stack uses `stack_in_psram`; its internal draw workers do not.
The adapter's optional draw-worker patch hook does not match LVGL 9.6 and must
remain disabled. Do not modify managed dependencies to bypass that check.

## Build and migration

Always use wrappers, redirecting build output:

```powershell
powershell -File build.ps1 *> build_out.txt 2>&1
powershell -File flash.ps1 COM7
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' run_tests.py COM7 results.txt --gameplay
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' stability_check.py COM7 40
```

Asset packing precedes the main component build; generated identifiers must
exist before compiling consumers. Do not call `idf.py` directly.
Keep active `sdkconfig` and defaults aligned. See [memory budget](memory_budget.md).
Prefer `--gameplay` for an existing pet: the default suite intentionally
performs care/save/state changes. New hunt/progression diagnostics restore
synthetic fields before the LVGL callback returns, so saves cannot capture them.

The new table retains NVS/PHY offsets and adds OTA metadata, a 3 MB factory app,
two 3 MB OTA slots and 6 MB asset storage. Initial migration requires the full
flash wrapper (bootloader, table, app, assets), not an application-only OTA.
Do not erase NVS: it holds the user's pet and credentials.

## Verification boundaries

Round 42: final wrapper build/full flash exit 0 with zero warnings,
**39/39 non-mutating console checks**, all three games scoring 7/8 with mock
rewards, gift economy/visit tests, battery policy/wake tests and exact gift
text/pixel geometry PASS. `render 500` and `scanloop 5` have zero largest-block
drift; the final 40-second soak has zero faults. Final suite largest is 63,488
bytes, above the 51,200 gate.

Actual uninterrupted inactivity capture verified brightness 15 at one minute,
0/screen sleeping after five minutes, and serial wake restoring 50%. AXP2101
reports 100% and approximately 4,150mV on USB. A fresh SD backup wrote/read-verified
144 bytes including the appended wallet, ownership and brightness fields.
All 123 packed artwork entries match PNG pixels; 97 creature/icon/prop images
are preserved while the 18 furniture gifts are intentionally redesigned.
No unplugged runtime, physical low-battery or charging-transition claim is made.
See [battery limits](BATTERY_MANAGEMENT.md) and [latest progress](../PROGRESS.md).

Round 29 supersedes Round 28's persistent HUD and procedural landscape art.
Final build/full flash exited 0 with zero main warnings. **39/39 non-mutating
console cases** passed, including injected LVGL press/move/release and catch,
aligned icon/status bars and Gift cancellation, gait cadence/geometry, and all
eight prey unlock boundaries. `render 500` and quiescent `scanloop 5` had zero
drift; 40-second soak passed with zero faults. Minimum observed DMA largest:
63,488 bytes. All eight authored scene IDs survived independent resets and
matched packed source pixels; the user's original leaf-litter selection was
restored. All 123 packed entries matched decoded PNG pixels; all 115 earlier
PNGs remained byte-identical. See [latest progress](../PROGRESS.md).
The art preview was inspected, but the user still decides aesthetic acceptance.
Synthetic diagnostics restore pet/prey fields; normal live auto-hunting may
advance the existing pet during the time hardware runs.

Round 28 on COM7: final build/flash exit 0, zero main warnings, **44/44 console
checks**, two successful real-callback `uitest` runs, and 40-second soak
`VERDICT: PASS` with zero faults. Final `render 500` and `scanloop 5` reported
zero largest-block drift. Minimum observed internal/DMA largest and standalone
post-reset memory check: **63,488 bytes**; suite-end/soak snapshots: 81,920.
First scan infrastructure can consume a one-time block; subsequent scans are stable.
All eight background choices saved and survived independent resets; the original
choice was restored. All 95 packed QOI entries match their PNG pixels; the 56
non-spider assets enforce 2x2 blocks, binary alpha and limited palettes, with
deterministic regeneration preserving all 39 spider PNGs.
Earlier Round 27 RTC/SD evidence remains applicable:
Manual host-UTC RTC write/readback and restoration across reset passed.
The actual missing-card SD probe ran on its worker while menu commands remained
responsive; expected driver errors were confined to that explicit probe.

Console tests are actual UI/game code-path tests, not proof of visual quality
or physical touchscreen ergonomics. Gift computations are not proof of every
random catch outcome. See [serial reference](serial_console.md).

Positive saved-network association, fresh SNTP/boot synchronization and OTA
installation require authorized credentials and a hosted, uniquely versioned
Spidarium HTTPS image. Never join an arbitrary discovered open AP. RTC retention
while powered off requires physical backup-power testing; a reset is not proof.

OTA checks HTTPS certificate/hostname, matching project and image completion.
It has no release manifest, semantic downgrade policy, provisioned signing,
automatic reboot or application-functional rollback. Credentials use ordinary
NVS, not encrypted flash, and must never be logged or committed.
Application-only OTA does not replace artwork. Asset/identifier changes require
a matching full flash until a separate asset-update mechanism exists.

## Preserve and extend

- Allocate CPU-only buffers once in PSRAM; no per-frame allocation.
- Publish worker completion only after results are ready and busy is cleared.
- Queue copied state, not pointers to live LVGL objects or mutable game state.
- Report rejection/failure explicitly; test failure cleanup and radio-off.
- Compare internal/DMA `largest` before and after repeated operations.
- Preserve dirty user changes. Commit only when requested; never erase NVS or
  flash-wide storage for an artwork or menu change.
- Append each round to [PROGRESS](../PROGRESS.md), including Decisions made and
  honest build/flash/hardware evidence. Do not label blocked checks as passed.
