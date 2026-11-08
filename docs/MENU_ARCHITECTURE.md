# Menu implementation

`ui_menu.c/h` owns one three-card paged modal, a text editor and short minigames.
`game.c` owns navigation and callbacks. This is not the previously proposed
menu-stack/slider architecture; use the implemented API.

## Navigation

```
Menu
  Backgrounds            eight unlocked retro pixel-art habitats
  Achievements           read-only, paged
  Settings
    WiFi
      Scan networks      explicit on-demand action
      Add network        SSID -> masked password editor with Show/Hide
      Saved networks
        Selected network -> sync RTC / forget
      Firmware update    HTTPS URL editor
      Scanned networks   choose -> password editor if secured
    SD card              backup / restore (optional)
    About                firmware and hardware information
    New Tank             red text; confirmation before reset
    Battery and Display  live battery/charging status; brightness presets
    Sound                persisted on/off
  Prey species           read-only unlock list
```

Back from WiFi/SD/About/New Tank/Battery/Sound returns to Settings. Back from saved network/editor
returns to WiFi. Other submenus return to the root; root Back closes.
Only the explicit Back control navigates back. Blank panel/backdrop taps are
absorbed without navigating; disabled cards do not dismiss the menu.

## Rows and lifetimes

`ui_menu_item_t` fields: `label`, optional `detail`, `enabled`, `selected`,
`is_submenu`, and uint8 `id`. The menu copies text into LVGL labels when built.
Use static tables/detail buffers for menu definitions. The callback is captured
before closing, so selecting a row can safely open its destination.

The corner-safe 400x400 panel and backdrop are opaque. Three 76-pixel-high
cards are reused on each page, with 96x56 Back and 120x56 Prev/Next controls.
There is no scrolling, momentum or scroll animation. Selected entries open
on their page. Cards show press feedback, selected tint, disabled styling and
chevrons on actionable submenus. Read-only/disabled cards have no dash marker.
Achievements use ten distinct authored 48px badges, including Perfect Hunt;
the retired First Pet badge remains mapped for legacy data but is not earnable.
`ui_menu_set_item_icon(item, asset_index)` attaches an icon without changing
static item definitions. Backgrounds has no root-card subtext.
Shared `ui_style_card()` supplies rounded silk-bordered panels and pressed
outlines without shadows, filters or per-frame object creation. Prev/Next
remain visible, gray and inert at both page boundaries, including single pages.
Routine WiFi actions have no explanatory subtext; status/errors, scan security
and unavailable-action requirements remain visible.
Menu headings/actions and definition names use proper capitalization; preserve
the exact case of user names, SSIDs, passwords and URLs.
Long SSIDs/details are ellipsized instead of overflowing the Back button or row.
`ui_menu_set_destructive(id)` colors an action red across pages; it resets on
close/open. Settings' New Tank and its confirmation action both use it.

`ui_menu_open_input(title, initial, password, max_length, submit)` builds a
one-line textarea and a QWERTY keyboard (10/9/8 keys in its letter rows).
The 376x228 matrix has four letter rows; letters, case,
numbers and two symbol pages cover all 95 printable ASCII characters. Space
and Backspace are explicit keys, and touching the text moves the cursor.
Passwords start masked with no temporary character reveal; Show/Hide password
is an explicit control. Visibility never changes the underlying input. Done
submits and Back discards the editor. First-run name editing uses this same
keyboard, mounted on the active setup screen. A false submit result
keeps the editor open and displays a validation error; an accepted callback
owns navigation. Back discards input. No NVS or WiFi work occurs in the editor.

## Care overlay and hunting

