---
name: plan-spidarium
description: "Execute the Spidarium ESP32-S3 virtual pet overhaul phase by phase, unattended"
agent: "agent"
argument-hint: "Optional: phase number to start from (default: 0)"
---

# Spidarium Overhaul — Execution Prompt

You are taking over the Spidarium ESP32-S3 virtual pet project. This file is both your
briefing and the authoritative spec. Read it in full before touching any code.

## Operating rules (read these first)

You are running unattended, likely overnight. Follow these rules exactly.

**R1 — Read the whole file first.** Do not start editing until you have read every section,
including the Asset specification and Constraints.

**R2 — Back up before you break anything.** There is no version control in this workspace
(`git` is not installed and there is no `.git`). Before your first edit, copy the current
`main/` folder to `main_backup_<YYYYMMDD>/`. If you later need to revert, restore from it.

**R3 — Phase 0 is a hard gate.** The tree does not compile today (`game.h` and `game.c` are
from different generations). Do not begin Phase 1 until `build.ps1` exits 0 **and**
`stability_check.py` reports `VERDICT: PASS`.

**R4 — One phase at a time, verified.** For each phase: implement → build → flash →
stability check → log → next phase. Never start phase N+1 with phase N unverified.

**R5 — Never leave the tree uncompilable.** If you run out of time or hit a wall mid-phase,
revert that phase's partial work so the last committed state still builds. A building tree
with 3 phases done beats a broken tree with 6 phases half-done.

**R6 — Log progress to `PROGRESS.md`.** After each phase, append a section: phase number,
what changed, files touched, build result, stability result, anything the human must know.
Create the file on your first phase. This is how the human catches up in the morning.

**R7 — Bail out loudly, don't thrash.** If flashing fails 3 times in a row, or the board
stops responding on COM7, or a build error resists 3 fix attempts: stop immediately, write
`BLOCKED.md` with the phase, the exact error text, what you tried, and what you think is
wrong. Then end your turn. Do not keep retrying variations.

**R8 — Don't ask questions.** The human is asleep. Where the spec is silent, make the
reasonable choice, implement it, and record it under a "Decisions made" heading in
`PROGRESS.md`. Only stop for the R7 conditions.

**R9 — Respect the Constraints section.** Those six rules encode bugs that have already
crashed this project. Violating them will cost you hours.

**R10 — Report at the end.** Finish with a short summary: phases completed, phases skipped,
current build state, and the single most important thing the human should look at first.

## The plan

`main/game.h` and `main/game.c` are from two different generations, so the tree doesn't compile. Phase 0 restores a green build, then the game is rebuilt as a modular engine with a full-bleed terrarium screen plus a separate status page, sprite-based spider/prey, live prey hunting, needs/care loop, life cycle, progression, and synthesized SFX.

## Steps

### Phase 0 — Restore green build (blocking)

1. Reconcile `game.h`/`game.c` — treat the header as truth, minimally rewrite `game.c` to compile (`g.food`→`g.hunger`, `g.spark`→`g.hygiene`, add `state`/`taps`/`eat_until_ms`/`blink_window`/`spider_heading` to the struct, map `ui.spider`/`ui.crumb`/`ui.stat_*`/`ui.subtitle`)
2. Fix duplicate `MAX_DECO`/`MAX_POOP` defines (header says 12/8, `.c` says 10/6)
3. Replace deprecated `lv_obj_add_flag`/`lv_obj_remove_flag` with `lv_obj_set_hidden`/`set_clickable`/`set_scrollable`
4. Remove the unknown `CONFIG_LV_DRAW_THREAD_SUPPORT` symbol from `sdkconfig.defaults`
5. Wire up or delete the unused `make_deco` (`-Wunused-function`)
6. Verify: build → flash → `stability_check.py` PASS

### Phase 1 — Foundation (depends on 0)

7. `game_config.h` holding every tunable; strip magic numbers
8. `save.c/.h` with `magic`/`version`/`size` header — reject mismatched blobs instead of loading garbage (today any struct change silently corrupts saves)
9. `tools/gen_sprites.py` (PIL) + `assets/` tree + `assets.c/.h` — implement the full
   **Asset specification** section below (67 files, exact names and frame counts)
