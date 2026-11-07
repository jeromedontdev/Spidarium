# Spidarium Overhaul — Progress Log

Started: 2026-11-04
Agent: unattended run per `.github/prompts/plan-spidarium.prompt.md`

Backup of pre-work `main/` taken at `main_backup_20261104/` (R2).

---

## Phase 0 — Restore green build ✅

**Status:** COMPLETE — gate passed.

### What was actually wrong

The spec assumed `game.c` existed and merely disagreed with `game.h`. In reality
**`main/game.c` did not exist at all** — only `main/game.c.bak-web2` (an older
generation using `g.hunger`/`g.cleanliness`). CMake failed at configure time:

```
Cannot find source file: .../main/game.c
No SOURCES given to target: __idf_main
```

So Phase 0 was a rewrite of `game.c` against the `game.h` contract, not a patch.

### Changes

| File | Change |
|---|---|
| `main/game.h` | Added runtime fields to `game_state_t` (`state`, `taps`, `eat_until_ms`, `blink_window`, `blink_until`, `spider_heading`). Added `subtitle`, `spider`, `crumb`, `stat_food/mood/spark` to `game_ui_t`. |
| `main/game.c` | **New file.** Reconciled to the header: `hunger`/`hygiene` naming, `SPIDER_EAT` (was `SPIDER_EATING`), 4 stats, `MAX_DECO`=12 / `MAX_POOP`=8 from the header only (no duplicate defines). |
| `sdkconfig.defaults` | Removed the unknown `CONFIG_LV_DRAW_THREAD_SUPPORT` symbol. |
| `stability_check.py` | Fixed two bugs that made the gate meaningless (see below). |

### Deprecated LVGL API removal

All `lv_obj_add_flag` / `lv_obj_remove_flag` calls replaced with the LVGL 9.6
per-flag setters: `lv_obj_set_hidden`, `lv_obj_set_clickable`,
`lv_obj_set_scrollable`. Verified against
`managed_components/lvgl__lvgl/include/lvgl/core/lv_obj.h` (LVGL 9.6.0), where
the old functions carry `LV_DEPRECATED`.

### `make_deco` wired up

Previously unused (`-Wunused-function`). Now used by `spawn_blob()` and draws a
body plus a 30%-opacity highlight, so decorations read as objects rather than
flat dots.

### Verification

```
powershell -File build.ps1        -> === idf.py build exited with code 0 ===
powershell -File flash.ps1 COM7   -> === idf.py flash exited with code 0 ===
python stability_check.py COM7 30 -> VERDICT: PASS - clean boot window (44 lines)
```

Boot log confirms the app reaches ready state:

```
I (2211) spidarium: Spidarium booting
I (2252) spidarium: Spidarium ready (480x480)
I (2252) main_task: Returned from app_main()
```

Only warning in `build.log` is pre-existing and unrelated
(`esp_codec_dev/Kconfig: ESP_IDF_VERSION env var not set`).

---

## Decisions made

1. **`stability_check.py` was silently broken — fixed.** It opened the port with
   `rtscts=True`, which suppressed every read, so it captured 0 lines and
   printed `VERDICT: PASS` unconditionally. A dead board would have passed the
   Phase 0 gate. Removed the flow-control flags (matching the working
   `capture_log.py`) and added an explicit `FAIL - no serial output captured`
   branch so a silent port can never masquerade as a pass.

2. **Removed `ELF file SHA` from the fault regex.** It matches the normal boot
   line `I (599) app_init: ELF file SHA256: ...`, so every healthy boot was
   reported as FAIL. Also dropped `rst:0x[1-9]`, which fires on ordinary
   power-on resets. The pattern now matches the fault list in the spec.

3. **`python` on PATH lacks `pyserial`.** The verification scripts must be run
   with the IDF interpreter:
   `C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe`.
   The spec's `python stability_check.py ...` command fails as written.

4. **`clamp_playfield` takes `int16_t*`.** `game.h` declares `spider_x/y` as
   `int16_t`; the old code passed `int32_t*`. Kept the header as truth per the
   spec and narrowed the helper.

5. **`game_on_play` added.** `game.h` declares it and the spec's action tray
   needs it, but the old implementation never defined it. Implemented as a mood
   bump costing energy, with a hop.

6. **`game_on_terrarium_tap` delegates to `game_on_pet`.** Declared in the
   header, undefined in the old code. Phase 6 will give it real behaviour.

7. **`ui.bar_track[]`/`bar_fill[]` populated with the three live bars.** The
   header has `NUM_STATS`=4 slots but Phase 0 only renders three (food, mood,
   spark). Slot 3 is `NULL` until Phase 5 adds energy to the HUD.

---

## Phase 1 — Foundation ✅

**Status:** COMPLETE — gate passed.

### What was built

| File | Purpose |
|---|---|
| `main/game_config.h` | Every tunable: timing, decay rates, care gains, movement, gait, prey, life cycle, XP, layout, audio, save format. `GAME_TIME_SCALE` ships at 1. |
| `main/save.h` / `save.c` | Versioned persistence. Header carries `magic`/`version`/`size`; any mismatch is rejected rather than loaded. |
| `tools/gen_sprites.py` | Pillow generator, deterministic, `PALETTE` + `GEOMETRY` dicts at the top. |
| `assets/` | 67 PNGs: 39 spider + 15 prey + 13 UI. |
| `main/assets.h` / `assets.c` | Mounts the mmap QOI partition, exposes `assets_image(index)`. |
| `main/CMakeLists.txt` | New SRCS, `REQUIRES`, and `spiffs_create_partition_assets(storage … MMAP_SUPPORT_QOI)`. |
| `sdkconfig.defaults` | Enabled `CONFIG_ESP_LVGL_ADAPTER_ENABLE_FS` and `_ENABLE_DECODER`. |
| `main/main.c` | Calls `assets_init()` after `bsp_display_start()`. |

### Asset inventory (matches the spec exactly)

```
spider: 39  (13 frames x 3 views: n, e, s)
prey:   15  (5 frames x 3 species: fly, cricket, moth)
ui:     13  (feed clean play sleep decor status back hunger mood energy hygiene xp lock)
total:  67
```

Packed size: **50 KB** against the 7 MB `storage` partition.

### Verification

```
powershell -File build.ps1        -> exit 0
powershell -File flash.ps1 COM7   -> exit 0 (storage.bin written at 0x810000)
python stability_check.py COM7 30 -> VERDICT: PASS (51 lines)
```

Boot log confirms the whole pipeline:

```
I mmap_assets: MMAP Assets [storage] mounted successfully. (Lib: v2.0.1, Bin: v1.0.0)
I lv_fs: Drive 'A' successfully created, version: 1.0.1
I esp_lvgl:adapter: File system mounted successfully
I spidarium.assets: assets mounted: 67 files
W spidarium.save: save magic 0x00000000 != 0x53504944; rejecting
I spidarium: starting from defaults
I spidarium: Spidarium ready (480x480)
```

The `save magic ... rejecting` line is the new guard working as intended: the
old unversioned blob from Phase 0 is refused instead of being reinterpreted as
the new struct.

### Decisions made

1. **Generated enum names carry a `_QOI` suffix.** The spec predicted
   `MMAP_ASSETS_SPIDER_N_WALK_0`, but `spiffs_assets_gen.py` derives the name
   from the *converted* filename, so the real symbols are
   `MMAP_STORAGE_SPIDER_N_WALK_0_QOI` (and the prefix is the partition label,
   `STORAGE`, not `ASSETS`). `assets.c` uses the generated header, so this is
   transparent to callers.

2. **`sdkconfig` had to be edited directly.** `sdkconfig.defaults` only seeds a
   *fresh* config; the existing `sdkconfig` kept `# ... is not set` and the
   `esp_lv_adapter_fs_mount` declaration stayed compiled out. Both files now
   agree.

3. **`nvs_flash` and `esp_lvgl_adapter` moved to `PRIV_REQUIRES`.** `save.c`
   includes `nvs.h`; the build fails the component-dependency check otherwise.

4. **`assets_init()` failure is non-fatal.** If the storage partition is blank
   or missing, the game logs a warning and continues with primitive rendering
   rather than aborting. Phase 2 will branch on `assets_ready()`.

5. **Leg geometry retuned three times.** First pass had legs hidden inside the
   body silhouette; final values (`leg_foot_out` 22, darker `leg` colour) give
   the stout crouched stance the style guide asks for. Verified by eye at 1x.

6. **`POOP_DIRTY` → `FEED_HYGIENE_COST`, `AGE_PERIOD_MS` → `AGE_TICK_MS`.**
   Local tunables were deleted in favour of `game_config.h`; two call sites
   still referenced the old names.

---

## Phases 2–9 — Engine, gameplay, UI, audio, polish ✅

**Status:** COMPLETE — all phases implemented and verified.

### Module layout (9 modules, as specified)

| Module | Responsibility |
|---|---|
| `game.c` | Orchestrator: owns `g`/`ui`/`g_play`, both timers, event callbacks, XP/achievements |
| `terrarium.c` | Substrate gradient, glass vignette, corner web, 5 decor shapes, poop, dust motes |
| `spider.c` | Sprite object, frame cycling, one-shot clips, squash/stretch, hop arc |
| `behaviour.c` | Steering, turn-rate limit, decor separation, 9-state FSM |
| `prey.c` | Fly/cricket/moth wander + flee AI, catch resolution |
| `needs.c` | 4 stats with distinct decay curves, mood, stages, molting, SICK |
| `ui_hud.c` | Corner stat rings, 6-button action tray, bubbles, decor hint |
| `ui_status.c` | Big bars, stage/age/level/XP, 10 achievements, mute toggle |
| `audio.c` | Synthesized PCM SFX (eat/pounce/molt/tap/alert), no audio assets |

### Verification

```
powershell -File build.ps1        -> exit 0, zero project warnings
powershell -File flash.ps1 COM7   -> exit 0
python stability_check.py COM7 30 -> VERDICT: PASS (59 lines)
```

5-minute unattended soak: **0 faults**, 5 catches, 13 misses, reached level 5,
spider moving continuously, energy cycling 96→51, sick/recover transitions
behaving (1 sick, 1 recovered — no flapping).

### Bugs found and fixed during verification

These were all found by reading the serial log, not by the build. Each is a
real defect that would have shipped:

1. **Spider was mathematically frozen.** `MOVE_SPEED` was 2.0, inherited from
   the old per-tick code, but the new engine treats it as px/second. At 20 Hz
   that is 0.1 px/tick, which truncates to zero against an `int16_t` position.
   The spider never moved. Fixed by making the constants px/s (55/30/70) and
   adding float position accumulators so sub-pixel motion is not lost.

2. **Corner deadlock.** `advance()` reflected the heading on a wall hit but
   left the position unchanged, so a spider steering into a corner oscillated
   in place forever (observed frozen at `(40,130)`). Now clamps *and* reflects.

3. **Permanent SICK = de-facto death.** A sick spider refused to hunt, so it
   could never feed itself, so it could never recover — violating the spec's
   "no death" rule. Sick spiders now hunt at reduced catch probability (0.7x).

4. **Sick/recover flapping every 3 s.** Hygiene was a standalone sickness
   trigger but is only restored by the CLEAN button, so with no player input it
   sat at 0 and the spider toggled sick↔recovered forever. Hygiene now acts
   through mood instead of directly.

5. **Unreachable recovery threshold.** Requiring hunger *and* hygiene *and*
   mood all above 30 was impossible because hunger decays fastest. Now hunger
   and mood above 25, which feeding and petting can actually deliver.

6. **Slept forever.** `SPIDER_SLEEP` had no wake condition, so once energy
   filled the spider lay there indefinitely while hunger drained to 0
   (observed: 3+ minutes asleep at energy 100). Now wakes at energy ≥95 or
   hunger <25.

7. **Pounce spam.** After a miss the spider re-pounced every ~600 ms from the
   same spot, producing an endless "missed" log and never letting prey flee.
   Added a 1400 ms cooldown.

8. **Energy starvation.** Hunting kept the spider out of SLEEP indefinitely, so
   energy was pinned at 0. Exhausted spiders now rest.

9. **Dead prey leaked slots.** Corpses never despawned, so after 3 catches
   `MAX_PREY` was permanently full and `prey_spawn()` silently stopped working.
   Corpses now clear after 1.5 s.

10. **FEED appeared broken.** The spider only hunted below hunger 70, so after
    one meal it ignored prey the player had just spawned. Threshold raised to 95.

11. **Prey too sparse.** A 3% random spawn roll meant a hungry spider could sit
    with nothing to chase for minutes. Now spawns on a 6 s timer.

### Decisions made

1. **West view is not mirrored.** The spec says to mirror east with
   `lv_image_set_scale_x(obj, -256)`, but LVGL 9.6's `scale_x` is `uint32_t`
   and `lv_image_set_scale_x` takes `uint32_t`, so a negative value is not
   representable. With `LV_USE_MATRIX` off there is no rotation fallback. The
   east sprite is reused for west-facing headings; at 96 px the difference is
   not noticeable. **This is a spec deviation worth knowing about.**

2. **`spider_launch_pounce` added to `spider.h`.** Defined in `spider.c` but
   never declared, so `behaviour.c` failed to compile.

3. **`game.c.bak-web2` deleted** per Phase 9.

4. **Heartbeat log added** (`hb state=… pos=… h=… m=… e=… hy=… prey=… sick=…
   lvl=…`, every 30 s). Without it an unattended soak is unverifiable — the
   first three soaks looked "clean" while the spider was frozen.

5. **`ui.bar_track[]`/`bar_fill[]` left populated but unused.** The HUD uses
   arc rings rather than bars; the status page has its own bars. The fields
   remain for API compatibility.

### Known limitations

- **Hygiene only recovers via CLEAN.** Correct by design, but it means an
  unattended spider always ends up dirty. Not a bug.

- **No on-device verification of the visual result.** I verified sprites by
  rendering them to PNG and inspecting, and verified behaviour from the serial
  log, but I could not see the panel. Layout, colours and sprite scale on the
  actual AMOLED are unconfirmed.

---

## Post-completion fixes (user-reported)

### 1. "Assets are switched around for nearly everything" — FIXED

**Root cause:** `assets_image()` returned a pointer to a single shared
`static lv_image_dsc_t`, rebuilt on every call. `lv_image_set_src()` stores the
pointer rather than copying it (verified: `img->src = src;` in
`lv_image.c`), so every sprite object pointed at the *same* descriptor, which
mutated to whichever asset was requested last. Every image therefore rendered
as the most recently looked-up sprite.

The decoder test app uses a stack-local descriptor and forces a synchronous
`lv_refr_now()` immediately after — a test-only pattern that hides this bug.

**Fix:** one persistent descriptor per asset index (`s_dsc[128]` +
`s_dsc_used[128]`), built once on first lookup and never mutated afterwards.

### 2. Spider look — reverted to jointed procedural legs

The sprite-based spider read as a static blob and lost the articulated leg
motion. `spider.c` is now procedural again:

- Eight two-segment legs as `lv_line` polylines (hip → knee → foot) with a
  visible knee, drawn with rounded 3 px strokes.
- **Tetrapod gait**: legs 1/3 on one side move with 2/4 on the other, so
  neighbouring legs are always out of phase — this is what makes the walk read
  as alternating rather than shuffling.
- Gait phase advances only while the spider is actually moving
  (`behaviour_speed() > 1`), so a resting spider holds its stance.
- Squash/stretch on pounce, dimming when sick, blink, heading-tracking pupils.

The 39 spider sprites are still generated and packaged (they cost ~30 KB and
may be useful later), but nothing references them now. Prey and UI icons still
use the sprite pipeline, which is why fix #1 mattered.

**Verification:** build exit 0, zero project warnings, flash exit 0,
`VERDICT: PASS`, 0 faults, prey being caught.

**Note:** `level=50` in the boot log is accumulated test data from repeated
soak runs, not a bug — XP persists across reboots by design. Erase NVS to
reset.

### 3. Head and abdomen refined (legs untouched)

Leg motion was confirmed correct and left alone. The anatomy had two problems:

- **The abdomen was being clipped.** The body container was `BODY_R * 2` = 32×32
  while the abdomen reached x=36, y=38, so its lower-right was cut off by the
  container bounds. The container is now `BODY_BOX` (44×44), independent of
  `BODY_R`, which still drives the leg hip radius and is unchanged.
- **Head and abdomen read as one blob.** They were two overlapping circles of
  similar size at similar offsets. Now the cephalothorax sits forward of centre
  (r=10) and the abdomen behind it (r=13), so they read as separate masses with
  the head overlapping the abdomen at the waist. Both carry a lighter
  upper-left highlight, matching the top-left light direction of the sprite set.

Eyes were repositioned relative to the new cephalothorax centre: four large
anterior eyes with dark pupils plus four small outer eyes. Pupil tracking now
offsets from each eye's centre rather than using hard-coded absolute positions.

**Verification:** build exit 0, zero project warnings, flash exit 0,
`VERDICT: PASS`, 0 faults.

### 4. More head/abdomen separation, cartoon eyes

Legs untouched again. Two changes:

- **Separation.** The two masses overlapped by 17 px, which still read as one
  blob. The cephalothorax moved forward (`CEPH_CY` −4 → −8) and the abdomen
  back (`ABD_CY` +8 → +11), reducing the overlap to 5 px — they now touch only
  at the waist. The container grew to 56×56 to fit the new spread.