A translucent folded-page ear replaces the dot at the display's top-left.
Its transparent 44x44 object starts at (-1,-1), with a 12-pixel extended hit area.
The visible fold is unchanged. The triangle occupies the
lower-right half of that target, folding inward from the top-left corner.
A 60%-opacity black shadow and lighter navy (`0x203044`) face replace the pale flap;
a muted 20%-opacity crease provides detail without washing out contrast. It reveals four compact named
status boxes centered as a group at y=8 (318px wide, x=81 on the 480px display),
a translucent bordered identity panel at y=56 holding name/XP, next unlock and
Silk, and six labelled
128x64 actions (Feed, Clean, Play, Sleep, Gift, Menu). The bars show the current
number and proportional fill without animation. Names and values are stacked
to fit the compact row. The inward-facing fold extends one pixel past the origin, following
the user's incremental placement adjustments; actual visibility needs
device/user assessment, not rectangular LVGL hit testing alone.
The fold stays unchanged when the HUD opens; tap it again to close.
An X appears only during gift placement to cancel;
a tank tap also closes without
starting a drag. The overlay pauses simulation/tank updates but not LVGL or
service polling. It stays open until explicitly dismissed, not on a timeout.
A top-right 52x44 battery control remains visible while care is closed. Its
fill/percentage show PMIC charge, a bolt means charging, and USB/unknown states
are explicit. Tapping opens voltage and brightness details. It reads only an
atomic PMIC snapshot, never I2C. Feed disables at three live bugs; callback and
needs-layer guards reject repeated taps without spending taps or playing sounds.
Dead splatters do not count toward the three-live-bug limit.
Spider/aim/prey objects live in a stable creature layer below a separate HUD
layer. Newly spawned bugs cannot overdraw the HUD. Menus are above both.
The playfield has a 34-pixel perimeter margin, not reserved top/bottom HUD bands.
Gift uses the same paged cards, showing effects and unlock levels; choose a
kind, then the overlay closes so a tank tap places it. The corner button becomes
X during placement; it cancels without placing or shooting.
Existing/restored gifts and new placements are clamped into the visible
playfield. Prey species pages display the current pet level and each species'
availability/required level. A level-10 pet correctly has the original four
species plus roach, with mealworm, fruit fly and waxworm still locked.

Press/move/release on the full-screen tap layer owns drag hunting. A gesture
must move at least `AIM_MIN_PULL_PX`; a far-away stationary tap is not a shot.
Release jumps toward the finger, with gift-adjusted range and playfield clamps.
While held, AI transitions and all prey movement stop. Sleeping, eating, molting,
already jumping or full (food >=95) prevents aiming. Full presses show no
"Full: hunt later" bubble, as requested; food remains visible in the status row.
Entering care cancels a held drag. Landing synchronizes the float/integer
positions, preventing post-jump snapback.

The canvas gait advances by subpixel walking distance, not render calls.
The original angular fan anatomy and quarter-cycle pair offsets are restored:
hips at radius 16, segments 22*0.58 / 22*0.72 pixels, neutral bend 0.55 radians.
This matches the original setup preview, without the replacement IK proportions
or knee highlights. Legs rotate with heading but do not inherit body squash,
as in the original renderer. Original emote modulation is retained, except the
combined swing/bend angle is bounded to 0.15..1.25 radians to prevent inversion.
The gait diagnostic compares all eight neutral poses with original-coordinate
golden values, then checks signed bend (>40), segment lengths, finite coordinates
and canvas reach across animated/resting emotes. Do not replace the anatomy
merely to address timing, or confuse geometric assertions with visual acceptance.
Idle/jumping do not advance walking phase. No limb objects or per-frame memory
allocations were added; body/face art and all spider PNGs remain unchanged.

## Play and progression

Play opens a randomly selected eight-round game, excluding the previous choice:

- Bug Hunt: match a reference sprite/name among four actual prey sprites.
  The target swaps tiles every 700ms initially, falling to 420ms in round eight.
- Color Match: remember one color in rounds 1-3, two in rounds 4-6 and three
  in rounds 7-8, previewed for 750ms each. Repeat them in order after the
  prompt disappears, within the round's deadline. A 150ms input guard prevents
  a duplicate touch consuming another step. Choices retain color names for accessibility.
- Quick Tap: wait for Green/Tap, ignoring one orange feint or, from round
  three, orange and red feints. Initial wait is 1.1-3.5s; each feint lasts
  0.5-1.6s. Reaction windows shrink from 850ms to 465ms across rounds.
  Early taps and timeouts miss that round.

Bug/Color deadlines shrink from 2600ms to 1480ms. Rounded bordered cards,
the prey reference, actual sprites and a countdown bar replace plain text tiles.

The four-choice tiles are 182x100; Quick Tap uses one 376x212 target.
Between rounds, 600ms feedback absorbs extra taps. No objects are created per
round. A 50ms LVGL timer drives deadlines without blocking. Closing or replacing
the modal deletes the timer and cancels without XP, mood or energy rewards.
Completing all eight rounds applies ordinary care Play once (+10 mood, -5 energy,
one play count) plus score XP: total base reward is 8 + 3 per correct answer,
or 8-32 XP, with existing achievement bonuses on top. Silk pays 2 + 2 per
correct answer (2-18). The result shows score, earned XP and Silk.
The persistence worker queues the completed reward.
`play` on serial opens the same game; it no longer grants care immediately.