10. `main/CMakeLists.txt`: add `spiffs_create_partition_assets(storage … MMAP_SUPPORT_QOI)`, new SRCS, `REQUIRES esp_mmap_assets esp_lv_fs esp_lv_decoder esp_codec_dev`
11. `sdkconfig.defaults`: enable `CONFIG_ESP_LVGL_ADAPTER_ENABLE_FS` and `_ENABLE_DECODER` (both currently off)

### Phase 2 — Terrarium & rendering (depends on 1)

12. `terrarium.c`: substrate gradient, glass vignette, web, real decor shapes, poop, dust motes
13. `spider.c`: replace circle+`lv_line` spider with `lv_image` sprite + frame cycling
14. Full-bleed playfield (480×480 minus HUD safe areas)

### Phase 3 — Movement & behaviour AI (depends on 2)

15. Steering (seek/arrive/flee), turn-rate-limited heading, decor separation
16. Gait phase driven by actual speed; hop locomotion + pounce ballistic arc with squash/stretch
17. FSM: IDLE micro-behaviours, WANDER, STALK, POUNCE, EAT, SLEEP, PLAY, MOLT, SICK

### Phase 4 — Prey & hunting (depends on 3)

18. `prey.c`: fly/cricket/moth with wander + flee AI
19. Stalk→pounce→catch probability from distance/energy/stage; miss recovery

### Phase 5 — Needs, care loop, life cycle (parallel with 4)

20. Four stats with distinct decay curves; mood derived from hunger/hygiene/play
21. Feed/clean/play/sleep with real consequences
22. Stages + molting events affecting size/speed/stats
23. Neglect → SICK state only. **The spider never dies.** SICK is a visible, recoverable
    condition (sluggish movement, dull colours, refuses prey, sad idle animations) that
    clears once needs are met again. No death screen, no restart, no permadeath path.

### Phase 6 — Two-screen UI (depends on 5)

24. `ui_hud.c`: corner stat rings, action tray, tap-to-pet
25. `ui_status.c`: big bars, stage, age, XP/level, achievements, settings
26. Screen switch via button — gesture recognition is disabled in this build

### Phase 7 — Progression & terrarium building (depends on 6)

27. XP from meals/pets/plays/cleans/days; levels unlock decor, prey species, themes
28. Decor placement mode: pick item → tap playfield → persisted
29. Decor effects on stats/behaviour

### Phase 8 — Audio SFX (parallel with 7)

30. `audio.c`: `bsp_audio_init` + ES8311 speaker, synthesized PCM envelopes (eat, pounce, molt, tap, alert) — no audio assets
31. Mute toggle persisted

### Phase 9 — Polish

32. Perf to 30 FPS, remove `game.c.bak-web2` and dead code, 10+ min soak, save/load round-trip

## Asset specification

`tools/gen_sprites.py` (Pillow) draws every sprite procedurally and writes RGBA PNGs into
`assets/`. Deterministic (fixed seed), re-runnable, committed alongside its output. The build
converts PNG → QOI via `spiffs_create_partition_assets`.

### Naming convention

Lowercase snake_case, one directory per category:

```
assets/spider/<view>_<anim>_<frame>.png
assets/prey/<species>_<anim>_<frame>.png
assets/ui/<icon>.png
```

Frames are zero-indexed. The generated header exposes each file as an enum member with `.`
replaced by `_` and uppercased, e.g. `MMAP_ASSETS_SPIDER_N_WALK_0`.

### Spider — 96×96 canvas, transparent background

Anchor/pivot is the **body centre at (48, 48)**. All frames must keep the body centred on
that point so position updates never jitter.

Three unique views: **`n`** (facing up), **`e`** (facing right), **`s`** (facing down).
West is `e` mirrored at runtime via `lv_image_set_scale_x(obj, -256)` — do **not** author
west frames, and do **not** use `lv_image_set_rotation` (`LV_USE_MATRIX` is off).

Heading → view mapping: snap the spider's continuous heading to the nearest of four
cardinal directions, then pick the view. `n` for headings in the upper quadrant, `e` for
right, `s` for down, `e` mirrored for left. Diagonal headings snap to whichever cardinal
is closer; do not interpolate or blend views.

| Animation | Frames | Names | Frame period | Loop |
|---|---|---|---|---|
| walk | 4 | `walk_0` … `walk_3` | 110 ms | yes |
| idle | 2 | `idle_0`, `idle_1` | 900 ms | yes |
| eat | 2 | `eat_0`, `eat_1` | 180 ms | yes |
| sleep | 1 | `sleep_0` | — | static |
| pounce | 2 | `pounce_0`, `pounce_1` | 90 ms | no (one-shot) |
| molt | 2 | `molt_0`, `molt_1` | 400 ms | no (one-shot) |

