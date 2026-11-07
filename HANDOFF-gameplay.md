# Spidarium — Gameplay Refinement Brief

**From:** the implementing agent (builder)
**To:** the architect
**Date:** 2026-11-05
**Purpose:** hand back a working, verified build and describe honestly what is
and is not fun yet, so the next plan targets the real gaps.

---

## 1. What exists and is verified

All nine phases of the original plan are implemented, built, flashed and
running on hardware (ESP32-S3, COM7, 480×480 AMOLED).

```
build.ps1        -> exit 0, zero project warnings
flash.ps1 COM7   -> exit 0
stability_check  -> VERDICT: PASS, 0 faults
```

### Module map

| File | Lines | Responsibility |
|---|---|---|
| `game.c` | 381 | Orchestrator: owns `g`/`ui`/`g_play`, both timers, event callbacks, XP, achievements |
| `game_config.h` | ~175 | Every tunable. Single source of truth for feel. |
| `behaviour.c` | 373 | Steering, turn-rate limit, decor separation, 9-state FSM |
| `spider.c` | 479 | Procedural jointed legs, body anatomy, gait, hop arc |
| `prey.c` | 452 | Procedural bugs, wander/flee AI, catch resolution |
| `needs.c` | 247 | 4 stats, decay curves, mood, stages, molting, SICK |
| `terrarium.c` | 307 | Substrate, vignette, web, decor shapes, poop, dust |
| `ui_hud.c` | 249 | Arc stat rings, 6-button tray, bubbles |
| `ui_status.c` | 282 | Bars, stage/age/level/XP, achievements, mute |
| `audio.c` | 184 | Synthesized PCM SFX (no audio assets) |
| `save.c` | 148 | Versioned NVS persistence (magic/version/size) |
| `assets.c` / `sprites.c` | 104 / 175 | mmap QOI asset access, sprite table |

### Rendering note

The spider and prey are **procedural**, not sprites. The 67 generated sprites
still exist and are packaged (~45 KB) but nothing references them. This was a
deliberate reversal: LVGL 9.6 cannot mirror an image (`scale_x` is `uint32_t`,
`LV_USE_MATRIX` is off), so right-facing sprites always looked like they were
moving backwards. Procedural parts rotate through the heading instead.

---

## 2. Current gameplay loop, as implemented

This is what actually happens right now, not what was intended.

### The loop

1. Four stats decay on a 3 s tick: hunger, hygiene, energy, mood.
2. Prey auto-spawns whenever `prey_count() < 1`, on a 6 s timer.
3. The spider autonomously hunts: `IDLE` → `STALK` → `POUNCE` → `EAT`.
4. Catch probability is `0.55 - dist*0.004 + energy*0.2 + stage*0.1`,
   clamped to 0.20–0.95, ×0.7 when sick.
5. Eating gives +15 hunger, +8 mood, +5 XP, and drops poop (−12 hygiene).
6. The player can also press FEED / CLEAN / PLAY / SLEEP / DECOR / STATUS.

### The numbers

| Stat | Decay | Restored by |
|---|---|---|
| hunger | 1 per 3 s (≈33 min from 100→0) | eating prey (+15) |
| hygiene | 0.7 per 3 s | CLEAN button (→100) |
| energy | 0.5 per 3 s awake, −2 per 3 s asleep | sleeping |
| mood | drifts toward avg(hunger, hygiene, energy) −12 per stat below 25 | petting (+6), play (+10), eating (+8) |

SICK triggers when `hunger < 10 || mood < 10`. Recovery needs
`hunger > 25 && mood > 25`. The spider never dies.

---

## 3. Honest assessment: what is not fun yet

These are the real gaps. I am listing them in the order I believe they matter.

### 3.1 The player has almost nothing to do

This is the biggest problem. The spider **feeds itself**. Prey spawns
automatically and the spider hunts autonomously, so hunger — the primary
driver — resolves without the player. FEED just spawns prey the spider would
have gotten anyway.

The player's actual agency is: press CLEAN when hygiene is low, press PLAY for
mood, press SLEEP when energy is low. That is three buttons on a timer, with no
decisions and no failure states.

**The core design question for the architect:** should feeding be player-driven
(the spider only hunts prey *you* introduce), or should the loop be about
something else entirely (arrangement, breeding, observation)?

### 3.2 Nothing escalates

Difficulty is flat forever. Decay rates are constant, catch probability is
constant, prey behaviour is constant. A player who understands the loop in
minute one has nothing new to learn in hour ten.

Levels exist but only gate prey species (cricket at 2, moth at 4). XP comes
from actions the player barely controls.

### 3.3 Progression is cosmetic

- Levels unlock prey species only. No decor unlocks, no themes, no new
  mechanics.
- Achievements are 10 one-shot flags with no reward attached.
- Decor placement exists but has **zero gameplay effect** — decorations are
  purely visual. `behaviour.c` only uses them for collision separation.

### 3.4 The spider has no personality

The FSM has 9 states but they are mostly mechanical. There is no variation in
temperament, no preferences, no individual character. Two spiders behave
identically.

### 3.5 Feedback is thin