Hunt accuracy rewards rise from 12/7 to 18/10 XP for perfect/good catches,
before existing meal, streak and achievement bonuses. The 50 XP level
threshold and species levels are unchanged; [gift progression](GIFTS_CATALOG.md)
extends through level 40 with prices. Existing XP/level are not rebased and
the appended save fields migrate without losing the pet. Care shows XP and
the nearest future gift/prey unlock rather than leaving progression implicit.
Gameplay still pauses behind the minigame/result modal.

`uitest` checks all three games' scoring, early/wrong taps, timeouts, cancellation,
exactly-once reward callbacks, timer cleanup and immediate-repeat avoidance
using a mock reward callback; it never saves synthetic scores to the pet.
It also checks shifting targets, preview-tap suppression and exact ramped deadlines.

## Battery and idle display

[Battery management](BATTERY_MANAGEMENT.md) reads the board's AXP2101 on its
own worker. Settings offers 25/50/75% brightness; the stored preference defaults
to 50%. After one minute idle the display dims to at most 15%, and after five
minutes a black tap-absorbing overlay and 0% brightness stop tank simulation/
rendering. Touch wakes without hunting; serial `wake` exercises the same path.
Low battery (<=10% without USB) limits brightness to 5% and requests a save.
USB/charging/voltage/percentage are visible in Settings and Care. Charger current
and voltage configuration are not changed.

## Care pacing and audio

Base fullness decay is 3 points per minute (previously 1.1), multiplied by
temperament/gifts. A neutral full spider becomes huntable at 94 after about
two minutes; the >=95 full gate remains. `uitest` simulates two minutes through
the actual hunger accumulator and restores fullness/accumulator without saving.
Ambient rustle, chirp and water-drop effects play quietly at randomized 12-25s
intervals during gameplay, outside aiming/pouncing/eating. They use the existing
PSRAM PCM buffer and audio worker, obey mute and network exclusivity, and do
not create tasks or perform codec writes on LVGL.

## SD backup

Backup is one copied-snapshot persistence job: commit the snapshot to NVS,
copy its versioned blob to `/sdcard/spidarium-save.bin`, flush/sync/close,
then reopen and verify every byte. The UI polls completion and shows explicit
success/failure. FATFS requires heap-backed long-filename support in both
`sdkconfig` and defaults; short-name-only mode rejected this filename despite
successful mounting. Never format a user's card to fix this issue.

## Threading

All menu/LVGL calls execute on the adapter task. Console menu commands use
request/acknowledgement, not direct worker `lv_*`.
Network callbacks queue copied jobs and immediately return.
Network work intentionally pauses **all display composition**, holding the
last frame. A generation counter refreshes WiFi results after resume.
Never pause/delete the render service timer to perform network operations.
Ordinary modals suspend gameplay simulation and tank rendering, but keep the
LVGL/service timer and asynchronous completion polling alive. The game does
not repaint moving prey/canvas/dust beneath menus. HUD text refreshes only when
changed at at most 4 Hz; no shadows, translucent card blending or scroll animations.

Game save callbacks enqueue snapshots to the persistence worker.
See [whole-game threading](PSRAM_THREADING_ARCHITECTURE.md).

## Adding a menu

1. Add a menu ID and `menu_open()` case in `game.c`.
2. Build `ui_menu_item_t` rows and an appropriate pick callback.
3. Set the Back destination after opening.
4. Add a console `open NAME` mapping for reproducible tests.
5. For slow work, queue a worker request and refresh from completion.
6. Test selection, Back, page-to-last-row, disabled rows, and repeated opens.

`uitest` exercises real LVGL page/buttonmatrix callbacks, final-page selection,
case/symbol/backspace edits, maximum password length, password visibility,
Care/Gift callbacks, aligned/non-overlapping status labels and bar values,
plus unique enclosure/grid/exact-source-pixel checks. `hunttest`
injects an LVGL pointer through real hit testing and press/move/release,
including stationary taps, care dismissal, held aiming, landing/catch and
subpixel continuity. `gaittest` compares 20/30/60 Hz and repeated renders plus
bounded leg poses; `preytest` checks all eight unlock thresholds and rejected
spawns. These restore synthetic pet/prey changes without saving. They use synthetic text and
never submits credentials. Physical typing and aesthetic acceptance still need
viewing/tapping the board; diagnostic passes are not user acceptance.

No per-frame menu creation: rebuild only for explicit navigation or completion.
Art assets and icons follow [the art guide](ART_STYLE.md).