13 frames × 3 views = **39 spider files**.

`pounce_0` is the crouch/coil, `pounce_1` is the extended leap. `molt_0` is the old
exoskeleton splitting, `molt_1` is the emerged spider. `sleep_0` is legs curled tight
against the body.

### Prey — 48×48 canvas, transparent background

Anchor/pivot is the **centre at (24, 24)**. Three species: **`fly`**, **`cricket`**, **`moth`**.

| Animation | Frames | Names | Frame period | Loop |
|---|---|---|---|---|
| walk | 2 | `walk_0`, `walk_1` | 140 ms | yes |
| flee | 2 | `flee_0`, `flee_1` | 70 ms | yes |
| dead | 1 | `dead_0` | — | static |

5 frames × 3 species = **15 prey files**.

Prey are drawn facing right; mirror for left. `dead_0` is legs-up/curled, visually distinct
from `walk` so the player can tell a catch succeeded.

### UI icons — 48×48 canvas, transparent background

Single frame each, monochrome-friendly (one accent colour + white), designed to read at
48 px and still legible at 32 px.

`feed`, `clean`, `play`, `sleep`, `decor`, `status`, `back`, `hunger`, `mood`, `energy`,
`hygiene`, `xp`, `lock` = **13 icon files**.

### Style guide

- **Palette** — background is `#04050a`. Spiders read against it with a mid-grey body
  (`#6f7a8a` base, `#8a94a6` highlights) and warm amber eye reflections (`#f5b342`).
  Prey use muted browns/greys so they don't compete with the spider. UI icons use the
  existing accent set: `#f5b342` (hunger), `#58d68d` (mood), `#7c9cff` (energy),
  `#4fd1c5` (hygiene).
- **Silhouette first** — every sprite must be identifiable as a flat black shape at 1×
  scale. Detail is secondary.
- **Jumping spider anatomy** — compact rounded cephalothorax, larger teardrop abdomen,
  eight eyes with the four anterior ones large and forward-facing, stout short legs held
  in a slight crouch. It must not read as a generic orb-weaver: no long thin legs, no
  oversized round abdomen.
- **Legs** — 8 legs, 4 per side, drawn as tapered 2-segment limbs with a visible knee.
  Walk frames alternate a tetrapod gait (legs 1/3 left + 2/4 right forward, then swap).
- **Fuzzy edge** — a 1 px lighter rim on the body silhouette sells the "hairy" look without
  needing per-hair geometry.
- **No anti-alias fringing** — draw on a transparent RGBA canvas; do not composite onto a
  background colour, or QOI conversion will bake in a halo.
- **Consistent lighting** — light from the top-left across every sprite in the set.

### Generator contract

`tools/gen_sprites.py` must:

1. Take no required arguments; write into `assets/` relative to the repo root.
2. Be idempotent — re-running produces byte-identical PNGs.
3. Print a summary line per file written, and a final count.
4. Fail loudly if Pillow is missing.
5. Keep all drawing parameters in a `PALETTE` and `GEOMETRY` dict at the top of the file so
   the art can be retuned without touching drawing code.

Total: **67 files**, comfortably under 1 MB as QOI against the 7 MB `storage` partition.

## Relevant files

- `main/game.h` — declares the new API; treat as the contract
- `main/game.c` — old implementation; Phase 0 patches, then gets split apart
- `main/game.c.bak-web2` — older variant using `g.hunger`/`g.cleanliness`; reference only, delete in Phase 9
- `main/main.c` — add assets mount + audio init around `game_init()`
- `main/CMakeLists.txt` — asset packaging + new sources + component requires
- `sdkconfig.defaults` — enable FS/decoder, drop the bogus symbol
- `partitions.csv` — `storage` spiffs 7M already exists and is unused; that's the asset home
- `build.ps1` / `flash.ps1` / `stability_check.py` / `capture_log.py` — the verification loop
- `managed_components/espressif__esp_mmap_assets/project_include.cmake` — `spiffs_create_partition_assets` signature and its QOI/SPNG mutual-exclusion rules
- `managed_components/waveshare__esp32_s3_touch_amoled_2_16/esp32_s3_touch_amoled_2_16.c` — `bsp_display_start`, brightness, audio, spiffs mount