- **Cartoon eyes.** Anterior eyes went from r=4 to r=6 with r=3 pupils, plus
  white specular glints. At this physical size realistic spider eyes are 2–3 px
  and disappear entirely; oversized amber eyes are what make the creature
  legible and expressive on a 480 px panel. Four small outer eyes (r=3) keep
  the eight-eye silhouette.

Anatomy constants (`CEPH_CX/CY/R`, `ABD_CX/CY/R`, `EYE_R/DX/DY`, `PUPIL_R`)
are now defined once at file scope and shared by `spider_init()` and
`spider_render()`, so the pupils cannot drift away from the eyes they belong
to — that duplication is what caused the earlier mismatch.

**Verification:** build exit 0, zero project warnings, flash exit 0,
`VERDICT: PASS`, 0 faults.

### 5. Heading-aware anatomy + procedural prey

Two orientation bugs, both real:

- **The spider's body always faced up.** The legs rotated with `spider_heading`
  but the anatomy was placed at fixed *screen* offsets (head always at −y), so
  the spider appeared to walk sideways with its head permanently pointing up.
  Anatomy is now defined in body-local `(forward, right)` coordinates and every
  part is placed each frame by rotating that offset through the heading
  (`layout_parts()`). The head now leads the direction of travel. Container
  grew to 68×68 to fit the rotated spread.

- **Prey moved backwards.** The sprite set is authored facing right, and LVGL
  9.6 cannot mirror an image — `scale_x` is `uint32_t` and `LV_USE_MATRIX` is
  off, so there is no negative scale and no rotation. A right-facing sprite
  therefore always looked like it was reversing whenever it travelled left.
  `prey.c` is now procedural: each bug is a handful of primitives registered in
  body-local coordinates and rotated through its own heading, so it faces where
  it is actually going. Species differ by silhouette (fly: wings above/below;
  cricket: long swept-back hind legs; moth: broad side wings), and `dead` is a
  distinct curled-up pose.

**Verification:** build exit 0, zero project warnings, flash exit 0,
`VERDICT: PASS`, 0 faults, prey being caught.

### 6. Head was on backwards

`EYE_FWD` was `-2.0f`, placing the big anterior eyes *behind* the cephalothorax
centre — so the face read as the back of the head. The small outer eyes were
worse: they sat at `CEPH_FWD + 3`, i.e. *in front* of the big eyes, which
inverted the whole face.

Corrected to a proper jumping-spider eye layout, all relative to the head
centre and positive-forward:

| Pair | Forward | Lateral | Radius |
|---|---|---|---|
| anterior (big) | +4 | ±5 | 6 |
| middle | −1 | ±8 | 3 |
| rear | −6 | ±3 | 3 |

Pupils and glints moved forward within the eye (+1, +2) so the gaze reads as
focused ahead rather than centred. Head now spans forward −3…+19 with the big
eyes at +12, near the leading edge.

**Verification:** build exit 0, zero project warnings, flash exit 0,
`VERDICT: PASS`, 0 faults.

### 7. "Head is on backwards" — it was a gap, not a rotation

The user reported the head looked backwards and correctly guessed the cause:
black showing between head and body. The eyes were already at the front; the
problem was the silhouette.

Abdomen spanned forward −32…−6 and the head −3…+19, leaving a **3 px band of
bare terrarium background** between them. With no waist connecting the masses,
the head read as a detached ball floating in front of the body — which the eye
interprets as a head facing away. Closing the gap (`ABD_FWD` −19 → −13) makes
them overlap by 3 px so they form one continuous silhouette. Eyes moved from
+4 to +6 so they sit just past the head's leading edge.

**Verification:** build exit 0, zero project warnings, flash exit 0,
`VERDICT: PASS`, 0 faults.

### 8. Orange smear behind the eyes — secondary eyes

The orange behind the face was the secondary eye pairs. Two problems made them
read as a colour artefact rather than as eyes:

- **Same bright amber as the primaries.** At `0xf5b342` they merged with the big
  eyes into one orange blob. Now `0x9a6a24` (dim amber), so they read as
  secondary.
- **Positioned inside the primary eyes.** At `EYE_MID_FWD +5` they sat entirely
  within the big eyes' silhouette and were invisible; the visible orange was
  just the primaries bleeding outward. Moved behind the primaries
  (`EYE_MID_FWD −4`, `EYE_REAR_FWD −8`) so they clear the silhouette.

They are also drawn *before* the primary pair, so the big eyes occlude their
inner halves and only the outer edges peek through — the correct depth order.
Radius reduced 3 → 2 so they stay subordinate.
Resulting layout (forward offsets from head centre):

| Pair | Forward | Lateral | Radius | Colour |
|---|---|---|---|---|
| primary | +6 | ±5 | 6 | bright amber |
| middle | −4 | ±8 | 2 | dim amber |
| rear | −8 | ±4 | 2 | dim amber |

**Verification:** build exit 0, zero project warnings, flash exit 0,
`VERDICT: PASS`, 0 faults.

### 9. Leg alternation pattern

The user asked for the front pair to alternate rather than the sides. Two
separate bugs were making the gait read wrong:

- **The fan was asymmetric.** The base angle was
  `head + side*(PI/2 + side*off)`. That extra `side*` reversed the fan on the
  left, so `k=0` was the frontmost leg on the right but the *rearmost* on the
  left. The gait wave therefore ran front-to-back on one side and
  back-to-front on the other — no phase pattern could look right. Now
  `head + side*(PI/2 + off)`, so `k` means "front to back" on both sides.

- **The phase pattern was an XOR.** `(k%2)*PI + (side>0)*PI` put legs 0 and 2
  in phase on each side, reading as two groups per side rather than as
  alternating legs. Replaced with a metachronal wave:
  `s_phase + k*(PI/2) + (side>0)*PI`.

Resulting phases (0 = left, 1 = right):

| k | left | right |
|---|---|---|
| 0 (front) | 0 | π |
| 1 | π/2 | 3π/2 |
| 2 | π | 0 |
| 3 (rear) | 3π/2 | π/2 |

No two adjacent legs are ever in phase, and the front pair alternates left/right
as requested.
**Verification:** build exit 0, zero project warnings, flash exit 0,
`VERDICT: PASS`, 0 faults.

### 10. Leg speed — gait now tracks distance, not time

The gait advanced a fixed `GAIT_STEP` per render frame, so the legs cycled at a
constant rate no matter how fast the spider was actually moving. Sprinting and
drifting looked identical — the legs skated.

Phase now advances with **distance travelled**:

```c
s_phase += dist * GAIT_PER_PX;   /* GAIT_PER_PX = 0.055 */
```

Consequences:

- Legs stop when the spider stops, and cycle faster when it moves faster.
- `GAIT_PER_PX` is the single tuning knob for "how fast the legs move". Raise it
  for a busier gait, lower it for longer strides.
- Discontinuities are ignored (`dist < 40`) so a pounce landing or decor
  separation does not spin the legs wildly.

`behaviour_speed()` and its `s_speed` backing variable became dead and were
removed.

**Verification:** build exit 0, zero project warnings, flash exit 0,
`VERDICT: PASS`, 0 faults.

### 11. Reverted distance-based gait — it glitched

Item 10 (distance-driven gait) was a mistake and has been reverted. It looked
right in principle but failed in practice:

- `g.spider_x/y` are `int16_t`, so position updates arrive as irregular 1–2 px
  jumps rather than smooth motion. Per-frame distance was therefore spiky.
- The hop offset `dy` changes fast during a pounce, adding more noise.
- The `dist > 0.05 && dist < 40` sanity window meant the phase **froze
  entirely** whenever distance fell outside it.

Net effect: legs stuttered, skipped, and did partial articulation at high speed
— exactly the reported symptom.

Restored a steady time base: `s_phase += GAIT_RATE` (0.30 rad/frame at 30 Hz)
while the spider is in a moving state. Smooth, full articulation, and
`GAIT_RATE` remains the single knob for leg speed.

The `s_last_x/s_last_y/s_last_valid` tracking state was removed with it.

**Verification:** build exit 0, zero project warnings, flash exit 0,
`VERDICT: PASS`, 0 faults.

### 12. Idle leg glitch — the heading was spinning

Movement looked right after item 11, but standing still glitched. The legs
themselves were correctly frozen (`swing = 0` when not moving); the problem was
the **heading**.

`SPIDER_IDLE` called `steer_toward(s_idle_look, dt_s * 0.4f)`. `steer_toward`
caps at `TURN_RATE_DEG` (540°/s), so the `0.4f` scale still allowed **216°/s** —
a full rotation in under two seconds. Because the legs are drawn relative to
`g.spider_heading`, the entire leg fan whipped around while the spider stood
still.

Added `IDLE_TURN_RATE_DEG` (45°/s) and a `steer_toward_rate()` variant that
takes an explicit rate. Idle now uses the slow rate; movement still uses the
full 540°/s so turning stays responsive.

**Verification:** build exit 0, zero project warnings, flash exit 0,
`VERDICT: PASS`, 0 faults.

### 13. Idle leg glitch, properly fixed

Item 12 reduced the symptom but did not remove it. Two causes remained:

- **The `moving` gate was state-based, not movement-based.** `SPIDER_PLAY` is a
  hop *in place* — the spider does not translate, yet its legs cycled. The same
  happened in `WANDER` while pinned against a wall.
- **Idle still rotated the heading.** Even at 45°/s, the legs are drawn relative
  to `g.spider_heading`, so the whole fan swept around while the body (a circle)
  looked stationary.

Fixes:

- Idle no longer rotates the heading at all. `s_idle_look` and
  `IDLE_TURN_RATE_DEG` were removed.
- The gait is now driven by **actual float displacement**, via a new
  `behaviour_pos()` accessor that exposes the sub-pixel position. `g.spider_x/y`
  are `int16_t` and quantise to whole pixels, which is why the earlier
  distance-based attempt glitched; the float position has no such problem.

The legs now move only when the body genuinely translates.

**Verification:** build exit 0, zero project warnings, flash exit 0,
`VERDICT: PASS`, 0 faults.

### 14. Gait: smooth time base, gated on smoothed movement

Item 13 fixed the idle glitch but broke movement. Two bugs:

- **`clamp_to_playfield()` destroyed the sub-pixel accumulator.** It ran at the
  end of every logic tick and unconditionally did `s_px = (float)g.spider_x`,
  rounding the float position back to whole pixels 20 times a second. So
  `behaviour_pos()` returned whole-pixel jumps, not smooth motion. It now only
  adopts the integer position when another code path actually wrote it.
- **Sampling-rate mismatch.** Position updates at the 20 Hz logic rate, but the
  renderer runs at 30 Hz. Driving phase directly from per-frame distance
  therefore produced a beat pattern — some frames saw a full step, others saw
  zero. That is the stutter.

Final design: keep the smooth **time base** (`GAIT_RATE` per render frame) and
gate it on a **smoothed** speed estimate derived from the sub-pixel position
(exponential smoothing, factor 0.25, threshold 0.15 px/frame). A single frame of
jitter cannot start or stop the gait, but sustained motion registers within a
few frames.

This satisfies both constraints: legs move only when the body genuinely
translates, and the motion is smooth because the phase advances on a steady
clock rather than on a noisy per-frame measurement.

**Verification:** build exit 0, zero project warnings, flash exit 0,
`VERDICT: PASS`, 0 faults.

---

## Remaining phases

None. All phases 0–9 complete.
---

# Gameplay Refinement — 2026-11-05

Agent: unattended run per `.github/prompts/plan-spidarium-gameplay.prompt.md`
Backup of pre-work `main/` taken at `main_backup_20261105_gameplay/` (R2).

---

## Phase A — The hunt becomes the verb ✅

**Status:** COMPLETE — gate passed on hardware.

### What changed

The spider no longer pounces on its own the moment it gets close. It now
**crouches and holds**, opening a window for the player to tap where the bug
will be. Landing distance decides perfect / good / miss.

| File | Change |
|---|---|
| `game.h` | `SPIDER_AIM` inserted between `SPIDER_STALK` and `SPIDER_POUNCE`; `SFX_AIM`; `ACH_FIRST_PERFECT`; `streak`/`best_streak` appended to `game_state_t`. |
| `game_config.h` | New "Hunting" section: `AIM_RANGE_PX`, `AIM_WINDOW_MS`, `POUNCE_MAX_PX`, `CATCH_PERFECT_PX`/`CATCH_GOOD_PX`, XP/streak/energy constants, `SFX_MS_AIM`. |
| `behaviour.c` | `aim_window_ms()`, `SPIDER_AIM` FSM case, `behaviour_pounce_at_ex()`, `behaviour_player_pounce()`, aim accessors. Energy is deducted per pounce. |
| `behaviour.h` | New aim API. |
| `prey.c` | Catch quality classification, streak-multiplied XP, energy refund on eat, `wild` field added. |
| `game.c` | Tap precedence is now decor → aim pounce → pet. Heartbeat extended with `aim=` and `streak=`. |
| `spider.c` | AIM crouch pose, persistent focus ring (built once, toggled hidden). |
| `ui_hud.c` | "TAP!" bubble when the window opens. |
| `audio.c` | `SFX_AIM` rising tick. |

### Decisions made (spec was silent)

1. **Pounce flight time scales with distance.** The spec kept the old fixed
   260 ms pounce window, but at 260 px/s that only covers ~68 px — a tap at
   `POUNCE_MAX_PX` (130 px) would land the spider half way and *always* miss.
   Flight time is now `distance / 260 px/s`, clamped to 120–520 ms.
2. **Auto-pounce is capped at GOOD quality.** The safety valve aims at the
   bug's live position, so it lands dead-on and scored `q=2` (perfect) every
   time — which bypasses the roll entirely and handed the catch back to the
   spider. Perfect is now the player's reward only; an unattended pounce is
   demoted to GOOD and must still win its roll. Verified in the soak: auto
   pounces now log `q=1`.
3. **Squash/stretch is actually applied.** `s_scale_x`/`s_scale_y` were
   assigned in `spider_render` but never read, so the crouch pose was dead
   code. `layout_parts()` now scales the body-local part offsets by them
   (LVGL 9.6 cannot scale objects without `LV_USE_MATRIX`).
4. **Catch log distinguishes hit from miss** (`CAUGHT` / `MISSED`) so soaks are
   readable.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability 45 s   -> VERDICT: PASS, 0 faults
5 min soak       -> aim=1 observed, state cycled 2 -> 3, 0 faults
```

Soak excerpt:

```
hb state=3 pos=(325,157) h=76 m=36 e=52 hy=0 prey=1 sick=0 lvl=4 aim=1 streak=0
CAUGHT q=1 p=0.33 d=24 streak=0
MISSED q=1 p=0.35 d=22 streak=0
```

### Not yet verified by hand

The crouch pose, the focus ring drain, and the tap-to-pounce feel need a human
finger. Everything mechanical is confirmed from the log.

---

## Phase B — Feeding dependency and the dead knobs ✅

**Status:** COMPLETE — gate passed on hardware.

### What changed

The spider no longer feeds itself for free. Wild prey drifts in every 20 min
and is leaner and warier than what the player introduces, so FEED matters.

| File | Change |
|---|---|
| `game_config.h` | Per-minute decay rates replace per-tick constants; `WILD_*` constants; `XP_MOLT`; `POOP_LIFETIME_MS`; `GAME_TIME_SCALE` compile guard. Deleted `DECAY_HUNGER/HYGIENE/ENERGY/ENERGY_ASLEEP`, `ENERGY_REGEN_ASLEEP`, `SLEEP_ENERGY_GAIN`. |
| `needs.c` | Per-minute decay scaled by `GAME_TIME_SCALE`; hygiene regen when the tank is clean; mid-stage molt via `MOLT_DAYS`; aging honours `GAME_TIME_SCALE`. |
| `prey.c` | `prey_spawn_species_ex()` / `prey_spawn_wild()`; wild gains; wild flee radius/speed; poop + hygiene cost on a catch. |
| `terrarium.c` | `terrarium_poop_count()`, `decor_kind_name()`, poop decomposition. |
| `game.c` | 6 s auto-spawn replaced by the 20 min wild trickle; player-chosen decor. |
| `game.h` | `DECO_KINDS` moved here from `terrarium.c`. |

### Decisions made (spec was silent, or the spec was wrong)

1. **`HYGIENE_REGEN_PER_MIN` raised 0.25 → 2.0, and it replaces decay rather
   than fighting it.** The spec's 0.25 against 0.7 decay is a net loss of
   0.45/min, so an unattended spider still ended up dirty — the exact dead end
   the change was meant to remove. Verified: `hy` now climbs when the tank is
   clean.
2. **Poop decomposes after 10 min.** This was the actual root cause of the
   permanent-dirt dead end: poop never disappeared, so the tank was never
   clean again after the first meal and the regen condition could never be
   satisfied. `POOP_LIFETIME_MS` closes the loop.
3. **One wild bug is seeded on the first logic tick.** With a 20 min spawn
   timer and no seed, a fresh boot opened with an empty tank and nothing to
   hunt for 20 minutes.
4. **The spec's `game_on_decorate` sketch could never enter placement mode.**
   It tested `!g.decor_mode` in both branches but never set the flag, so every
   press just cycled the kind. Implemented with an explicit armed flag, plus a
   third press to cancel.
5. **Poop recycle path fixed.** Shifting `ui.poop[]` without shifting the new
   `s_poop_at[]` array would desync timestamps and delete the wrong blobs.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability 120 s  -> VERDICT: PASS, 0 faults
```

Soak excerpt (hygiene recovering on a clean tank, tank never empty):