- No indication of *why* the spider is doing something.
- No sense of relationship or attachment.
- The status page is a readout, not a reward.

### 3.6 Known mechanical rough edges

- `GAME_TIME_SCALE` is defined but **never referenced** — time compression does
  not actually work.
- `SLEEP_ENERGY_GAIN` is defined but unused; sleep regenerates via
  `ENERGY_REGEN_ASLEEP` instead.
- `MOLT_DAYS` is defined but unused; molting triggers on stage change only.
- Hygiene only recovers via CLEAN, so an unattended spider always ends dirty.
- Decor placement picks a **random** kind (`rand() % 5`) rather than letting the
  player choose.

---

## 4. What the architect should decide

I am not proposing solutions — that is the architect's job. These are the
questions the next plan needs to answer.

1. **What is the core verb?** Right now it is "wait and press a button." What
   should it be?
2. **Where does challenge come from?** The spec forbids death. Without failure,
   what creates tension or mastery?
3. **Should the spider be autonomous or dependent?** Autonomy is charming but
   removes agency. Dependence creates obligation but can feel like a chore.
4. **What makes a session worth returning to?** There is no reason to come back
   tomorrow beyond curiosity.
5. **Do decor and progression need mechanical weight,** or is this a toy, not a
   game?

---

## 5. Constraints the next plan must respect

These are hard-won. Violating them costs hours.

### Hardware / platform

- **No death.** Neglect produces recoverable `SICK` only. No game-over, no
  restart, no permadeath.
- **LVGL 9.6 cannot mirror or rotate images.** `scale_x` is `uint32_t`;
  `LV_USE_MATRIX` is off. Any directional art must be procedural or
  pre-rendered per direction.
- **Only Montserrat 12/14/24 are compiled in.**
- **`LV_USE_GESTURE_RECOGNITION` is off** — screen switching must be
  button-driven.
- **The panel is a squircle.** Corner wedges have no pixels; `UI_CORNER` (40 px)
  is the safe inset.

### LVGL correctness

- `lv_line_set_points()` stores the pointer. Point arrays must be `static`.
- Deleting LVGL objects inside an event callback crashes. Use the deferred
  delete queue, flushed at the top of the render timer.
- All LVGL access outside the worker task must hold `esp_lv_adapter_lock`.
- `lv_image_set_src()` stores the pointer, not a copy. Each asset needs its own
  persistent descriptor (this was a real bug — see `assets.c`).

### Timing

- Logic runs at 20 Hz, render at 30 Hz. **Do not drive animation from per-frame
  position deltas** — the sampling rates differ and it beats. Use a time base
  gated on a smoothed movement measure (see `spider.c` gait).
- `g.spider_x/y` are `int16_t` and quantise to whole pixels. Sub-pixel motion
  needs the float accumulator in `behaviour.c`.

### Build

- Always use the wrapper scripts, never `idf.py` directly.
- `python` on PATH lacks `pyserial`; use
  `C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe`.
- `sdkconfig.defaults` only seeds a *fresh* config. The existing `sdkconfig`
  must be edited too.

---

## 6. Verification loop

```powershell
powershell -File build.ps1                 # writes build.log
powershell -File flash.ps1 COM7            # writes build\_flash.log
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' stability_check.py COM7 30
```

`stability_check.py` fails loudly on a silent port and on any
`Guru Meditation` / `abort` / `assert` / `panic` / `E (...)` line.

There is a 30 s heartbeat in `game.c`:

```
hb state=2 pos=(231,366) h=14 m=9 e=71 hy=0 prey=1 sick=1 lvl=49
```

**Use it.** Three separate soaks looked "clean" while the spider was frozen in
place. The heartbeat is the only way to verify behaviour unattended.

---

## 7. Suggested shape for the next plan

Not prescriptive — offered as a starting structure.

- **Phase A — Decide the core loop.** Answer §4 before writing code. Everything
  else depends on it.
- **Phase B — Give the player agency.** Whatever the core verb is, make it
  matter. If feeding stays player-driven, remove auto-spawn.
- **Phase C — Add escalation.** Something that changes over days, not minutes.
- **Phase D — Give progression teeth.** Decor effects, unlocks that change play.
- **Phase E — Personality.** Variation between spiders, or mood-driven
  behaviour that reads as character.
- **Phase F — Feedback.** Make state legible without the status page.

Each phase should end with build → flash → stability check, as before.

---

## 8. Files most likely to change

| File | Why |
|---|---|
| `game_config.h` | All tuning lives here; most balance work is edits to this file |
| `needs.c` | Decay curves, care effects, SICK thresholds |
| `behaviour.c` | FSM, steering, what the spider chooses to do |
| `prey.c` | Spawn rules, flee AI, catch probability |
| `game.c` | Event callbacks, XP, achievements, auto-spawn timer |
| `ui_hud.c` / `ui_status.c` | Feedback and progression display |

---

## 9. One thing I would flag hardest

The spider currently **feeds itself**. That single fact removes most of the
game. Whatever else the next plan does, it should decide deliberately whether
that is the intended design — because right now the player is an observer with
three buttons, not a caretaker.