## Verification

1. `powershell -File build.ps1` → exit 0, no new warnings in `build.log`
2. `powershell -File flash.ps1 COM7` → exit 0
3. `python stability_check.py COM7 30` → `VERDICT: PASS`
4. `python capture_log.py COM7` → expected phase log lines present
5. Manual: every button, both screens, reboot, confirm persistence

## Decisions

- Hybrid art: procedural terrarium/web/decor, sprite atlas for spider + prey
- All six mechanics pillars in scope
- SFX only, synthesized in C — no audio assets
- Two screens: full-bleed terrarium + status page
- Sprites generated by a committed PIL script, overridable later
- Modular split (9 modules), not one `game.c`
- Fix-then-build sequencing; every phase ends with flash + stability check
- **No death.** Neglect leads to a recoverable `SICK` state, never to a game-over

## Constraints the next agent must respect

- `lv_line_set_points()` stores a pointer — point arrays must be `static`, never stack
- Deleting LVGL objects inside an event callback crashes; keep the deferred-delete queue flushed at the top of the render timer
- All LVGL access outside the worker task must hold `esp_lv_adapter_lock`
- `LV_USE_MATRIX` is off → no `lv_image_set_rotation`; pre-render directions or mirror only
- Only Montserrat 12/14/24 are compiled in
- `LV_USE_GESTURE_RECOGNITION` is off → screen switching must be button-driven

## Resolved implementation details

These were open questions; they are now decided. The next agent should implement them as
specified and not re-litigate them.

1. **Time compression** — `GAME_TIME_SCALE` lives in `game_config.h` as a compile-time
   constant. Ship at `1`. Phase 5 verification builds with `60` to compress a day of decay
   into ~24 minutes. No runtime UI toggle; it is a developer knob only.

2. **Sprite budget** — Spider sprites are **96×96** max, prey **48×48**, UI icons **48×48**.
   Assets are packaged as **QOI** (`MMAP_SUPPORT_QOI`), which is mutually exclusive with
   SJPG/SPNG in `spiffs_create_partition_assets`. Total asset budget is well under 1 MB
   against the 7 MB `storage` partition, so there is no pressure to shrink frames.
   Frame counts, filenames, pivots and the style guide are specified in the
   **Asset specification** section above — implement exactly that set.

3. **Death UX** — **No death.** The spider cannot die. Neglect produces a recoverable
   `SICK` state only (see Phase 5, item 23). There is no death screen, no restart flow, and
   no legacy/offspring mechanic. `game_state_t` therefore needs no death or generation
   fields.

## Practical notes for unattended runs

**Commands.** Always use the wrapper scripts, never `idf.py` directly — the toolchain paths
are hand-assembled inside them:

```powershell
powershell -File build.ps1                 # writes build.log
powershell -File flash.ps1 COM7            # writes build\_flash.log
python stability_check.py COM7 30          # 30 s boot window
python capture_log.py COM7                 # 6 s log sample
```

**Reading build failures.** `build.ps1` tees to `build.log`. Grep it for `error:` rather
than reading the whole file — it is large. The exit code is printed on the last line as
`=== idf.py build exited with code N ===`.

**Serial port.** The board is on `COM7`. If a flash fails with a port error, kill stale
processes first:

```powershell
Get-Process | Where-Object { $_.ProcessName -match 'python|idf|esptool' } |
    ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
```

Then retry once. Three consecutive failures triggers R7.

**Build times.** A clean build is several minutes; incremental builds after touching only
`main/` are much faster. Do not assume a slow build is a hung build — wait for the exit
code line.

**What "PASS" means.** `stability_check.py` prints `VERDICT: PASS - clean boot window` when
it sees no `Guru Meditation`, `abort`, `assert`, `Load access`, `Store access`, `panic`,
`brownout`, or `E (...)` lines. Anything else is a failure, even if the app appears to run.

## Begin

Start now, in this order:

1. Read this entire file.
2. Back up `main/` per R2.
3. Create `PROGRESS.md` with a header and today's date.
4. Execute Phase 0. Verify it. Log it.
5. Continue through the phases in order, verifying and logging each one.
6. Stop on R7 conditions, or when all phases are done.
7. Write the final summary per R10.