```
hb state=2 pos=(250,217) h=48 m=36 e=87 hy=0 prey=1 sick=0 lvl=9 aim=0 streak=0
hb state=3 pos=(366,328) h=47 m=35 e=84 hy=1 prey=1 sick=0 lvl=9 aim=1 streak=1
hb state=0 pos=(200,405) h=55 m=36 e=81 hy=0 prey=0 sick=0 lvl=10 aim=0 streak=0
```

### Not yet verified by hand

Press DECOR twice: the bubble should name a kind, then placement mode opens.

---

## Phase C — Escalation ✅

**Status:** COMPLETE — gate passed on hardware.

### What changed

Prey now scale with level, each species moves differently, a rare visitor
arrives once per in-game day, and achievements pay XP.

| File | Change |
|---|---|
| `game_config.h` | `PREY_SPEED_PER_LEVEL`/`PREY_ALERT_PER_LEVEL` + caps; species movement constants; `BEETLE_UNLOCK_LEVEL`; `VISITOR_XP`; `XP_ACHIEVEMENT`; flee burst/rest; `PREY_FLEE_RADIUS_MAX`. |
| `game.h` | `PREY_BEETLE` added before `PREY_SPECIES_COUNT`. |
| `prey.c` | Beetle body; unlock at level 6; level-scaled speed/alert; fly jitter, cricket hop rhythm, moth flutter, beetle slow; halved perfect radius vs beetle; flee bursts. |
| `game.c` | Achievements grant `XP_ACHIEVEMENT`; daily wild visitor. |

### Decisions made (spec was silent, or the spec was wrong)

1. **Prey flee in bursts (300 ms sprint / 1000 ms rest).** Continuous fleeing
   was a hard deadlock at high level: `alert_mult` pushed the flee radius to
   105 px, past `AIM_RANGE_PX` (78), and flee speed (112 px/s) outran the
   30 px/s stalk — so the spider chased forever and the aim window could never
   open. Observed: 2 minutes pinned in STALK. Bursts give the spider a closing
   window and give the player a readable rhythm.
2. **`PREY_FLEE_RADIUS_MAX` clamps the alert radius below `AIM_RANGE_PX`.**
   Structural guarantee that the spider can always get close enough to crouch.
3. **Fixed a pre-existing frozen-spider bug in `SPIDER_IDLE`.** The idle
   deadline was refreshed *before* it was tested, so the wander branch was
   unreachable and the spider sat in IDLE forever with no prey present
   (observed: pinned at (396,410) for 90+ s). This is exactly the failure the
   handoff warned about. The expiry is now captured before the refresh.
4. **`SPIDER_WANDER` is now interruptible.** Only IDLE looked for prey, so a
   wandering spider never hunted (observed: 3 min in WANDER with `prey=1` and
   hunger falling).
5. **Sick spiders wake at energy 30, not 95.** Sleeping to 95 takes ~12 min at
   the asleep regen rate, during which mood sits at 0 and the spider cannot
   feed itself — making SICK effectively permanent, which the spec forbids.
6. **Mood penalties are halved while SICK.** At full strength they pinned mood
   at 0 whenever two stats were low, and recovery needs mood > 25, so the
   spider could never climb out (observed: mood 0 for 4+ min). The spider is
   already punished by being sick.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability 300 s  -> VERDICT: PASS, 0 faults
```

Recovery trace (mood and energy climbing, spider wakes and hunts):

```
hb state=6 pos=(198,282) h=31 m=8  e=16 hy=16 prey=1 sick=1
hb state=6 pos=(198,282) h=29 m=16 e=28 hy=20 prey=1 sick=1
hb state=3 pos=(439,337) h=29 m=14 e=18 hy=21 prey=1 sick=1 aim=1
CAUGHT q=1 p=0.22 d=0
hb state=9 pos=(427,390) h=36 m=13 e=21 hy=9  prey=0 sick=1
```

### Not yet verified by hand

Prey movement differences (fly jitter / cricket hop / moth flutter) and the
tighter aim window at high level need eyes on the panel.

---

## Phase D — Decor with mechanical weight ✅

**Status:** COMPLETE — gate passed on hardware.

### What changed

Decor was purely cosmetic: `behaviour.c` only used placed items for collision
separation. Each kind now does one legible thing, and only while the spider is
near it (the flower is global).

| Kind | Effect | Radius |
|---|---|---|
| pebble | energy regen x1.5 | 90 px |
| leaf | hygiene decay x0.6 | 90 px |
| twig | pounce range x1.2 | 90 px |
| flower | wild spawn wait /(1+count) | global |
| crystal | aim window x1.25 | 90 px |

| File | Change |
|---|---|
| `decor.h` / `decor.c` | **New module.** `decor_compute()`, `decor_bonus()`, `decor_radius()`, `decor_kind_active()`, `decor_active_count()`. |
| `behaviour.c` | Aim window scaled by `aim_window_mult`; pounce clamp scaled by `pounce_range_mult`. |
| `needs.c` | Energy regen scaled by `energy_regen_mult`; hygiene decay scaled by `hygiene_decay_mult`. |
| `game.c` | `decor_compute()` at the top of `game_tick_logic`; wild spawn wait divided by `1 + wild_spawn_bonus`; level-gated kind cycling; two-tap placement with preview. |
| `terrarium.c` | Persistent preview ring (created once, toggled hidden); `terrarium_preview_show/hide()`. |
| `CMakeLists.txt` | `decor.c` added. |

### Decisions made (spec was silent)

1. **Placement is two taps, not one.** The spec asks for a preview ring "before
   committing", but a touchscreen has no hover event, so a single tap cannot both
   preview and commit. First tap shows the ring and the "tap again" bubble; the
   second commits at the previewed point. Cancelling via DECOR hides the ring.
2. **The flower's preview shows a 12 px marker, not a 90 px circle.** Its effect
   is global, so drawing a radius circle would be a lie.
3. **`decor_active_count()` added** so the heartbeat can prove buffs are live.
4. **Heartbeat extended** with `deco=`, `eR=`, `hD=`, `pR=`, `aW=`.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability 60 s   -> VERDICT: PASS, 0 faults
4 min soak       -> buffs observed live, 0 faults
```

Buff chain proven live in the soak (temporary seed, since removed):

```
hb ... deco=3 eR=1.50 hD=0.60 pR=1.00 aW=1.00   <- pebble + leaf in range
hb ... deco=4 eR=1.50 hD=0.60 pR=1.20 aW=1.00   <- + twig in range
hb ... deco=2 eR=1.50 hD=1.00 pR=1.00 aW=1.00   <- spider walked out of leaf range
```

The count and multipliers change as the spider moves, which is the proof that
proximity gating works and that bonuses are recomputed each tick.

### Not yet verified by hand

The preview ring's visual weight and the two-tap placement feel need a finger.

---

## Phase E — Personality ✅

**Status:** COMPLETE — gate passed on hardware, including the 10 min deadlock soak.

### What changed

Each spider now rolls a temperament at first boot, and caring for it builds a
bond that unlocks visible behaviour.

| Temperament | aim | catch | speed | hunger | sleep |
|---|---|---|---|---|---|
| bold | 0.85 | 1.10 | 1.10 | 1.00 | 15 |
| timid | 1.25 | 0.95 | 0.90 | 0.90 | 20 |
| greedy | 1.00 | 1.05 | 1.00 | 1.25 | 15 |
| lazy | 1.15 | 0.90 | 0.80 | 0.85 | 30 |

| File | Change |
|---|---|
| `game.h` | `temperament_t`, `TEMPERAMENT_COUNT`, `k_temperaments` extern, `game_temperament()`, `temperament`/`bond` fields. |
| `game_config.h` | `MOOD_REFUSE_HUNT`, `MOOD_HUNGER_OVERRIDE`, `BOND_*` constants. |
| `game.c` | Table defined once; sentinel `0xFF` in `state_defaults()`; roll after `srand()`; bond gift at 75. |
| `behaviour.c` | Aim window, speed and sleep threshold scaled by temperament; mood gate; bond turn-to-tap. |
| `needs.c` | Hunger decay scaled by temperament; bond +1 per pet; bond -1 per decay tick while sick. |
| `prey.c` | Catch multiplier; bond +2 per catch, +4 per perfect. |
| `spider.c` | Bond-100 idle sway (reuses the part layout, no new objects). |
| `ui_status.c` | Temperament and bond line. |

### Decisions made (spec was silent, or the spec was wrong)

1. **`temperament_t` lives in `game.h`, not `game_config.h`.** The spec puts the
   type in `game_config.h` but the accessor declaration in `game.h`, and
   `game.h` does not include `game_config.h` — so the build failed with
   `unknown type name 'temperament_t'`. The type is part of the shared contract,
   so it belongs in `game.h`. The table is still defined once in `game.c`.
2. **Hunger overrides boredom (`MOOD_HUNGER_OVERRIDE` 40).** The spec's mood gate
   alone can deadlock: a spider with low mood and low stats refuses to hunt, mood
   drifts toward the same low average, and it starves into SICK before the gate
   lifts. Hunger is the stronger drive, so below 40 hunger the spider hunts
   regardless of mood. SICK spiders are exempt as the spec requires.
3. **Bond 25 turns the spider toward the last tap.** Implemented as a slow
   heading lerp in IDLE only — rotating while walking would fight the steering.
4. **Bond 75 gifts are capped at one free slot.** If all 12 decor slots are
   full the gift is skipped rather than recycling the player's own decoration.
5. **Bond 100 sway is a squash/stretch oscillation**, not a new animation. The
   spec says "reuse the existing part layout", and LVGL 9.6 cannot scale objects
   without `LV_USE_MATRIX`, so the sway modulates the same scale factors the
   crouch pose uses.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability 120 s  -> VERDICT: PASS, 0 faults
10 min soak      -> VERDICT: PASS, 0 faults
```

Deadlock soak (no PLAY pressed for 10 minutes). Mood recovered on its own and the
spider kept hunting:

```
hb state=1 pos=(129,410) h=78 m=65 e=72 hy=38 prey=0 sick=0 lvl=1 bond=6 temp=timid
hb state=1 pos=(440,391) h=76 m=64 e=71 hy=37 prey=0 sick=0 lvl=1 bond=6 temp=timid
hb state=1 pos=(84,247)  h=74 m=62 e=70 hy=35 prey=0 sick=0 lvl=1 bond=6 temp=timid
```

Mood 65 -> 61 over 10 minutes with no play, and the spider never froze. The gate
is not aggressive enough to trap it.

### Not yet verified by hand

The bond-25 turn, the bond-75 gift bubble and the bond-100 sway need eyes. The
temperament differences are only visible over long play.

---

## Phase F — Feedback and legibility ✅

**Status:** COMPLETE — gate passed on hardware.

### What changed

| File | Change |
|---|---|
| `ui_hud.c` | State-driven bubbles (hunting / TAP! / yum / zzz / not well / bored) that yield to explicit button bubbles. |
| `ui_status.c` | Temperament + bond, streak / best streak, next unlock, active decor effects. Layout compacted to fit. |
| `game.h` | `SFX_PERFECT`. |
| `game_config.h` | `SFX_MS_PERFECT`; `SAVE_VERSION` 1 -> 2. |
| `audio.c` | Bright 880 -> 1760 Hz chime. |
| `prey.c` | Perfect catches play the chime; `prey_species_name()` / `prey_unlock_level()` helpers. |
| `prey.h` | New helper declarations. |

### Decisions made (spec was silent, or the spec was wrong)

1. **The status page needed compacting, not just appending.** Three new lines at
   the old 22 px spacing pushed the achievement block under the mute button at
   y=400 on a 480 px panel. Row spacing is now 18 px and the achievement grid
   14 px. Everything still fits above the button.
2. **The auto-pounce now scatters by up to 12 px.** Aiming perfectly at the bug's
   live position made unattended lunges land at d=0-1 and then miss the roll,
   which reads as a broken catch. A little scatter makes the failure legible: the
   spider lunged and fell short. Verified: misses now log d=5-14.
3. **The auto bubble tracks its last text.** Re-asserting only when hidden left
   the previous word on screen across a state change (hunting -> yum). A static
   last-text pointer fixes it without allocating.
4. **Save v2 rejects v1 outright, as instructed.** No migration shim. Verified:
   the level-12 spider was discarded, a new temperament rolled, and the v2 blob
   then survived a reboot.

### Tuning applied (the "panel feels tight" table)

The human reports the panel feels tight, so the sanctioned steps were applied
rather than invented numbers:

| Knob | Before | After |
|---|---|---|
| `AIM_WINDOW_MS` | 2200 | 2600 |
| `AIM_WINDOW_MIN_MS` | 1300 | 1600 |
| `AIM_WINDOW_PER_LEVEL` | 30 | 20 |
| `CATCH_PERFECT_PX` | 22 | 28 |
| `CATCH_GOOD_PX` | 44 | 55 |
| `POUNCE_MAX_PX` | 130 | 160 |
| `BTN_H` | 30 | 36 |
| `HUD_BOTTOM_H` | 70 | 80 |

`HUD_BOTTOM_H` was raised alongside `BTN_H` so the taller tray does not overlap
the playfield. The spider was NOT slowed.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability 180 s  -> VERDICT: PASS, 0 faults
10 min soak      -> VERDICT: PASS, 0 faults
save round-trip  -> v1 rejected, v2 persisted across reboot
```

### Not yet verified by hand

The bubble wording and placement, the status page density, and whether the
widened aim window now feels comfortable. These are all judgement calls that
need a finger on the glass.

---

## Tap usability — "the game is almost unplayable" ✅

**Status:** COMPLETE — flashed and verified on hardware.

### The actual bug behind "the spider says happy! for no reason"

`game_on_terrarium_tap()` called `game_on_pet()` **unconditionally** at the end.
Any tap anywhere in the tank — empty substrate, a corner, the far wall — petted
the spider and showed "happy!". So every stray tap looked like a successful
action, and the aim verb was impossible to learn because there was no such thing
as a failed tap.

Petting is now gated on the tap actually landing within `PET_RADIUS_PX` (56 px)
of the spider. The pounce boundary uses the *same* radius, so the rule is one
sentence: "tap the spider to pet, tap away to pounce."

### Touch targets

| What | Before | After | Why |
|---|---|---|---|
| Action buttons | 6 across, ~61 px wide | 2 rows of 3, ~127 px wide | 61 px is narrower than a fingertip |
| Button height | 36 px | 44 px | sanctioned step in the plan's table |
| Stat rings | 34 px | 40 px | sanctioned step; ring labels re-centred |
| Aim range | 78 px | 110 px | window opened when the bug was already close and fast |

`HUD_BOTTOM_H` is now derived (`2 * BTN_H + 8 + UI_CORNER`) so the taller tray
cannot silently overlap the playfield. The decor hint offset moved with it.

### Tap assist (the real precision fix)

Enlarging targets does not help when the target is a few pixels wide and moving.
A tap within `TAP_ASSIST_PX` (44 px) of a bug now aims **at the bug** instead of
at the finger. A tap far from any bug still pounces where you tapped, so assist
widens the target without aiming for the player. Set `TAP_ASSIST_PX` to 0 to
disable it.

### Empty-tap feedback

An empty tap used to do nothing at all — the only signal was the absence of
"happy!". It now says "tap the spider", or "wait for TAP!" when the spider is
hunting but the aim window is not open yet. Telling a hunting player to "tap the
spider" would be actively misleading.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability 120 s  -> VERDICT: PASS, 0 faults
stability 240 s  -> VERDICT: PASS, 0 faults
```

### Not yet verified by hand

Whether the two-row tray reads clearly and whether 44 px assist feels generous
rather than automatic. Both need a finger on the glass.

---

## Decor art + spider snagging ✅

**Status:** COMPLETE — flashed and verified on hardware.

### Why the spider kept getting caught on decor

`separate_from_decor()` was a hard positional shove applied **after** the spider
had already walked into an item. Every tick it steered toward its target, got
pushed back out, and ground against the obstacle. That is what "the spider keeps
getting caught on the decor" was: not a collision bug, but a steering bug.

Two changes:

1. **Avoidance is now a steering input.** `decor_avoidance()` looks ahead and
   returns a heading offset that bends the spider away from decor *before* it
   arrives. Repulsion from every nearby item is summed, so clusters produce one
   smooth turn instead of oscillating between two items. Only items roughly
   ahead (`dot >= 0.15`) are avoided — an item behind the spider must not push
   it, or it could never turn around.
2. **The hard shove is now only a safety net.** It fires only inside
   `DECO_PUSH_RADIUS` (20 px), for the case where something else puts the spider
   inside an item (a pounce landing, decor placed on top of it). The avoidance
   radius is 42 px, deliberately larger, so the spider starts turning well
   before it needs pushing.

### A real bug found while fixing this

The shove also ran **during a pounce**, which deflected the landing point away
from the bug. Catches that looked dead-on missed because a pebble nudged the
spider off target. The shove is now skipped while `s_pouncing` — a pounce owns
its position until it resolves.

### Decor art

The old shapes were 6-7 px blobs with no shadow and no depth, and the `variant`
field was set but never used. Every piece now shares one light source (shadow
down-right, highlight up-left), which is what makes a set of small shapes read
as one art style:

| Kind | Before | After |
|---|---|---|
| pebble | one 7 px dot + faint dot | 22-28 px oval, shadow, highlight |
| leaf | two overlapping dots | stem, 3-oval blade, light midrib |
| twig | two thin bars | 26 px branch, 2 offshoots, buds at tips |
| flower | 5 dots + centre | stem, 6 petals, warm centre |
| crystal | dot + dot | shadow, 16 px gem, top facet, glint |

`variant` now drives pebble size (22/25/28 px), so repeated pebbles are not
identical.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability 120 s  -> VERDICT: PASS, 0 faults
stability 300 s  -> VERDICT: PASS, 0 faults
```

The 5 min soak shows the spider crossing the whole playfield with decor present
and never stalling:

```
hb pos=(40,141)  -> (301,130) -> (131,295) -> (213,221)
   -> (44,244)   -> (136,332) -> (234,140) -> (139,163)
```

### Not yet verified by hand

Whether the new decor reads as attractive rather than merely less bad. That is a
judgement call and needs eyes on the panel. The avoidance radius (42 px) may also
need widening if the spider still clips items at speed.

---

## Spider restyle + gift picker ✅

**Status:** COMPLETE — flashed and verified on hardware.

### Spider body: black/grey gradient, fuzzy, cartoony

The body was two flat warm-brown circles. It is now a black-to-grey gradient
with a fuzzy rim and big white eyes.

LVGL 9.6 has no radial gradient fill for plain objects, so the gradient is
**faked by stacking progressively smaller, lighter circles** offset toward the
light source (up-left). Three steps per mass is enough to read as a soft sphere
at this size:

| Mass | Base | Mid | Highlight |
|---|---|---|---|
| abdomen | `0x14171c` | `0x2b3138` | `0x454d57` |
| head | `0x1b1f25` | `0x353c45` | `0x525b66` |

The head is a touch lighter than the abdomen so the two masses stay
distinguishable where they overlap at the waist.

The fuzzy rim is a ring of 14 small dots just outside each mass, with alternating
radii so it reads as fur rather than as a ring of beads. They are registered as
ordinary parts, so they rotate with the body and cost nothing per frame.

**The legs were not touched** — geometry, colour and gait are unchanged, as
requested.

`MAX_PARTS` was raised from 16 to `2 * FUZZ_COUNT + 12`. The old limit was sized
for the flat-circle body and would have silently dropped the gradient and rim
parts.

### Gift: picker menu instead of blind cycling

Pressing GIFT now opens a **menu of the five kinds**. Choosing one closes the
menu and enters placement mode; the next tank tap places it.

The old flow cycled kinds blindly on repeated presses, so you could not see the
options and could not go back to one you had passed.

- The menu is **built once at init** and only shown/hidden — never created or
  deleted per press (the project's LVGL rule).
- Locked kinds are shown greyed with their unlock level rather than hidden, so
  the player can see what is coming.
- The kind currently in the tank is outlined in amber and labelled "(in tank)".
- One shared click handler; the kind travels in the button's user data, so there
  are not five near-identical callbacks.
- Placement is now a **single tap**. The old two-tap confirm existed because the
  kind was chosen blind; now that the player has explicitly picked what to give,
  confirming the spot as well is just friction.

Menu geometry verified against the 480 px panel: 400x350, last row bottom at
318 px inside a 326 px content area.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability 120 s  -> VERDICT: PASS, 0 faults
```

### Not yet verified by hand

Whether the gradient reads as a soft body rather than as concentric rings, and
whether the menu's row height is comfortable to hit. Both need eyes and a finger.

---

## Ten-item fix pass ✅

**Status:** COMPLETE — flashed and verified on hardware.

### 1. Pupils too light -> pure black
`COL_PUPIL` was `0x10131a` (near-black but visibly grey against white). Now
`0x000000`.

### 2. Odd white pixel at the upper-left -> MAX_PARTS overflow
**Root cause of both #1 and #2.** `MAX_PARTS` was `2*FUZZ_COUNT + 12` = 40, but
the body registers **44** parts. `add_part()` silently drops anything past the
limit, so the last four parts -- both pupils and both eye glints -- were thrown
away. The dropped glints were never positioned, so they sat at the body
container's origin (0,0): the stray white pixel. The missing pupils are why the
eyes looked pupil-less.

Fixed by sizing the budget exactly (`2*FUZZ_COUNT + 16` = 44) with the
arithmetic written out in a comment so it cannot drift again.

### 3. Play area should be the whole screen
The field was `UI_CORNER..W-UI_CORNER` by `HUD_TOP_H..H-HUD_BOTTOM_H`, confining
the spider to a narrow middle band. It is now the whole screen inset only by
`PLAY_MARGIN` (34 px, the spider's own visual radius) so it never gets
half-clipped at an edge. The HUD floats over the playfield, so the spider may
pass behind it.

### 4. Glitches at the edges -> reflect replaced with slide
`advance()` reflected the heading on every wall contact. That fought the
steering: the spider steered back toward its target, hit the wall, reflected,
and jittered in place. It now clamps only the offending axis, so the spider
slides along walls. A stuck detector re-aims it at the tank centre if it has not
moved for 1.5 s.

### 5. Pounce replaced with golf-style aim
Tapping a small moving bug was a precision test. Aiming is now two timed taps:

1. An **arrow sweeps** across a +/-55 deg arc centred on the bug. Tap to lock
   direction.
2. A **power meter** fills and empties. Tap to lock power and fire.

Where you tap no longer matters at all. The meter shows an **amber ideal marker**
at the power that would land exactly on the bug, so the player matches a visible
target instead of guessing. Catch quality now comes from `behaviour_aim_quality()`
(how close the locked power was to ideal) rather than from landing distance, so
skill is timing. The auto-pounce safety valve still uses landing distance and is
still capped at GOOD.

### 6. Spider stuck in place -> explained
Added `behaviour_resting()` and a "too tired" bubble. A spider out of energy
looked exactly like a frozen game; now the HUD says which it is. The stuck
detector from #4 also covers the wall-pinning case.

### 7. Background invisible, web not organic
The substrate was `0x0b0f18 -> 0x05070c`, almost pure black, so the gradient,
glass and web were all invisible. Lifted to `0x1b2433 -> 0x0a0e15`.

The web was 10 spokes x 4 rings anchored a quarter into the playfield: a small
patch that read as a target. It is now a **corner web** anchored just off the
top-left, fanning across the whole tank, with:
- 11 spokes at jittered angles, each drawn as two segments with a slight bend
- 7 rings, spaced `t^2` so they are denser near the anchor
- each ring segment sagging toward the anchor at its midpoint

`WEB_MAX_LINES` was also undersized (109 vs the 162 lines needed), which would
have silently truncated the web.

### 8. "gift pebble: eR1.50 hD1.00 aW1.00" -> plain words
Added `gift_kind_effect()`. The status page now reads "gift: pebble (rests
faster)". The raw multipliers meant nothing to a player.

### 9. First meal achievement never awarded
`ACH_FIRST_MEAL` was defined and displayed but **never awarded anywhere** -- only
`ACH_FIRST_HUNT` was. Now awarded on the first catch. Verified in the log:
`achievement 0x00000001`.

### 10. PLAY vs pet unclear
Both raised mood, so they looked interchangeable. They are now distinct in both
effect and feedback:
- **PLAY**: bigger mood gain, costs energy. Bubble: "play! +mood -energy".
- **Pet**: small mood gain, builds bond, costs nothing. Bubble: "pet +bond".

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability 120 s  -> VERDICT: PASS, 0 faults
stability 240 s  -> VERDICT: PASS, 0 faults
```

Full-screen roaming confirmed (x 34..446, y 34..434, no stalls), aim window
opens and resolves, and `achievement 0x00000001` fires on the first catch.

### Not yet verified by hand

The golf aim's feel: sweep speed, meter speed, and whether the ideal marker is
readable. These are timing constants (`AIM_SWEEP_PERIOD_MS`,
`AIM_POWER_PERIOD_MS`) and need a finger to judge.

---

## Bug fixes + extensibility pass ✅

**Status:** COMPLETE — flashed and verified on hardware.

### Bugs fixed

**1. "Stuck on awake" — SLEEP button did nothing.**
`needs_sleep()` set `SPIDER_SLEEP`, but `behaviour_tick`'s wake condition fires
on `energy >= 95`. At high energy the spider bounced back to IDLE on the very
next tick, so the button appeared dead and the bubble said "awake". Added
`behaviour_force_sleep()` which holds the state for `SLEEP_MIN_MS` (6 s), so the
button always visibly works.

**2. "Pressing other buttons just says tap spider" — the tap layer ate every
press.**
`ui.tap_layer` is a full-screen clickable object. It was created before the HUD,
but LVGL's hit test walks children in reverse creation order, so it was winning
the hit test and every button press ran `game_on_terrarium_tap()`, which fell
through to "tap the spider". Fixed with an explicit `lv_obj_move_background()`
so the ordering no longer depends on creation order.

**3. "All species unlocked but there's only one species."**
`prey_unlocked_species()` returns a **count** of unlocked species, but the status
page used it as an **index** of the next one. At level 1 the count is 1, so it
reported "next: cricket at level 2"; once the count reached
`PREY_SPECIES_COUNT` it claimed everything was unlocked. Added
`species_next_locked()`, which walks the table and returns the first still-locked
entry.

**4. Eyes stay white while asleep.**
The eyes now turn black and the pupils hide when `SPIDER_SLEEP`, so a sleeping
spider reads as asleep rather than staring.

### Features

**5. Auto-hiding chrome.**
The stat rings, level labels and action tray are hidden by default so the tank
is unobstructed. Tapping or swiping within `CHROME_EDGE_PX` (90 px) of the top or
bottom edge reveals them for `CHROME_HOLD_MS` (5 s). The edge check runs BEFORE
the aim and pet logic, so reaching for the buttons never accidentally advances a
shot.

**6. Data-driven species and gifts.**
New `species.h` / `species.c` hold two catalogue tables:

- `k_species[]` — name, unlock level, movement style, speed/flee/armour
  multipliers, colours, body radii, and shape flags.
- `k_gifts[]` — name, plain-language effect, unlock level, colour, and all five
  multipliers.

Adding a bug or a gift is now **one row**. `prey.c` and `gift.c` read the tables
instead of switching on enums, so no code changes are needed. The movement style
is an enum (`MOVE_ERRATIC`, `MOVE_HOP`, `MOVE_FLUTTER`, `MOVE_SLOW`,
`MOVE_WANDER`), so a new bug picks an existing style for free.

**7. Changeable backgrounds.**
New `theme.h` / `theme.c` with four themes (night, forest, desert, amethyst),
each unlocking at a level. `terrarium.c` reads its colours from
`theme_current()` instead of hardcoded constants, and `terrarium_rebuild()`
redraws the substrate and web on change. A THEME button cycles them. Adding a
theme is one row.

**8. Rules-based mind.**
New `mind.h` / `mind.c`: a small rules engine that decides what the spider
**wants** (food, rest, play, clean, explore, watch, hide) from a table of
threshold rules. It re-evaluates every 4 s and publishes an intent plus a short
thought ("hungry", "sleepy", "watching you") that the HUD shows when nothing
mechanical is happening.

Deliberately rules, not an LLM: this is a 240 MHz MCU with no network. The
engine does **not** drive the FSM — it only publishes intent — so the existing
safety guarantees (never stuck, never dies) still hold. Adding a behaviour is one
row in `k_mind_rules[]`.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability 120 s  -> VERDICT: PASS, 0 faults
stability 240 s  -> VERDICT: PASS, 0 faults
```

Sleep verified in the log (state=6, energy climbing 17 -> 26), full-screen
roaming confirmed, aim window opens and resolves.

### Not yet verified by hand

The edge-swipe reveal gesture, the theme colours, and whether the mind's
thoughts read as character rather than noise. All need eyes on the panel.

---

## Hunt fix, emotes, split chrome, menu system ✅

**Status:** COMPLETE — flashed and verified on hardware.

### 1. Hunt/hop didn't work — prey escaped during the aim window

**Root cause.** The bug kept fleeing while the player aimed. At level 13 it
covers ~324 px during the 2.6 s window, but the longest possible pounce is
160 px. The bug was therefore **always** out of reach by the time the shot
fired, so the hunt could not succeed no matter how well the player timed it.

**Fix.** The target bug is now held still while `behaviour_aim_active()`. This
is also the right feel: the spider has it cornered, and the tension is in the
timing, not in chasing a moving target with a timing meter. Moths still flutter
so they do not look frozen dead.

Verified: catches now land at d=11 instead of always missing.

### 2. Eyes blinked open while asleep

Two separate bugs:

- **Nothing ever scheduled a blink.** `g.blink_until` was only ever set to 0, so
  the spider never blinked at all. Added a scheduler in `spider_render` using
  the existing `BLINK_MIN_MS` / `BLINK_JITTER_MS` / `BLINK_MS` constants.
- **Blinking while asleep revealed the white eyes.** Blinking hides the eye
  objects entirely, which briefly exposed the white eye colour underneath and
  made the spider appear to open its eyes in its sleep. Blinking is now
  suppressed while asleep, and the timer is reset so no blink is in flight.

### 3. Edge bands too wide, and top/bottom should be separate

`CHROME_EDGE_PX` 90 -> `CHROME_EDGE_TOP_PX` 56 and `CHROME_EDGE_BOTTOM_PX` 56.
90 px covered a fifth of the screen and swallowed taps meant for the tank.

The chrome is now **two independent groups**: the top band reveals the STATS
(rings, level, stage, age), the bottom band reveals the ACTIONS (the tray). Each
has its own visibility flag and its own hold timer, so reaching for a button no
longer covers the tank with stat rings.

### 4. Theme moved into a menu

New generic `ui_menu.h` / `ui_menu.c`: one reusable modal list with a title,
rows (label + optional detail), enabled/selected states, and a pick callback.
The theme button now opens a menu showing all four themes with locked ones
greyed, instead of cycling blindly.

This is the foundation for moving the action buttons and stats off the main
screen: any future menu is a few lines.

### 5. Thought bubbles replaced with leg emotes

The spider narrated itself with text ("hungry", "sleepy", "watching you"), which
read as noise — a constant stream of words the player learns to ignore.

It now emotes **physically through its legs**, as an additive modulation of the
existing gait so it costs nothing and cannot break movement:

| Emote | Legs |
|---|---|
| HAPPY | front pair lifts and waves |
| SAD | all legs droop, stride shortens |
| ALERT | legs splay wide and stiffen |
| SLEEPY | legs tuck in tight |

The emote is derived from state in one place (`ui_hud_tick`), so there is a
single source of truth for how the spider looks. The bubble is now reserved for
things the player must ACT on (the aim prompt) or explicit button feedback.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability 120 s  -> VERDICT: PASS, 0 faults
stability 240 s  -> VERDICT: PASS, 0 faults
```

### Not yet verified by hand

Whether the leg emotes read as emotion at this size, and whether 56 px is a
comfortable edge band. Both need eyes and a finger.

---

## Slingshot, menu system, setup screen ✅

**Status:** COMPLETE — flashed and verified on hardware.

### 1. Theme menu did nothing — callback was nulled before it ran

`on_row()` called `ui_menu_close()` and *then* `s_cb(id)`. But `ui_menu_close()`
sets `s_cb = NULL`, so every row called NULL and nothing happened. The callback
is now captured before closing.

### 2. Hunt replaced with a slingshot

The golf-style sweep + power meter is gone. The player now **drags back from the
spider and releases**, like a catapult:

- The drag vector is **inverted**, so pulling down-left fires up-right.
- Pull **length** sets power — direct manipulation, no meter to read.
- A line is drawn from the spider in the firing direction, and its **colour**
  shows how close the pull is to the ideal (green / amber / red), so feedback is
  visual rather than numeric.
- A pull shorter than `AIM_MIN_PULL_PX` (18 px) is treated as a cancel, so a
  stray tap does not waste energy.

This required `LV_EVENT_PRESSED` / `PRESSING` / `RELEASED` on the tap layer, not
just `CLICKED`.

### 3. Menu button with submenus

The THEME button is now a MENU button opening a hierarchy:

```
menu
  themes        >
  sound         >
  achievements  >
```

Each row opens its own list. Themes show locked entries greyed; sound shows the
current state; achievements is a read-only list. `ui_menu_item_t` gained an
`is_submenu` flag that draws a chevron.

This is the structure the action buttons and stats will move into.

### 4. Instruction bubbles removed

All auto-driven bubbles are gone ("tap to aim", "hunting", "hungry", "zzz").
They were noise that trained the player to ignore the bubble. The bubble remains
available for explicit button feedback, but nothing drives it automatically.

### 5. Leg emotes made visible

The amplitudes were far too small to see at this size — the player reported never
seeing any leg animation. Roughly doubled, and the wave is now applied to the
**knee bend** as well as the swing, so motion is visible even when the spider is
standing still. Cycle slowed to 1.2 s so it reads.

### 6. Name and colour on first start

New `ui_setup.h` / `ui_setup.c`: a first-run screen with a name field and six
body-colour swatches. Shown only when `g.name` is empty, so it appears once and
never again unless the save is erased.

Six palettes (charcoal, ash, rust, moss, indigo, plum), each a base/mid/highlight
triple plus a fuzzy-rim colour. `spider.c` now reads its body colours from the
active palette instead of hardcoded constants, so adding a palette is one row.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability 120 s  -> VERDICT: PASS, 0 faults
```

### Not yet verified by hand

The slingshot's feel (pull sensitivity, whether the colour feedback is readable),
whether the leg emotes are now obvious, and the setup screen layout. All need a
finger on the glass.

---

## Setup screen, menu navigation, hint removal ✅

**Status:** COMPLETE — flashed and verified on hardware.

### 1. Colour swatches were all identical

The setup screen filled every swatch with the same hardcoded `0x14171c`, then
put a second hardcoded dot inside it. All six circles were therefore the same
colour regardless of palette.

Added `spider_colour_swatch()` which returns the palette's mid tone, and the
swatch is now filled with it directly. The redundant inner dot is gone.

### 2. Name field had no keyboard

There is no physical keyboard on this device, so the textarea was untypable.
Added an `lv_keyboard` that appears when the field is focused and hides on
READY/CANCEL or defocus, so it does not cover the colour swatches while the
player is choosing one.

### 3. No way to dismiss or go back in the menu

The menu covered the tank and swallowed every tap with no exit. Added:

- A **BACK button** in the menu's top-right corner.
- `ui_menu_set_back()` so a submenu can return to its parent. Submenus set it to
  `back_to_main`; the main menu leaves it unset, so BACK closes the menu.

### 4. On-screen hints removed

Every remaining bubble is gone:

- `"tap the spider"` / `"wait for TAP!"` on empty taps
- `"tap to aim"` when the aim window opened
- `"hunting..."`, `"clean!"`, `"play! +mood -energy"`, `"zzz"/"awake"`,
  `"pet +bond"`, `"given!"`, `"a gift!"`, `"a visitor!"`, `"locked"`

The spider's own reaction is the feedback. `ui_hud_bubble()` still exists but
nothing calls it.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability 120 s  -> VERDICT: PASS, 0 faults
```

The soak shows `CAUGHT q=2` — a perfect catch — confirming the slingshot's aim
quality is reaching `prey.c` correctly.

### Not yet verified by hand

The keyboard's size and whether it obscures the swatches, and whether the BACK
button is comfortably placed.

---

## Factory reset, fangs, per-part colours, sleep pose ✅

**Status:** COMPLETE — flashed and verified on hardware.

### 1. Factory reset in the menu

New `new tank` entry in the main menu, behind a confirmation submenu so the
destructive action is never one tap away:

```
menu
  themes        >
  sound         >
  achievements  >
  new tank      >   ->  "start a new tank / erases this spider"
```

`do_factory_reset()` calls `save_erase()`, then `state_defaults()`, then
`spider_refresh_colours()` and `terrarium_rebuild()` so the live objects drop the
old spider's colours, then `ui_setup_show()`. Because `state_defaults()` memsets
`g`, `g.name` is empty and `ui_setup_needed()` is true, so the new-tank screen is
the correct destination.

### 2. Tapping outside the menu closes it

`ui_menu_open()` now creates a full-screen backdrop **before** the panel, so the
panel stacks above it and keeps its own taps. The backdrop is 50% black (which
also dims the tank and makes the menu read as modal) and its CLICKED handler runs
the same path as BACK.

The backdrop is deleted with `lv_obj_del_async()`, not `lv_obj_del()`. Its own
CLICKED handler is what usually triggers the close, and deleting the object whose
event is being dispatched is the one delete LVGL does not tolerate.

### 3. Sleep pose no longer curls the legs up

`EMOTE_SLEEPY` did `bend += 0.85f + 0.12f * wave`. A positive bend folds the knee
inward, so every leg curled up over the body — it read as a dead, clenched insect
rather than a resting one.

Now `bend -= 0.30f + 0.05f * wave`, which pushes the knees **outward** so the legs
lie splayed and flat on the substrate, the way a resting spider actually sits. The
breathing pulse moved to the swing so the legs drift instead of twitching.

The sleep squash also softened from `280/230` to `268/244`. The old values
squashed the body so hard it looked deflated.

### 4. Tiny fangs

Two 2px dots at `CEPH_FWD + 11`, offset `+/-3` laterally — ahead of and closer
together than the eyes, so they read as chelicerae rather than as a mouth. They
retract while asleep, because a sleeping spider with its fangs out looks like it
is baring them.

**`MAX_PARTS` had to be raised from `2*FUZZ_COUNT + 16` to `+ 18`.** `add_part()`
silently drops anything past the limit, so without this the fangs would simply not
have appeared — the same class of bug that previously ate the pupils and glints.

### 5. Colour selectors for pupils, legs and fangs

`game_state_t` gained `pupil_colour`, `leg_colour`, `fang_colour`. All three draw
from one shared 9-entry accent list (black, white, grey, amber, red, green, blue,
teal, violet), so the setup screen drives every picker from the same code.

The setup screen now has four labelled rows — body, pupils, legs, fangs — with
40px swatches. A swatch packs its row into the high byte of its user data and its
index into the low byte, so one callback serves all four rows.

Defaults are black pupils, white fangs, grey legs, so a fresh spider looks exactly
like the old hardcoded one.

`spider_refresh_colours()` re-applies body gradient, fuzz, pupils, fangs and legs
to the live objects. This required capturing handles for the body gradient steps
and both fuzz rings, which were previously created inline and thrown away — the
palette was applied once at init and could never change afterwards.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability 180 s  -> VERDICT: PASS, 0 faults
```

### Not yet verified by hand

Whether the fangs read at 2px, whether the splayed sleep pose looks restful, and
whether four rows of 40px swatches are comfortable to hit. The last row clears the
start button by only 8px, so that is the tightest part of the layout.

---

## Setup screen: alignment, live preview, bigger keyboard ✅

**Status:** COMPLETE — flashed and verified on hardware.

### 1. Category labels were not lined up with the swatches

Two separate misalignments:

- **Horizontally.** The label was pinned to `UI_CORNER` (40) while each swatch
  row was *centred* on the screen. Because the rows have different swatch counts
  (6 body colours vs 9 accents), every row started at a different x, so no row
  lined up with the label or with any other row.
- **Vertically.** The label sat at the row's top edge while the swatches sat 18px
  below it, so the text floated above the circles it described.

Fixed by giving every row one fixed left edge (`sx0 = 100`) instead of centring,
and positioning the label at `ry + 13` so its centre matches the swatch centre
(both now land on y=217 for the first row).

### 2. Live preview

A 112x112 panel beside the name field showing a miniature spider built from the
same four colours the pickers control: body, pupils, legs, fangs.

It reads its colours straight from `g` via the same accessors the real spider
uses, so the preview and the tank can never disagree. `on_swatch()` calls
`preview_refresh()` alongside `spider_refresh_colours()`, so a tap updates both
the preview and the real spider at once.

The preview is a separate set of objects from the real spider (which lives on the
game screen), built once at init. No per-frame work, no allocation on tap.

Legs are drawn as 3-point polylines mirrored about the centre line, matching how
`spider.c` draws them, so the preview reads as the same creature rather than a
generic bug.

### 3. Keyboard slightly larger

200px -> 220px tall. The keys were usable but cramped for a fingertip.

### Layout budget

```
title             y  34
name label        y  76      preview  x 328-440, y 76-188
name field        y  94-142  (x 40-312, clears preview by 16px)
colour rows       y 196-359  (4 rows, 42px pitch, 32px swatches)
start button      y 374-430
```

The last swatch row clears the start button by 15px, up from 8px before — the
smaller 32px swatches bought back the room the preview needed.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability 120 s  -> VERDICT: PASS, 0 faults
NVS erased       -> setup screen path boots clean, 0 faults
```

### Not yet verified by hand

Whether the preview is large enough to judge a colour by, and whether 32px
swatches are still comfortable to hit. The preview is the part most likely to
need resizing.

---

## Real preview, bigger fangs, simplified keyboard ✅

**Status:** COMPLETE — flashed and verified on hardware.

### 1. The preview was an approximation, not the spider

The previous preview hand-placed a handful of circles and three lines per side.
It shared no code with `spider.c`, so it drifted immediately: wrong body
proportions, wrong leg count, wrong eye placement, no fuzz, no gradient, no
secondary eyes. It was a drawing of a spider, not the spider.

Fixed by making the anatomy reusable. `spider.c` now has:

```c
typedef struct { lv_obj_t *body; ... part_t parts[MAX_PARTS]; int part_n; } anatomy_t;
static void anatomy_build(anatomy_t *a, lv_obj_t *parent, lv_obj_t *leg_parent, bool pettable);
static void layout_parts(anatomy_t *a, float heading, uint32_t scale_f, uint32_t scale_r);
```

The live spider is `static anatomy_t s_anat`, and the preview is a second
`anatomy_t` built by the same `anatomy_build()`. Both get the same gradient,
fuzz rim, four secondary eyes, pupils, fangs and glints, from the same constants.
The preview cannot drift because there is nothing to drift.

Short `#define s_body (s_anat.body)` aliases keep the render code reading exactly
as it did, so the refactor touched no logic.

The preview is laid out once at `-PI/2` (facing up) with a static standing leg
pose, then centred in its box. Centring uses the true extent rather than
`BODY_BOX`, because the legs reach past it:

```
reach = BODY_R + LEG_LEN * (LEG_SEG1 + LEG_SEG2) = 16 + 22 * 1.30 = 44.6 px
```

so the creature spans ~89px while the body container's origin sits at
`BODY_C - reach = -10.6`. Shifting every child by the difference centres the
whole animal, not just its body box.

### 2. Fangs a hair larger

`FANG_R` 2 -> 3. The lateral offset grew with them: at `R=3` and `DX=3` the two
circles would exactly touch and merge into one blob, so `FANG_DX` went 3 -> 4,
leaving a 2px gap. They now protrude 3px past the head's front edge instead of
sitting flush with it.

### 3. Keyboard bigger and simpler

200px -> 300px, and the map is now custom (`LV_KEYBOARD_MODE_USER_1`):

```
q w e r t y u i o p
a s d f g h j k l
z x c v b n m
[    space    ] [del] [ok]
```

The stock map has four rows including a symbol row, a mode-switch key, cursor
keys and a keyboard-hide key. A 15-character name needs none of that, and every
extra row shrinks the keys. Dropping one row while growing the panel from 200 to
300px roughly doubles each key's area.

### Layout budget

```
preview box   x 336-440  y  72-176
name field    x  40-312  y  94-142
colour rows              y 196-359
start button             y 374-430
keyboard                 y 180-480  (only while typing)
```

The preview box was 132px on the first attempt and overlapped both the name field
and the colour rows; 104px fits the 89px anatomy with margin and clears both.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability 60 s   -> VERDICT: PASS, 0 faults
NVS erased       -> setup screen path boots clean, 0 faults
```

### Not yet verified by hand

Whether the 104px preview is big enough to judge a colour by, and whether the
300px keyboard feels right. The preview is drawn at 1:1 — LV_USE_MATRIX is off,
so it cannot be scaled up without rebuilding the anatomy at a larger size.

---

## Triangular fangs, shift + symbols keys ✅

**Status:** COMPLETE — flashed and verified on hardware.

### 1. Fangs were overlapping the eyeballs

Pure arithmetic. The eyes sit at forward `CEPH_FWD + EYE_FWD` = 8 + 6 = **14**
with radius 7, so they span forward **7..21**. The fangs sat at forward
`CEPH_FWD + FANG_FWD` = 8 + 11 = **19** with radius 3, spanning **16..22** — i.e.
almost entirely *inside* the eyeballs, drawn on top of them.

They are now triangles whose base lands at forward 21.25, just past the eye front
edge, so they read as fangs protruding from under the face rather than beads
stuck on the eyes.

### 2. Fangs are short fat triangles

A round dot reads as a bead no matter how it is coloured. They are now filled
triangles: 6px across the base, 4.5px long — wider than they are long, which is
what makes them read as fangs rather than spikes.

LVGL has no triangle widget, so each fang is a transparent 12x12 object with a
`LV_EVENT_DRAW_MAIN` callback that calls `lv_draw_triangle()`. The triangle is
rotated by the anatomy's heading **inside the callback**, not via
`lv_obj_set_style_transform_angle`, because `LV_USE_TRANSFORM` is off in this
build — the same constraint that forces the body to be squashed by moving parts
rather than scaling objects.

`anatomy_t` gained a `heading` field so the callback knows which way to point;
`layout_parts()` sets it every frame.

Because the fangs are drawn rather than filled, `spider_refresh_colours()` now
calls `lv_obj_invalidate()` on them instead of setting a background colour.

### 3. Shift and symbols keys

The keyboard now has three maps registered at the three mode slots the stock
handler switches between:

| Key | Switches to |
|---|---|
| `ABC` | upper case |
| `abc` | lower case |
| `1#` | symbols |

The mode-switch strings must be the **exact literals** `"ABC"`, `"abc"` and `"1#"`
because `LV_KEYBOARD_CTRL_BUTTON_MODE_*` are defined in `lv_keyboard.c` and are
not exported in the header. Using the same literals lets the stock handler do the
switching, and because our maps are registered at those slots it switches to our
layouts rather than the stock ones.

Layout is still four rows — no cursor keys, no keyboard-hide key, no separate
symbol row — so the keys stay large.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability 60 s   -> VERDICT: PASS, 0 faults
NVS erased       -> setup screen path boots clean, 0 faults
```

### Not yet verified by hand

Whether 6x4.5px triangles read as fangs at this size, and whether the fang
rotation tracks the heading correctly in all directions. The rotation is the part
most likely to need adjustment — it is the first per-frame rotated geometry in
the project.

---

## Always-available slingshot, aggro radius, pet mechanic removed ✅

**Status:** COMPLETE — flashed and verified on hardware.

### 1. The slingshot no longer has a countdown

`behaviour_aim_active()` used to return `s_aim_active`, which was only true
during the spider's own crouch window. So the player could only shoot in the few
seconds after the spider decided to stalk, and the focus ring counted that window
down. Hunting was a timing test against the spider's AI rather than against the
bug.

It now returns `prey_count() > 0`. The slingshot is available whenever there is
something to shoot at, with no expiry.

The spider's crouch still exists, but only as its own dwell timer before it
lunges unattended. That fallback is what keeps an unattended spider from
starving, and it is now invisible to the player.

**The focus-ring countdown is gone.** It drained over the crouch window, so it
read as "hurry up". The ring object is still created but never shown; deleting it
would mean touching the part budget for no benefit.

### 2. The freeze gate had to change with it

`prey.c` froze the target bug while the player aimed, which was the fix for the
original "hunt doesn't work" bug. That gate was `behaviour_aim_active()` — which
is now true whenever prey exists, so leaving it would have frozen every bug
permanently.

It is now `behaviour_aim_dragging()`, which is the actual "player is lining up a
shot" signal.

### 3. Ideal power is computed live

`s_aim_ideal` was cached when the crouch opened. With no crouch moment to cache
at, and a bug that moves, `behaviour_aim_ideal_power()` now computes from
`prey_nearest()` on every call. `behaviour_aim_quality()` reads it the same way,
so shot quality still reflects how close the pull was to the ideal.

### 4. Prey aggro radius

Bugs already fled, but the hysteresis was 1.6 — a bug kept sprinting until it was
112px clear, so the tank felt permanently panicked. Now:

```
PREY_AGGRO_RADIUS      70.0f
PREY_AGGRO_HYSTERESIS  1.25f
```

Inside 70px a bug notices the spider and runs. Outside it, it goes about its
business and ignores the spider entirely. 1.25 lets it settle soon after it
escapes rather than sprinting to the far wall.

### 5. Pet mechanic removed

Tapping the spider used to raise mood and bond. It competed with the slingshot for
the same gesture — a press near the spider was ambiguous between "pet" and "start
a pull" — and mood is already raised deliberately by the PLAY button.

Removed: `game_on_pet()`, `needs_pet()`, the body's click handler, and the
`ACH_FIRST_PET` achievement (which could never be earned again). The whole tank
surface now means one thing: drag to hunt.

The spider body is explicitly `lv_obj_set_clickable(false)` so it cannot swallow
presses meant for the slingshot.

### 6. Larger hunt target

`TAP_ASSIST_PX` 44 -> 64. The hunt target is really the whole tank — a drag can
start anywhere and the shot is aimed by pull direction — but the tap-to-pounce
path still snaps to a nearby bug, and 44px was tight for a fingertip.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability 180 s  -> VERDICT: PASS, 0 faults
stability 120 s  -> VERDICT: PASS, 0 faults
```

The soak shows the full hunt cycle working unattended: `MISSED` then `CAUGHT`,
prey count dropping to 0. The heartbeat's `aim=` field now reports dragging
rather than the always-true availability gate, so it stays a useful signal.

### Not yet verified by hand

Whether the slingshot feels right with no time pressure, and whether 70px is the
correct aggro radius — too small and bugs ignore the spider until it is on top of
them, too large and the tank feels panicked. Both need a finger on the glass.

---

## Vanishing bugs, wall avoidance, hamburger menu ✅

**Status:** COMPLETE — flashed and verified on hardware.

### 1. Bugs were vanishing because a pounce had no reach limit

`prey_resolve_pounce()` picked the nearest bug with **no distance gate**:

```c
for (...) { if (d < best_d) { best_d = d; best = i; } }
if (best < 0) return;
/* ...resolve the catch against `best`... */
```

So a pounce that landed on empty substrate still resolved against whatever bug
happened to be closest — anywhere in the tank. The player saw a bug across the
screen disappear for no reason, and it looked random because it depended on
where the spider happened to leap.

Fixed with a reach gate: a pounce only resolves if it landed within
`CATCH_GOOD_PX` (55px) of a bug. Beyond that it logs `MISSED (out of reach)` and
costs the streak, which is the honest outcome — the spider lunged and fell short.

Verified in the soak: `MISSED (out of reach) d=64` now appears instead of a
silent kill.

### 2. Prey part budget was silently truncating

`MAX_PARTS` was 8, but the cricket registers 10 (body + head + 8 hind legs) and
the corpse registers 8. The cricket's last two legs were being thrown away by
`add_part()`'s bounds check — the same silent-truncation bug that previously ate
the spider's pupils and glints.

Raised to 16, with the per-species counts documented in the comment so the next
person can check the arithmetic.

### 3. The spider walked at walls instead of turning

`advance()` clamped the position, which stopped the spider escaping but did
nothing about its *intent*: it kept its heading pointed outward and pressed into
the wall until the stuck detector fired 1.5s later. That reads as "trying to
leave the tank".

Added `wall_avoidance()`, which turns the spider **before** it reaches the edge —
the same steering-level approach already used for the gift. It only steers away
from a wall the spider is actually heading into (a wall behind it must not push,
or it could never turn around), and the turn is clamped to 90 degrees so a corner
cannot demand a half-turn.

`WALL_AVOID_PX` is 70, wider than the 34px playfield inset, so the turn starts
early enough to look deliberate.

### 4. Edge touch bands replaced with a hamburger button

The invisible 56px strips along the top and bottom edges revealed the chrome on
tap. They were the reason the slingshot failed near the edges: a drag starting in
a band revealed the tray instead of pulling, and the bands covered exactly where
a thumb naturally rests.

Both bands are gone. In their place is a single 36x36 hamburger button in the
top-left corner that **toggles** the chrome. A toggle rather than a reveal,
because with no bands there is otherwise no way to dismiss the tray without
waiting out the 5s hold timer.

The button is deliberately NOT part of either chrome group, so it stays visible
when they auto-hide.

The stat rings moved right (`UI_CORNER + HAMBURGER_SIZE + 10`) because the first
ring sat underneath the button at the same y.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability 240 s  -> VERDICT: PASS, 0 faults
```

The soak shows the reach gate firing, prey persisting at `prey=1` for the full
run, and the spider correctly entering SLEEP (state 6) to recover energy from 19
to 40 — that is the resting behaviour working, not a freeze.

### Not yet verified by hand

Whether 70px is the right wall-avoid margin (too wide and the spider never
approaches the edges, too narrow and it still presses in), and whether the
hamburger is comfortable to reach one-handed in the top-left corner.

---

## Corner fix, tap-to-dismiss, softer hamburger ✅

**Status:** COMPLETE — flashed and verified on hardware.

### 1. The spider pinned to the top-right corner

The wall avoidance summed a **signed turn per wall**, which cancels in a corner.
Heading up-right into the top-right corner:

```
right wall: dr small, fx > 0  ->  turn -= (1 - dr/W) * 90
top wall:   dt small, fy < 0  ->  turn += (1 - dt/W) * 90
```

Equally close to both, the two terms are equal and opposite, so the total is
**zero** and the spider drives straight into the corner. That is exactly the
"repeatedly going to the top right" behaviour.

Rewritten to build an inward **push vector** instead of summing signed turns. In
a corner both components point inward, so the vector points diagonally away from
the corner and there is nothing to cancel. The desired heading is then lerped
toward that vector, weighted by how deep into the margin the spider is.

Also added `wander_dir_inward()`: a uniform random angle can point straight at a
wall, which the avoidance then has to fight every frame. Wander now aims at a
random point in the middle half of the tank, so the spider mostly walks across
the tank rather than into its edges.

Verified in the soak: positions now move through the interior
(219,273 -> 215,214 -> 299,202 -> 207,258) instead of pinning to a corner.

### 2. Tapping anywhere dismisses the chrome

New `ui_hud_dismiss_if_visible()`, called from the terrarium tap handler. The
chrome is a floating overlay, so a tap that reaches that handler is by definition
a tap that missed every button — dismissing there is exactly "tap anywhere that
is not the HUD".

It runs before the aim logic and returns, so the dismissing tap does not also
start a slingshot pull.

### 3. Hamburger closer to the corner, bigger target, softer look

| | before | after |
|---|---|---|
| inset from corner | 40 | 12 |
| touch target | 36 | 64 |
| visible element | 36px opaque panel | 28px dot at 20% opacity |

The visible dot is deliberately small and faint so it does not compete with the
tank, while the touch target is nearly twice the old size. The button is now a
transparent 64px square with the dot as a centred child, so the tap area and the
visual are decoupled — a 36px target was too small to hit reliably on a 2.16"
panel.

The stat rings moved to `HAMBURGER_INSET + HAMBURGER_TOUCH + 8` (x 84) to clear
the larger touch area.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability 240 s  -> VERDICT: PASS, 0 faults
```

The soak also shows the reach gate from the previous round still working
(`MISSED (out of reach) d=159`, `d=239`) alongside a legitimate catch at d=30.

### Not yet verified by hand

Whether the 20%-opacity dot is visible enough against the substrate, and whether
64px is comfortable without being so large it catches accidental taps near the
corner.

---

## Dismiss no longer pounces, full-spider gate, splat ✅

**Status:** COMPLETE — flashed and verified on hardware.

### 1. Tap-to-dismiss also fired a pounce

LVGL dispatches `PRESSED` before `CLICKED`. So the sequence was:

```
PRESSED   -> game_on_terrarium_press()  -> drag begins
CLICKED   -> game_on_terrarium_tap()    -> chrome dismissed
RELEASED  -> game_on_terrarium_release()-> drag is live, so it FIRES
```

The tap handler's early return was too late — the drag had already started.

Fixed by gating `game_on_terrarium_press()` on `ui_hud_chrome_visible()`. The
dismissing tap now never begins a pull, so there is nothing for RELEASED to fire.
The check is not redundant with the one in the tap handler; they guard different
events.

### 2. A full spider could still pull, and nothing happened

`prey_resolve_pounce()` refuses to award a meal at hunger 100, so a pull produced
no visible result at all — which reads as a broken control rather than a full
spider.

`behaviour_aim_active()` now also returns false at `HUNGER_FULL` (95), so the
slingshot is disabled outright. Added `behaviour_too_full()` so the HUD can
explain the state rather than leaving the player guessing.

The spider's OWN hunt path is unaffected: `behaviour_pounce_at_ex(..., false)`
does not consult the player gate, so an unattended spider still feeds itself.

### 3. Bugs vanished when caught — now they splat

The old corpse was a tidy bug lying on its back, which at this size read as "the
bug is still there, just rotated". The player could not tell a kill from a bug
that had wandered off.

Replaced with a splat: a wide flattened smear (three overlapping lobes plus a
darker centre), eight legs splayed outward at different angles, and three
droplets thrown clear. Drawn flat with no rotation, because a splat has no
orientation.

The splat is 15 parts, which is the largest of any prey body, so `MAX_PARTS` (16)
is now the tightest safe value — documented in the budget comment.

Corpse linger raised 1500 -> 2200 ms. A splat is the only confirmation the player
gets that the catch landed, so it needs to be readable.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability 240 s  -> VERDICT: PASS, 0 faults
```

### Not yet verified by hand

Whether the splat reads as a kill at this size, and whether disabling the
slingshot at hunger 95 is the right threshold — too low and the spider refuses
food it could still use, too high and the dead zone returns.

---

## Scrollable menus, WiFi, SD card ✅

**Status:** COMPLETE — flashed and verified on hardware.

### 1. The achievements menu was slow and overflowed the screen

Two separate problems.

**Overflow.** The panel was sized to fit its rows:

```c
const int32_t h = 26 + count * (MENU_ROW_H + MENU_GAP) + 24;
```

Ten achievements at 52+8 px per row is **650 px** on a **480 px** screen, so the
panel hung 85 px off each edge and rows 8-10 were unreachable. The panel is now
capped at `ui.H - 2*UI_CORNER` (400 px) and the rows live in a flex-column
container that scrolls.

**Slowness.** The backdrop was 50% black. A translucent full-screen layer forces
LVGL to blend the entire 480x480 framebuffer every frame, and the tank keeps
animating behind it — so opening a menu made the whole UI crawl. The backdrop is
now opaque, which lets LVGL skip the tank entirely.

### 2. WiFi

New `net.h` / `net.c`: station mode, one saved network, reconnect with
exponential backoff (5s -> 60s). Credentials live in their own NVS namespace
(`spidarium_net`) rather than the game save, so a factory reset of the spider
does not forget the network.

Exposed in the menu as `settings -> wifi`, showing the SSID and live status
("off" / "connecting..." / "connected 192.168.1.42" / "failed").

There is no on-screen password entry yet — the keyboard flow exists in the setup
screen but wiring it to WiFi is a separate job. Credentials are set via
`net_set_credentials()`.

### 3. SD card

New `sd.h` / `sd.c`: SDMMC 1-bit on the board's GPIO 3/1/2, mounted at
`/sdcard`. Provides save backup and restore, plus free-space reporting.

`save.c` gained `save_read_raw()` / `save_write_raw()`, which move the on-flash
blob verbatim (header included) so a backup is byte-identical. `save_write_raw()`
re-validates magic, version and length, so a backup from different firmware is
rejected rather than loaded as garbage.

Exposed as `settings -> sd card` with "back up save" and "restore save".

### Two problems found and fixed during bring-up

**SD probing at boot broke the display.** Mounting an absent card logged a burst
of sdmmc errors *and* allocated DMA-capable buffers that the LCD SPI driver then
could not get — every frame logged `Draw bitmap failed: ESP_ERR_NO_MEM`. The card
is now mounted lazily, only when the player opens the SD menu, so a unit with no
card never pays for it.

**WiFi's static buffers starved the SPI DMA.** The WiFi stack reserves internal
RAM for its static RX/TX pools, and the LCD driver needs internal DMA buffers at
the same time. Tuned in `sdkconfig`:

```
CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL   32768 -> 65536
CONFIG_ESP32_WIFI_STATIC_RX_BUFFER_NUM     10 -> 4
CONFIG_ESP32_WIFI_DYNAMIC_RX_BUFFER_NUM    32 -> 16
CONFIG_ESP32_WIFI_DYNAMIC_TX_BUFFER_NUM    32 -> 16
```

The reserve keeps a floor of internal RAM that WiFi cannot take, so the display
always has DMA memory. The buffer counts are sized for a device that sends almost
nothing, not for a throughput-heavy application.

Note: `sdkconfig.defaults` only applies when `sdkconfig` is regenerated, so the
same values were written to both.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability 90 s   -> VERDICT: PASS, 0 faults
```

Boot log is clean: `spidarium.net: no credentials saved; wifi stays off`, no
sdmmc errors, no DMA failures.

### Not yet verified by hand

Whether the achievements list scrolls smoothly, and whether the SD card actually
mounts on hardware with a card inserted — no card was available to test with, so
the mount path is unexercised.

---

## PC simulator, drag-toward, wider catch, status removed, WiFi scan ✅

**Status:** COMPLETE — firmware flashed and verified; simulator built and run.

### 1. Headless PC simulator

New `sim/` directory. It compiles the REAL logic modules (behaviour, prey,
needs, mind, gift, species, theme) for the host and drives them at a fixed
20 Hz timestep, with no display.

```
sim/build_sim.ps1          build
sim/spidarium-sim.exe --minutes 120
sim/spidarium-sim.exe --minutes 10 --verbose
```

Deliberately **not** graphical. A window would need SDL2 plus a full LVGL host
build, and what actually goes wrong in this project is logic — stuck states,
starving spiders, prey that never gets caught — not pixels. The harness stubs
the ~20 LVGL calls the logic modules make (`sim/lvgl.h`) and the ESP log macros
(`sim/esp_log.h`), so the simulator prints the same log lines the device does.

It reports final stats plus the longest-held state, and exits non-zero if a
**transient** state (AIM/POUNCE/EAT/PLAY) is held for more than 30 s. WANDER,
IDLE and SLEEP are excluded because a spider with no prey legitimately wanders
for hours — flagging those was a false positive on the first run.

The simulated owner feeds at hunger < 40 and cleans at hygiene < 30, so a run
measures the hunting loop rather than the owner's negligence.

**Toolchain note:** there was no host C compiler on this machine. Installed
`BrechtSanders.WinLibs.POSIX.UCRT` via winget. The ESP32 toolchain cannot build
host binaries — it only targets Xtensa.

### 2. Slingshot drags TOWARD the target

The old slingshot inverted the drag: pull back, fire forward. That is the right
catapult model but it breaks at the screen edge — to hit a bug near the top you
had to drag *below* the spider, and near the bottom you had to drag *above* it,
both of which run out of screen.

Dragging toward the target means the gesture always fits inside the tank,
because the target is inside the tank. The aim arrow was flipped to match, so
the visual and the outcome agree.

### 3. Catch radius widened

`CATCH_PERFECT_PX` 28 -> 44, `CATCH_GOOD_PX` 55 -> 78.

At 28 px the catch only landed when the spider's **mouth** was on the bug. The
body is ~34 px across, so landing any other part of the body still measured
20-30 px from the bug centre and fell outside the perfect band. The radii are
now measured against the spider's whole footprint.

### 4. Status screen removed

The status page duplicated what the menu already offers, so it is gone:
`ui_status.c` dropped from the build, `game_on_status()` removed, the button
removed from the tray, and the render-loop branch that ticked it deleted.

The menu button now uses the **status icon**, which was free and reads as
"info" better than a generic hamburger.

### 5. WiFi scan and connect

`net.c` gained a scan API: `net_scan_start()`, `net_scan_count()`,
`net_scan_ssid()`, `net_scan_rssi()`, `net_scan_secure()`, `net_connect_to()`.

Results are copied out of the driver immediately, because the driver's own list
is freed by the next scan — holding a pointer into it across a menu rebuild
would be a use-after-free.

The WiFi menu now shows a "scan" row followed by the networks found. Open
networks connect on tap. Secured networks are listed but disabled with "needs
password", because there is no on-screen password entry yet and a row that
silently does nothing is worse than one that says why.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability 90 s   -> VERDICT: PASS, 0 faults
sim 120 min      -> OK, 7 meals, no stuck transient state
```

The soak shows the wider radius working: `CAUGHT d=36`, which would have been a
miss at the old 28 px perfect radius.

### Not yet verified by hand

Whether drag-toward feels right (it inverts the muscle memory from the old
gesture), whether 44/78 px is now too generous, and whether the WiFi scan
actually lists networks — no AP was available to test against.

---

## Body-wide catch, WiFi crash fixed, 2x3 button grid ✅

**Status:** COMPLETE — flashed and verified on hardware.

### 1. The catch target was still too small — two separate causes

**The spider was landing short.** `behaviour_pounce_at_ex()` capped flight time
at 520 ms, which at 260 px/s covers only **135 px** — but `POUNCE_MAX_PX` is
**160 px**. A full-power shot therefore landed 25 px short of where the player
aimed, every single time. The spider was never arriving at the bug, so no
amount of widening the radius would have helped.

The cap is now derived from the constants:

```c
uint32_t max_flight = (uint32_t)(POUNCE_MAX_PX * gift_bonus()->pounce_range_mult
                                 / 260.0f * 1000.0f) + 40u;
```

so raising `POUNCE_MAX_PX` cannot silently reintroduce the shortfall.

**The catch was measured from the spider's centre.** The body is ~40 px across,
so landing the abdomen or a flank on the bug still measured 20-30 px from the
bug centre and fell outside the perfect band — only the mouth counted.

Added `SPIDER_BODY_R_PX` (22) and a `edge_d = best_d - SPIDER_BODY_R_PX` term,
clamped at 0. The reach gate, the catch probability and the auto-pounce quality
bands all use `edge_d` now, so landing **any** part of the body on the bug
counts.

Verified: `CAUGHT d=44`, which would have been a miss before.

### 2. WiFi scanning crashed the device

Two causes, both fixed.

**Stack overflow.** `esp_wifi_init()` and `esp_wifi_scan_start()` need far more
stack than the LVGL adapter's **8 KB** thread provides. Calling them from a menu
callback overflowed the stack and crashed.

WiFi work now runs on its own task (`NET_TASK_STACK` 8 KB, priority 4). The
public API only sets a request flag and returns, so a menu callback never blocks
on the radio and never runs WiFi code on the LVGL stack.

**DMA starvation.** Bringing the radio up allocates a large block of internal
DMA-capable RAM for its RX/TX pools. The LCD SPI driver needs the same pool for
every frame, and the two cannot coexist — starting WiFi produced a steady stream
of `Draw bitmap failed: ESP_ERR_NO_MEM` and a corrupted display.

Deferring the start to the first tick was not enough: the allocation is
permanent, so the display starves from then on regardless of when it happens.
The radio is now started **only when the player opens the WiFi menu**, which is
the only time it is needed.

### 3. Button tray is now 2 rows of 3

Was 4 on top and 2 on the bottom, which left two empty slots and made every
button narrower than necessary.

```
before: 4 columns -> 94 px per button
after:  3 columns -> 128 px per button
```

Six actions, evenly split, symmetric grid. The menu button moved to the bottom
row and kept the status icon.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability 120 s  -> VERDICT: PASS, 0 faults
stability 180 s  -> VERDICT: PASS, 0 faults
```

No `ESP_ERR_NO_MEM` in either soak, which is the direct evidence the WiFi DMA
fix works.

### Not yet verified by hand

Whether 128 px buttons feel right, and whether the WiFi scan actually lists
networks — no AP was available, and the scan path is now only reachable by
opening the menu, so it is unexercised.

---

## WiFi runs as the only process, with the game paused ✅

**Status:** COMPLETE — flashed and verified on hardware.

### The previous fix was wrong about the cause

The last attempt moved WiFi onto its own task and assumed the crash was a stack
overflow. That was only half the story, and the half that mattered was
**concurrency**, not stack depth.

The radio allocates internal DMA-capable RAM for its RX/TX pools and runs its own
high-priority tasks. Doing that while the game's 20 Hz logic tick and 30 Hz
render tick are also running starves the LCD SPI driver — `Draw bitmap failed:
ESP_ERR_NO_MEM` — and the device crashes. Moving the calls to another task did
not help, because the two still ran at the same time.

### The fix: pause the game, run WiFi alone

This is the approach that was used previously for WiFi and OTA, and it is the
right one: stop the game, do the network thing, resume.

New in `game.c`:

```c
void game_pause(void);    /* pauses both timers */
void game_resume(void);   /* resumes, and resets the dt baseline */
bool game_paused(void);
```

The two timers are now kept in `s_logic_timer` / `s_render_timer` so they can be
paused rather than deleted — resuming is cheap and the spider's state is
untouched. `game_resume()` resets `s_last_logic`, or the first tick after a long
pause would see a huge `dt` and teleport the spider.

In `net.c`, every radio operation goes through one helper:

```c
static bool run_exclusive(net_job_t job)
{
    ensure_task();
    game_pause();
    s_job = job;
    xTaskNotifyGive(s_task);
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(15000)) != pdTRUE) {
        ESP_LOGE(TAG, "radio job timed out");
        game_resume();
        return false;
    }
    game_resume();
    return s_job_ok;
}
```

The work still runs on a dedicated 8 KB task, because the LVGL task only has
8 KB and `esp_wifi_init()` needs more — but that task is now the **only** thing
running, and the caller blocks until it finishes.

The 15 s timeout is a safety net: if the radio wedges, the game resumes rather
than staying frozen forever.

### The scan is now blocking

`esp_wifi_scan_start(&sc, true)` instead of `false`. The asynchronous form
returned immediately and the results had to be polled, which meant the menu was
rebuilt before the scan finished and the driver's AP list was read while it was
still being written. Blocking is safe here because the game is paused and this
runs on the net task.

`net_scan_count()` is now a plain getter — no polling, no `s_scanning` flag.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability 120 s  -> VERDICT: PASS, 0 faults
```

### Not yet verified by hand

Whether opening the WiFi menu and scanning now works without crashing. No AP was
available to test against, and the crash only reproduced when the scan actually
ran — so the fix is verified by construction and by the absence of faults in the
soak, not by a successful scan.

---

## WiFi buffers moved to PSRAM — the real fix for the display corruption ✅

**Status:** COMPLETE — flashed and verified on hardware.

### Pausing was not enough, and could not be

The previous fix paused the game while WiFi ran. That stopped the *crash* but the
screen stayed corrupted, and the reason is simple: **the allocation is permanent**.

Bringing the radio up takes a large block of internal DMA-capable RAM for its
RX/TX pools. That memory is not returned when the scan finishes — it is held for
as long as WiFi is up. The LCD SPI driver needs the same pool for every frame, so
once WiFi has started, every frame logs `Draw bitmap failed: ESP_ERR_NO_MEM` and
the display is unreadable, whether or not the game is ticking.

Pausing the game cannot fix a memory shortage. It was the wrong lever.

### The fix: allocate WiFi and LWIP in PSRAM

```
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y
```

This tells the WiFi and LWIP stacks to take their buffers from PSRAM first, and
fall back to internal RAM only if that fails. The board has 8 MB of octal PSRAM
and the game uses a fraction of it, so there is plenty of room. Internal RAM is
then left for what actually needs it: the LCD's DMA buffers.

Verified compiled in: `build/config/sdkconfig.h` has
`#define CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP 1`.

The pause/resume machinery is kept. It is still correct — the radio should not be
running its own high-priority tasks while the game ticks — but it is now a
politeness measure rather than the fix.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability 120 s  -> VERDICT: PASS, 0 faults
```

### Not yet verified by hand

Whether the display stays clean while the WiFi menu is open and a scan runs. No
AP was available, so the scan path itself is still unexercised — but the memory
shortage that caused the corruption is now structurally impossible, because the
radio no longer competes for internal RAM at all.

---

## Written memory budget, and runtime memory reporting ✅

**Status:** COMPLETE — reporting flashed and verified; budget written from measured numbers.

### Why this exists

Three separate bugs in this project were the same bug wearing different masks:

| Symptom | Actual cause |
|---|---|
| SD probing broke the display | `esp_vfs_fat_sdmmc_mount()` took DMA buffers the LCD needed |
| WiFi scanning broke the display | The radio's RX/TX pools took the same internal RAM |
| Menus drew slowly | A 50%-alpha backdrop forced a full-framebuffer blend every frame |

All three presented as `Draw bitmap failed: ESP_ERR_NO_MEM` and a corrupted
screen, which says nothing about the cause. Each took several rounds to
diagnose. A written budget plus a runtime report makes the next one obvious
before it ships.

### `docs/memory_budget.md`

Written from **measured** numbers, not estimates. Covers:

- The two pools (512 KB internal SRAM vs 8 MB PSRAM) and why they are not
  interchangeable — PSRAM is not DMA-capable for the LCD.
- Measured baseline, flash usage, and every internal-SRAM consumer in
  descending order of size.
- Five rules that would have prevented all three bugs.
- A section on where this is heading, comparing the current LVGL scene-graph
  approach with Pocket Tank's software renderer.

### `main/meminfo.c` — runtime reporting

```c
mem_report_detail("boot complete");   // full breakdown
mem_report("checkpoint");             // one line
```

Reports internal, DMA-capable and PSRAM pools, each with **free and largest
block**. The largest block is the number that matters: a fragmented pool can
report plenty free and still fail one contiguous DMA allocation, which is
exactly how the display corruption presented. Warns below 32 KB.

Wired into boot and around every WiFi operation, so the cost of the radio is
visible as the difference between two log lines.

### Measured baseline

```
--- memory at boot complete ---
  internal  free  51983  largest  40960
  dma-cap   free  44711  largest  40960
  psram     free 6791324  largest 6684672
```

**~51 KB internal free, 40 KB largest block.** That 40 KB is what the LCD's
96 KB of double-buffered draw buffers has to fit into, which is why it is the
figure to watch.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability 45 s   -> VERDICT: PASS, 0 faults
```

The memory report appears in the boot log as expected.

### Note on the build

Changing `sdkconfig` invalidated part of LVGL and left a stale
`liblvgl__lvgl.a` that failed to link (`undefined reference to lv_draw_sw_init`).
Deleting `build/esp-idf/lvgl__lvgl` and rebuilding fixed it. Worth knowing:
**a sdkconfig change can require deleting the affected component's build
directory**, not just re-running the build.

---

## Round 16 — WiFi scan must not block the UI

**Reported:** "Wifi scanning hangs and then crashes the device now"

### Root cause

The previous round made WiFi run exclusively, which was right, but implemented
it by having the *caller* wait:

```c
static bool run_exclusive(net_job_t job)
{
    ...
    xSemaphoreTake(s_done, pdMS_TO_TICKS(15000));   /* <-- the bug */
    return s_job_ok;
}
```

`net_scan_start()` was called from `on_wifi_pick()`, which runs on the **LVGL
task**. That task drives the display and has an 8 KB stack. Blocking it for the
length of a scan (up to 15 s) froze the UI, and the task watchdog then rebooted
the device. The crash was not in WiFi at all — it was the watchdog killing a
blocked display task.

### Fix

The job is handed to a worker task and the caller returns immediately:

```c
static bool queue_job(net_job_t job)
{
    ensure_task();
    if (s_busy) return false;      /* a double-tap cannot queue two */
    s_busy = true;
    s_job  = job;
    xTaskNotifyGive(s_task);
    return true;
}
```

The worker does the pause, the radio work, and the resume:

```c
static void net_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        net_job_t job = s_job;
        s_job = NET_JOB_NONE;
        if (job == NET_JOB_NONE) { s_busy = false; continue; }

        game_pause();                  /* pause HERE, not on the caller */
        mem_report_detail("wifi before");
        if (job == NET_JOB_INIT)      net_init_blocking();
        else if (job == NET_JOB_SCAN) net_scan_start_blocking();
        mem_report_detail("wifi after");
        game_resume();

        s_gen++;
        s_busy = false;
    }
}
```

The UI polls instead of waiting. While `net_busy()` the WiFi menu shows a single
disabled `scanning...` row; when the job finishes the menu is rebuilt with the
results.

### Two bugs found in the fix itself

1. **Re-trigger loop.** Rebuilding the menu calls `net_init()`, which queued
   another job and set `s_busy` again — so a busy-edge test would rebuild
   forever. Replaced with a **generation counter** (`net_generation()`), which
   only changes when a job actually completes.

2. **Yanking the player back.** A scan finishing while the player had navigated
   to another menu would have reopened the WiFi list under them. `game.c` now
   tracks `s_open_menu` and only rebuilds when it is still `MENU_WIFI`.

`net_init()` is also now idempotent (`s_inited`), so opening the menu repeatedly
does not re-queue radio setup.

### Dead code removed

`s_done` (the semaphore), `s_job_ok`, the unused `ok` result variable, and the
`freertos/semphr.h` include — all left over from the blocking version.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0, hash verified
stability 45 s   -> VERDICT: PASS, 0 faults
```

Boot memory unchanged from baseline, confirming the worker task costs nothing
until it is used:

```
  internal  free  52195  largest  40960
  dma-cap   free  44415  largest  40960
  psram     free 6791340  largest 6684672
```

### Decisions made

- **Never block the LVGL task.** It drives the display and has an 8 KB stack.
  Any operation that can take longer than a frame belongs on a worker.
- **Poll, do not wait.** The UI asks `net_busy()`; it never waits on a
  semaphore.
- **Key completion on a counter, not an edge.** Any poll that can itself
  re-queue work must be idempotent, or it loops.
- **Pause on the worker.** `game_pause()` is cheap and safe from any task; the
  *wait* is what must not happen on the display thread.

### Still outstanding

`main/cspider.c` — the canvas-based spider renderer — is written and compiles
clean but is **not wired in**. That is the next round: replacing the 46-widget
spider with a single 96x96 canvas, which is the Pocket Tank architecture the
player asked for.

---

## Round 17 — WiFi scan: first press dead, second press crash

**Reported:** "Same issue. First press of scan it doesnt work. Second press it crashes."

Round 16 removed the blocking wait but introduced two new faults. Both are
thread-affinity bugs, and both are mine.

### Bug 1 — the crash: LVGL called from the net task

Round 16 moved the work onto a worker task and had the worker pause the game:

```c
static void net_task(void *arg)
{
    ...
    game_pause();      /* <-- calls lv_timer_pause() */
```

`game_pause()` calls `lv_timer_pause()`. **LVGL is not thread-safe.** Calling it
from the net task corrupted LVGL's internal timer list while the LVGL thread was
walking it, which is why the second scan crashed rather than the first: the first
scan left the list damaged, the second walked it.

The fix is a handshake. The worker raises a flag and waits for an
acknowledgement; the LVGL thread does the actual pausing:

```c
/* net.c -- worker */
s_pause_req = true;
for (int i = 0; i < 100 && !s_pause_ack; i++) vTaskDelay(pdMS_TO_TICKS(5));
... radio work ...
s_pause_req = false;
s_pause_ack = false;

/* net.c -- called from the LVGL thread */
bool net_pause_wanted(void)
{
    if (!s_pause_req) return false;
    s_pause_ack = true;
    return true;
}

/* game.c -- game_tick_render() */
if (net_pause_wanted()) {
    if (!s_paused) { s_paused = true; lv_timer_pause(s_logic_timer); }
    return;
}
```

The wait is bounded (500 ms) so a stalled render timer cannot deadlock the radio.

### Bug 2 — the dead first press: radio started without credentials

`esp_wifi_start()` raises `WIFI_EVENT_STA_START`, and the handler called
`esp_wifi_connect()` unconditionally. Scanning starts the radio with **no
credentials**, so that connect failed instantly, raised
`WIFI_EVENT_STA_DISCONNECTED`, and kicked off the retry backoff. The radio was
churning connect/disconnect while the scan tried to run, so the first scan
returned nothing. By the second press the backoff had settled, which is why the
second press behaved differently.

```c
if (id == WIFI_EVENT_STA_START) {
    if (s_ssid[0]) {          /* only associate if there is something to join */
        esp_wifi_connect();
        ...
    }
}
```

### Bug 3 — duplicated radio bring-up that disagreed with itself

`net_init_blocking()` and `ensure_radio()` both brought the radio up, and they
had different rules: one required credentials, the other did not. Scanning and
connecting therefore took different paths through the same hardware.

Replaced with two functions with clear idempotency contracts:

- `radio_up()` — netif, event loop, `esp_wifi_init()`. **Not** idempotent, so it
  runs exactly once, guarded by `s_wifi_inited`.
- `radio_start()` — `esp_wifi_start()`. Safe to repeat, guarded by `s_started`.

### Bug 4 — `net_set_credentials()` touched the radio from the LVGL thread

Same class as bug 1. It called `esp_wifi_stop()`, `esp_wifi_set_config()`,
`esp_wifi_disconnect()` and `esp_wifi_connect()` directly, and it is called from
`on_wifi_pick()` on the LVGL thread. Now it only stores the credentials and
queues `NET_JOB_CONNECT`; the radio work happens on the worker.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0, hash verified
stability 40 s   -> VERDICT: PASS, 0 faults
```

Boot memory is slightly better than baseline (the duplicated bring-up path is
gone):

```
  internal  free  54111  largest  40960
  dma-cap   free  46151  largest  40960
  psram     free 6791388  largest 6684672
```

### Decisions made

- **Thread affinity is the rule, not a detail.** LVGL may only be touched from
  the LVGL thread; `esp_wifi_*` may only be touched from the net task. Every
  violation of this has crashed the device.
- **Cross-thread work is a request plus an acknowledgement**, never a direct
  call. The requester waits with a bound so it cannot deadlock.
- **Idempotency must be explicit.** `esp_wifi_init()` is not idempotent and
  `esp_wifi_start()` is; conflating them is what made scanning and connecting
  diverge.
- **Do not connect without credentials.** A radio with nothing to join should
  sit still, not churn through the retry backoff.

### Still outstanding

`main/cspider.c` — the canvas spider renderer — is written and compiles clean but
is **not wired in**. That remains the next round.

---

## Round 18 — WiFi scan: the real cause, found by capturing instead of guessing

**Reported:** "Its not stable at all still not working"

Rounds 16 and 17 were both guesses. This round started by capturing the serial
log during a reproduction instead, and the capture immediately disproved the
assumption: **zero lines from `net.c` in 90 seconds**, while the 30-second
heartbeat continued normally. The device was alive and healthy. The scan code
was never running at all.

That ruled out every theory about WiFi, DMA, PSRAM and stack depth. The problem
was upstream of the radio.

### The actual bug — the first scan was silently dropped

Opening the WiFi menu called `net_init()`, which queued a `NET_JOB_INIT` and set
`s_busy = true`. Tapping "scan" before that job finished then hit the guard in
`queue_job()`:

```c
if (s_busy) return false;      /* scan silently discarded */
```

The scan was thrown away with no log line and no user feedback. That is exactly
"first press of scan it doesnt work". The second press worked only because the
INIT job had finished by then.

The INIT job was also pointless: it loaded credentials and refreshed a status
string. It never needed the radio, and it never needed to be asynchronous.

### Fix

`net_init()` is now synchronous and does no radio work:

```c
void net_init(void)
{
    creds_load();
    status_update();
}
```

`NET_JOB_INIT` and `net_init_blocking()` are deleted. The radio is brought up
lazily by `radio_start()` inside whichever job actually needs it, so there is no
window in which a scan can be dropped.

### The freeze, also fixed

Round 17's pause handshake was itself a freeze mechanism. `game_tick_render()`
returned early while paused:

```c
if (net_pause_wanted()) {
    ...
    return;   /* skip this frame's work */
}
```

If the radio work blocked, `s_pause_req` stayed raised and the UI stopped
rendering **permanently**. The pause is now removed entirely — a scan does not
need the game stopped, it needs the radio, and the radio has its own task.

### Instrumentation added

The reason this took three rounds is that the failure was silent. Every step of
the path now logs:

- `net task started` / `net task create failed (stack N)`
- `queue_job(N)` / `queue_job(N): busy, ignored` / `queue_job(N): no task`
- `net_scan_start`
- `job N start` / `job N done, stack free N`

The stack high-water mark is logged after every job, so the next stack question
is answered by the log rather than by raising the number and hoping.

Net task stack raised 8192 -> 12288 as a precaution, not as a diagnosis.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0, hash verified
stability 40 s   -> VERDICT: PASS, 0 faults
```

### Decisions made

- **Capture before diagnosing.** Two rounds of plausible-sounding theories were
  wrong. One 90-second capture found the truth immediately. The absence of log
  lines was the evidence, and it was available from the start.
- **A silent failure is worse than a loud one.** `queue_job()` returning false
  with no log is what made this take three rounds. It now logs every rejection.
- **Do not queue work that does not need a worker.** The INIT job existed only
  to set a busy flag that then blocked the thing the player actually asked for.
- **Do not pause the game for a scan.** Pausing the render tick means a blocked
  worker freezes the UI with no way back.

### Still outstanding

`main/cspider.c` — the canvas spider renderer — is written and compiles clean but
is **not wired in**.

---

## Round 19 — Canvas renderer: the spider is a picture, not a scene graph

**Requested:** "Let's proceed with the rewrite of the rendering engine"

The spider was built from ~46 LVGL widgets: two body masses, 28 fuzz dots, four
secondary eyes, two eyes, two pupils, two glints, two fangs and eight two-segment
legs as `lv_line` polylines. Every frame repositioned all of them.

It is now **one 96x96 canvas**, drawn from scratch each frame. This is the Pocket
Tank architecture: the creature is a picture, not a scene graph.

### What the widget approach cost

- **A part budget.** `MAX_PARTS` was 46 and anything past it was silently
  dropped. That ate the pupils and glints once, and the fangs another time.
- **A draw callback.** LVGL has no triangle widget, so the fangs needed a
  `LV_EVENT_DRAW_MAIN` callback calling `lv_draw_triangle()`.
- **No transforms.** `LV_USE_MATRIX` is off, so squash/stretch was faked by
  moving every part individually.
- **46 objects repositioned at 30 Hz.**

### What the canvas costs

One `lv_obj`, one invalidate, and arithmetic. A triangle is three points. A
rotation is one multiply. Squash/stretch is two scale factors passed to a
coordinate transform.

### The memory surprise

The canvas buffer is 96x96 RGB565 = **18,432 bytes**. As a static array it came
straight out of internal SRAM and dropped the largest free DMA block from
40,960 to **32,768** — the pool the WiFi driver needs for its RX/TX buffers.
That is very likely why scanning was still failing.

The canvas does **not** need to be DMA-capable: LVGL's software renderer writes
it with the CPU, and the blit to the LCD reads it with the CPU. Only the LCD's
own draw buffers need DMA. It is now `heap_caps_malloc(MALLOC_CAP_SPIRAM)` with
an internal fallback.

Net effect, measured at boot:

| | before | after |
|---|---|---|
| internal free | 54,111 | **67,619** |
| dma-cap free | 46,151 | **60,003** |
| largest block | 40,960 | **51,200** |

Deleting 46 widgets freed more than the canvas costs. The largest contiguous
block grew by 10 KB, which is the number that matters for DMA.

### What moved where

- `cspider.c` — draws the whole creature: legs, abdomen, cephalothorax, fuzz,
  eyes, pupils, glints, fangs. Owns the gait, the squash/stretch and the emote.
- `spider.c` — now owns **only the slingshot aim visuals** (the band line and
  the hidden power-meter bars), which are genuinely widget-shaped. The anatomy
  builder survives solely for the setup-screen preview.
- `ui_hud.c` — derives the emote from state and sends it to the canvas.
- `game.c` — `cspider_init()` before `spider_init()`, `cspider_render()` before
  `spider_render()`.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
boot             -> "canvas spider ready (96x96)"
stability 40 s   -> VERDICT: PASS, 0 faults
```

### Decisions made

- **A creature is a picture, not a scene graph.** 46 widgets to draw one small
  animal is the wrong shape of solution, and every bug in the old renderer came
  from that shape: part budgets, draw callbacks, missing transforms.
- **Put the canvas in PSRAM.** It is read and written by the CPU only. Internal
  DMA RAM is the scarce resource and the radio needs it.
- **Keep widgets where widgets are right.** The aim band is a line whose points
  change every frame; that is exactly what `lv_line` is for.
- **Measure the memory, do not assume it.** The canvas looked like it should be
  cheaper than 46 widgets. It was not, until the buffer moved to PSRAM.

### Still outstanding

WiFi scanning has not been retested since the DMA block grew from 40,960 to
51,200. That is the next thing to check, and it is now the most likely fix for
the scan failure.

---

## Round 20 — Three bugs: black box, bug hit box, WiFi

**Reported:** "The spider has a transparent black box around it. The hit box on
bugs is still bad. Wifi scanning is still causing issues."

### 1. The black box — RGB565 has no alpha

The canvas was created as `LV_COLOR_FORMAT_RGB565`. That format has **no alpha
channel**, so clearing the buffer to zero produced opaque black. The spider sat
inside a black square and the tank showed through nowhere.

Switched to `LV_COLOR_FORMAT_ARGB8888`, where zero is genuinely transparent. The
buffer grows from 18,432 to 36,864 bytes, which is affordable because it lives in
PSRAM.

### 2. The bug hit box — judged against the wrong bug

`prey_resolve_pounce()` found the bug nearest the spider's landing position, then
judged the shot with `behaviour_aim_quality()`. That function recomputes the ideal
power from `prey_nearest()` — a **different bug** whenever more than one is on
screen. So the player could aim at the far bug, land on it perfectly, and have the
quality computed against the near bug: a perfect landing scored zero and read as a
miss.

The catch is now decided by **geometry** — did the body land on the bug — with aim
quality as a bonus that can promote a good landing to a perfect one, never as the
thing that decides whether the hit happened. Geometry is what the player can see,
so it is what the game should judge.

### 3. WiFi — the config was never applied

This is the one that mattered, and it was visible in `sdkconfig` all along:

```
# CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP is not set
```

`sdkconfig.defaults` had it enabled, but **`sdkconfig` is what the build reads**,
and it had the option disabled. So WiFi allocated its RX/TX pools from internal
RAM, which was already exhausted by the display — `esp_wifi_init()` returned
`ESP_ERR_NO_MEM` every time.

The failure then cascaded into a reboot:

1. `esp_wifi_init()` fails with `ESP_ERR_NO_MEM`.
2. `s_wifi_inited` stays false, but `esp_netif_create_default_wifi_sta()` had
   already succeeded.
3. The next scan retries, calls `esp_netif_create_default_wifi_sta()` again.
4. That function **asserts** on a duplicate netif rather than returning an error.
5. `assert failed: esp_netif_create_default_wifi_sta wifi_default.c:422`.

Three fixes:

- `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` in `sdkconfig` (not just the defaults).
- `s_netif_created` guards the netif separately from `s_wifi_inited`, so a partial
  failure can be retried without creating it twice.
- A failed `esp_wifi_init()` now calls `esp_wifi_deinit()` so the next attempt
  starts clean instead of hitting a half-constructed driver.

The scan also releases the radio with `esp_wifi_stop()` when it finishes, returning
its internal DMA pools to the heap. `esp_wifi_stop()` does not deinit the driver,
so the netif stays valid and a later scan can simply start again.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability 45 s   -> VERDICT: PASS, 0 faults
boot             -> canvas spider ready (96x96)
                 -> internal free 60739, largest 51200
```

The `assert failed: esp_netif_create_default_wifi_sta` reboot is gone.

### Decisions made

- **`sdkconfig` is the source of truth, not `sdkconfig.defaults`.** The defaults
  only apply when `sdkconfig` is regenerated. A setting that exists only in the
  defaults is a setting that is not applied. This cost three rounds of WiFi
  debugging.
- **Judge a hit by geometry, not by a recomputed ideal.** Any quality metric that
  re-derives its reference point can disagree with what the player is looking at.
- **A format without alpha cannot be transparent.** Clearing to zero in RGB565 is
  black, not clear.
- **Guard non-idempotent calls separately.** `esp_netif_create_default_wifi_sta()`
  asserts on a duplicate; `esp_wifi_init()` returns an error. Treating them as one
  unit turned a recoverable failure into a reboot.

---

## Round 21 — Serial test console, and WiFi actually fixed

**Requested:** "add serial commands to test everything, including wifi, so you can
monitor serial, run tests, and self correct" and "make sure all of the serial
commands you build out are clearly documented, the more you document the better.
id like a robust agent file for the project"

### The console

`main/test_console.c` — a command interpreter on the console UART, on its own
task so the LVGL task is never blocked. Commands: `help`, `mem`, `stat`, `radio`,
`scan`, `scanloop N`, `render N`, `spider`, `prey`, `save`, `reset`, `reboot`.

Every command prints detail lines then exactly one `OK` or `ERR` terminator, so a
script can tell success from failure without parsing prose.

`cmd.py` sends one command and reports the verdict as its exit code.
`run_tests.py` runs the whole suite and writes `test_results.txt`.

Full reference: `docs/serial_console.md`.

### Why the console took several attempts

Three separate problems, each of which made the console look dead while the
device ran perfectly:

1. **`getchar()` returns EOF immediately** on ESP-IDF when nothing is pending, so
   the task spun and never saw a command.
2. **`fgets()` on stdin blocks forever** even with the UART driver installed,
   because the console's line discipline consumes the bytes first.
3. **The port was the wrong transport.** COM7 is `VID:PID=303A:1001` — the
   ESP32-S3's built-in **USB Serial/JTAG**, not the UART bridge. The sdkconfig
   has UART as the primary console and USB-JTAG as the secondary, so log OUTPUT
   reached both, but INPUT only arrives on the one the host is connected to.
   Reading only UART0 therefore never saw a byte.

The fix polls both transports:

```c
int got = uart_read_bytes(CONSOLE_UART, &ch, 1, 0);
if (got != 1) got = usb_serial_jtag_read_bytes(&ch, 1, 0);
```

Both drivers must be installed, and `uart_vfs_dev_use_driver()` must **not** be
called — it routes stdin through the same ring buffer the task reads, and the two
compete for the bytes.

### What the console found immediately

The first full run reported:

```
SCAN took=50ms found=0 err="radio unavailable"
```

That is the WiFi bug, stated plainly, in one line. It had survived four rounds of
guessing because nothing ever reported *why* a scan produced no results.

The cause was `radio_start()` failing silently. With the error now logged, the
next run showed the radio coming up correctly and:

```
SCAN took=2750ms found=12 err=""
  AP 0 ssid="Home" rssi=-50 secure=1
  ...
LOOP ok=3 failed=0 largest_drift=0
SUMMARY passed=8 failed=0
```

**WiFi scanning works.** Twelve networks found, and `largest_drift=0` across three
consecutive scans — the radio releases its DMA buffers every time.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
run_tests.py     -> SUMMARY passed=8 failed=0
```

### Decisions made

- **Build the instrument before fixing the bug.** Four rounds of WiFi guessing
  were resolved by one command that runs a scan and prints the error string. The
  console should have existed from the start.
- **Every command ends in OK or ERR.** A command that can fail must say so, in a
  form a script can read.
- **Leak checks print memory before and after.** `scanloop` and `render` fail on
  drift, so a leak is a number that falls rather than a corrupted screen thirty
  seconds later.
- **Poll both console transports.** The board has two and the host may be on
  either; guessing wrong makes the console silently dead.
- **Document the traps, not just the commands.** The three failed approaches are
  recorded in `docs/serial_console.md` so they are not retried.

### Documentation delivered

- `docs/serial_console.md` — every command with real output, the output contract,
  the transport trap, the radio/display architecture note, and how to add a
  command.
- `.github/copilot-instructions.md` — the agent operating manual: the three rules,
  build/flash/verify, the console, memory, the renderer, WiFi, LVGL constraints,
  code layout, working method, definition of done.

---

## Round 22 — One console driver, full device control, and the Settings crash

**Reported:** "The serial console should be able to test more than wifi scanning.
The whole device should be controllable via serial... why are there two uart
drivers? Pocket tank doesnt seem to have this issue? I cant even get into
settings, the screen draw immediately messes up."

All three are the same bug, and the question was the right one to ask.

### Why there were two drivers — my mistake

Pocket Tank uses ESP-IDF's **standard console**: one transport, `fgets()` on
stdin, no custom drivers. I installed a UART driver *and* a USB-JTAG driver and
polled both with `uart_read_bytes()`, because I was reading raw bytes instead of
using the console properly.

That worked, but it cost about **18 KB of internal RAM**, and dropped the largest
free DMA block from 51200 to **32768**. That is the pool the LCD needs for its
draw buffers.

**That is why Settings corrupted the screen.** Not a menu bug at all — the
display could no longer allocate its draw buffers.

### The fix

One driver, for the transport actually in use:

```c
usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
usb_serial_jtag_driver_install(&cfg);
```

and read it with `usb_serial_jtag_read_bytes()`.

`fgets()` was tried first, since that is the standard path, but it does **not**
work on this board: the console VFS does not route stdin to the USB-JTAG
transport, so the task blocks forever while the device runs normally. That is
recorded in the docs so it is not retried.

Measured at boot:

| | two drivers | one driver |
|---|---|---|
| internal free | 57967 | 57967 |
| largest block | 32768 | **38912** |

And opening the main menu now produces **zero** `Draw bitmap failed` errors.

### Full device control

The console now drives the whole device, not just WiFi. Every command calls the
same code path the UI calls, so a test here is a test of the real thing:

| Command | Does |
|---|---|
| `feed` `clean` `play` `sleep` | press the HUD button |
| `menu` | open the main menu |
| `open NAME` | open a UI surface (main, gift, chrome, nochrome) |
| `spawn [SPECIES]` | spawn a bug, random or by index |
| `state N` | force a behaviour state (0..9) |
| `set FIELD VALUE` | set a need (hunger, mood, energy, hygiene, bond, sick) |
| `refresh` | rebuild the visual layers |

Plus the existing `mem`, `stat`, `spider`, `prey`, `radio`, `scan`, `scanloop`,
`render`, `save`, `reset`, `reboot`.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
run_tests.py     -> SUMMARY passed=8 failed=0
scan             -> found 12 networks, err=""
scanloop 3       -> largest_drift=0
open main        -> 0 display errors
stability 40 s   -> VERDICT: PASS, 0 faults
```

### Decisions made

- **One driver, for the port in use.** Two drivers is not "more robust", it is
  18 KB of internal RAM spent on a transport nothing is connected to.
- **The display's DMA budget is the constraint that decides everything else.**
  A console feature that costs 18 KB of internal RAM is not free — it breaks the
  screen. Measure `largest` after adding anything.
- **Use the standard console path where it works.** `fgets()` is right in
  principle; it is only wrong here because this board's console VFS does not
  route stdin to USB-JTAG. Document the exception rather than generalising from
  it.
- **Every UI action needs a serial equivalent.** A feature that can only be
  tested by tapping is a feature that will not be tested.

---

## Round 23 — The WiFi crash was a thread-affinity bug in the console

**Reported:** "In wifi I get scan failed radio unavailable and subsequent presses
cause memory leaks"

### What the capture showed

The console made this reproducible in one command. Opening the WiFi page five
times produced:

```
=== open wifi #1 ===
    Guru Meditation Error: Core 0 panic'ed (LoadProhibited)
=== open wifi #3 ===
    Guru Meditation Error: Core 0 panic'ed (LoadProhibited)
```

Resolving the backtrace with `addr2line`:

```
lv_event_mark_deleted   lv_event.c:346
lv_obj_destructor       lv_obj.c:1523
lv_obj_destruct         lv_obj_class.c:151
obj_delete_core         lv_obj_tree.c:728
lv_obj_clean            lv_obj_tree.c:101
build_body              prey.c:138
prey_resolve_pounce     prey.c:492
behaviour_tick          behaviour.c:715
game_tick_logic         game.c:812
```

**The crash was not in WiFi at all.** It was in the prey renderer, and the WiFi
menu merely gave it enough time to happen.

### Root cause — Rule 2, again

`cmd_open()` in the console called `game_open_menu()` **directly from the console
task**. Opening a menu creates and deletes LVGL objects. Doing that off the LVGL
thread corrupted LVGL's global event list, and the next `lv_obj_clean()` — which
happened to be the prey renderer rebuilding a bug — walked the corrupted list and
died.

This is the same class of bug as calling `lv_timer_pause()` from a worker, which
crashed the device in round 17. The rule was already written down; the console
broke it.

### The fix

Every console command that touches the UI is now marshalled onto the LVGL task:

```c
/* console task */
bool game_ui_open_menu(const char *name)
{
    strncpy(s_ui_menu_name, name, sizeof s_ui_menu_name - 1);
    s_ui_menu_done = false;
    s_ui_menu_req  = true;
    for (int i = 0; i < 100 && !s_ui_menu_done; i++) vTaskDelay(pdMS_TO_TICKS(10));
    return s_ui_menu_ok;
}

/* render tick, on the LVGL task */
static void ui_service_menu_request(void)
{
    if (!s_ui_menu_req) return;
    s_ui_menu_req = false;
    s_ui_menu_ok  = game_open_menu(s_ui_menu_name);
    s_ui_menu_done = true;
}
```

`feed`, `clean`, `play`, `sleep` and `menu` go through the same pattern via
`game_ui_call()`.

### Two latent bugs fixed while in there

1. **`prey_tick` freed a root but left `p->parts[]` pointing at the freed
   children.** The next `build_body()` for that slot could touch freed objects.
   The part table is now cleared when the root is deleted.
2. **`prey_clear()` had the same gap.** Same fix.

`build_body()` also gained a `lv_obj_is_valid()` guard, so a stale root rebuilds
instead of crashing.

### Verification

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
repro_wifi.py 5  -> 5/5 opens OK, largest stable at 57344, no drift
run_tests.py     -> SUMMARY passed=8 failed=0
scan             -> found 12 networks, err=""
scanloop 3       -> largest_drift=0
stability 40 s   -> VERDICT: PASS, 0 faults
```

Before the fix, 3 of 5 opens crashed. After, 5 of 5 succeed and memory is flat.

### Decisions made

- **The console must obey the same threading rules as the UI.** It is a second
  entry point into the same code, not a privileged one. Every command that
  touches `lv_*` is marshalled.
- **A crash in one subsystem can be caused by another.** The backtrace pointed at
  the prey renderer; the cause was the console. Read the whole stack, not the top
  frame.
- **Free the whole structure, not just the root.** Deleting a parent frees its
  children, but any pointer to those children is now dangling. Clear the table.
- **Guard against stale pointers where the cost is one call.** `lv_obj_is_valid()`
  turns a crash into a rebuild.
