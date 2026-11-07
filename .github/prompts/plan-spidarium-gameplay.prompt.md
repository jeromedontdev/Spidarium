---
name: plan-spidarium-gameplay
description: "Execute the Spidarium gameplay refinement (aim-and-pounce verb, energy economy, escalation, decor weight, personality) phase by phase, unattended"
agent: "agent"
argument-hint: "Optional: phase letter to start from (A-F, default: A)"
---

# Spidarium Gameplay Refinement — Execution Prompt

You are taking over Spidarium after the engine overhaul. The build is green and
verified on hardware, but the game is not fun yet. This file is your briefing and
your spec. Read it in full before touching any code.

The previous agent's honest assessment is in `HANDOFF-gameplay.md`. Read that too.
It is accurate — I verified every claim in it against the source.

## Operating rules (read these first)

**R1 — Read the whole file first.** Including the Constraints and the
"Verified facts about the current code" sections. Several of them will save you
from re-introducing bugs that already cost hours.

**R2 — Back up before you break anything.** There is no version control in this
workspace. Before your first edit, copy `main/` to `main_backup_<YYYYMMDD>_gameplay/`.

**R3 — One phase at a time, verified.** For each phase: implement → build → flash →
stability check → log → next phase. Never start phase N+1 with phase N unverified.

**R4 — Never leave the tree uncompilable.** If you hit a wall mid-phase, revert that
phase's partial work so the last committed state still builds.

**R5 — Log progress to `PROGRESS.md`.** Append a section per phase: what changed,
files touched, build result, stability result, decisions made. The human reads this
to catch up.

**R6 — Bail out loudly, don't thrash.** If flashing fails 3 times in a row, or a build
error resists 3 fix attempts: stop, write `BLOCKED.md` with the phase, exact error
text, what you tried, and your hypothesis. Then end your turn.

**R7 — Don't ask questions.** Where this spec is silent, make the reasonable choice,
implement it, and record it under "Decisions made" in `PROGRESS.md`.

**R8 — Respect the Constraints section.** Those encode bugs that already crashed this
project.

**R9 — Report at the end.** Phases completed, phases skipped, current build state,
and the single most important thing the human should look at first.

---

## The design decision this plan encodes

The handoff's central finding: **the spider feeds itself, so the player is an
observer with three buttons.** The fix is not to remove autonomy — an autonomous
spider is what makes the tank feel alive. The fix is to make the **catch** the
player's job.

The spider stalks on its own. When it gets close, it **crouches and holds** — a
window opens. The player taps where the bug will be. Landing distance decides
perfect / good / miss.

**Energy is the session budget.** Every pounce costs energy; a miss costs energy for
nothing. That is where tension comes from without violating the no-death rule.

Caretaking stops being the game and becomes the economy around it.

### The target loop

1. Spider wanders. Wild prey occasionally drifts in (lean, skittish).
2. Spider spots prey → STALK (creeps, turn-rate limited).
3. In range → **AIM**: crouches, legs coil, focus ring pulses, soft tick plays.
4. Player taps a point in the tank. Spider leaps there (clamped to max leap).
5. Landing distance to the bug decides perfect / good / miss.
6. Perfect → big hunger + XP + streak. Miss → bug bolts, energy spent, streak resets.
7. Window expires with no tap → auto-pounce at reduced odds. Never deadlocks, never dies.
8. Energy drains per pounce. Low energy → spider rests. Player chooses: keep hunting
   or let it recover.
9. Hunger on a ~90 min clock is the reason to come back.

---

## Verified facts about the current code

I read the source. These are true as of this writing. Do not re-derive them.

**Dead knobs (defined, never referenced):**
- `GAME_TIME_SCALE` — time compression silently does nothing.
- `SLEEP_ENERGY_GAIN` — superseded by `ENERGY_REGEN_ASLEEP`.
- `MOLT_DAYS` — molting only triggers on stage change.
- `FEED_HYGIENE_COST` — **and `terrarium_add_poop()` is never called from anywhere.**
  Poop never appears in the tank. This is a real bug, not just a dead constant.

**Current decay** (`needs.c`): integer accumulators, `DECAY_PERIOD_MS` 3000.
Hunger `1.0`/tick, hygiene `0.7`/tick, energy `0.5`/tick awake, `-2`/tick asleep.
Hunger 100→0 is ≈33 min.

**Current spawn** (`game.c` `game_tick_logic`): `if (prey_count() < 1 && now - last_spawn >= 6000u) prey_spawn();`

**Current catch** (`prey.c` `prey_resolve_pounce`): `p = 0.55 - d*0.004 + energy*0.2 + stage*0.1`,
clamped 0.20–0.95, `×0.7` when sick. No player input involved.

**Current decor** (`game.c` `game_on_decorate`): `g.decor_selected = rand() % 5`. Random.
`behaviour.c` uses decor only for collision separation. Zero gameplay effect.

**`prey_t`** (`prey.c`) is a file-static struct with `alive/dead/fleeing/species/x/y/heading/
retarget_at/wander_dir/frame_at/frame/dead_at/root/parts/part_n`. Add new fields here.

**`spider_play_once`** only handles `ANIM_POUNCE` (180 ms) and `ANIM_MOLT` (800 ms);
everything else returns early. `s_scale_x/s_scale_y` are set in `spider_render` from
`g.state` — that is where the AIM crouch pose goes.

**`game_state_t`** (`game.h`) ends with `float spider_heading;`. **Append new fields at
the end.** Saves are disposable (see Phase F4), so no migration is needed, but appending
keeps the struct layout predictable and avoids renumbering anything.

---

## Phase A — The hunt becomes the verb

Highest value. Do this first and verify it before anything else.

### A1. New FSM state

In `main/game.h`, add `SPIDER_AIM` to `spider_state_t` **between** `SPIDER_STALK` and
`SPIDER_POUNCE`. Note this renumbers `SPIDER_POUNCE` and everything after it — the
heartbeat prints the raw number, which is fine, but check nothing persists `state`
(it does not; `state_load()` resets it to `SPIDER_IDLE`).

### A2. `main/game_config.h` — new tunables

Add a new section. Every value here is a feel knob; keep them together.

```c
/* ------------------------------------------------------------------ */
/* Hunting: aim window and catch quality                               */
/* ------------------------------------------------------------------ */
/* These are deliberately GENEROUS. The first pass at 1400 ms / 14 px felt
 * tight on the 480 px AMOLED: the window closed before the player could
 * react, and a 14 px perfect radius is smaller than a fingertip. Widen
 * before you narrow. If it now feels too easy, cut AIM_WINDOW_MS first. */
#define AIM_RANGE_PX            78     /* stalk -> aim transition distance */
#define AIM_WINDOW_MS           2200u  /* base window; THE most important knob */
#define AIM_WINDOW_MIN_MS       1300u  /* floor, even at high level */
#define AIM_WINDOW_PER_LEVEL    30u    /* window shrinks as you level */

#define POUNCE_MAX_PX           130.0f /* player tap is clamped to this */
#define PLAYER_POUNCE_BONUS     1.15f
#define AUTO_POUNCE_PENALTY     0.55f

/* Touch targets. A fingertip is ~40 px on this panel, so the perfect radius
 * must be comfortably larger than the visual size of the bug. */
#define CATCH_PERFECT_PX        22.0f
#define CATCH_GOOD_PX           44.0f

#define XP_PERFECT              12
#define XP_GOOD                 7
#define XP_MISS                 1
#define PERFECT_HUNGER_BONUS    5
#define PERFECT_MOOD_BONUS      4

#define STREAK_BONUS_PER        0.05f
#define STREAK_MAX              10

#define POUNCE_ENERGY_COST      3
#define EAT_ENERGY_GAIN         6
```

### A3. `main/behaviour.c` — the AIM state

Add file-static state:

```c
static bool     s_aim_active;
static uint32_t s_aim_start;
static int32_t  s_aim_tx, s_aim_ty;
static bool     s_pounce_by_player;
```

Add a window helper (temperament is added in Phase E; leave a hook):

```c
static uint32_t aim_window_ms(void)
{
    uint32_t w = AIM_WINDOW_MS;
    uint32_t shrink = g.level * AIM_WINDOW_PER_LEVEL;
    if (shrink >= w) w = AIM_WINDOW_MIN_MS;
    else w -= shrink;
    if (w < AIM_WINDOW_MIN_MS) w = AIM_WINDOW_MIN_MS;
    return w;
}
```

In `behaviour_set_state()`, clear `s_aim_active` on every transition (alongside the
existing `s_has_target`/`s_pouncing` resets).

In the `SPIDER_STALK` case, replace the direct pounce with a transition to AIM:

```c
if (d < AIM_RANGE_PX && (int32_t)(now_ms - s_next_pounce_at) >= 0) {
    s_aim_tx = px;
    s_aim_ty = py;
    s_aim_start = now_ms;
    s_aim_active = true;
    behaviour_set_state(SPIDER_AIM);
    s_aim_active = true;   /* set_state clears it */
    audio_play(SFX_AIM);
}
```

Add the `SPIDER_AIM` case:

```c
case SPIDER_AIM: {
    /* Hold position. Track the bug so the auto-pounce aims at where it is
     * now, not where it was when the window opened. */
    int32_t px, py;
    if (prey_nearest(&px, &py)) {
        s_aim_tx = px;
        s_aim_ty = py;
    } else {
        /* Prey vanished (cleaned away). Abort cleanly. */
        s_aim_active = false;
        behaviour_set_state(SPIDER_IDLE);
        break;
    }

    /* Face the target while crouched. */
    float want = atan2f((float)(s_aim_ty - g.spider_y),
                        (float)(s_aim_tx - g.spider_x));
    steer_toward(want, dt_s);

    /* Window expired: auto-pounce at reduced odds. This is the safety valve
     * that keeps the loop from deadlocking when the player is absent. */
    if ((now_ms - s_aim_start) >= aim_window_ms()) {
        s_aim_active = false;
        behaviour_pounce_at_ex(s_aim_tx, s_aim_ty, false);
    }
    break;
}
```

### A4. `main/behaviour.h` — new API

```c
/* True while the spider is crouched, waiting for the player's tap. */
bool behaviour_aim_active(void);

/* Milliseconds left in the current aim window (0 if not aiming). */
uint32_t behaviour_aim_remaining_ms(void);

/* Total length of the current aim window, including level, decor and
 * temperament modifiers. The focus ring needs this to drain correctly --
 * using the raw AIM_WINDOW_MS constant makes the ring lie. */
uint32_t behaviour_aim_total_ms(void);

/* Player-initiated pounce. Returns false if not currently aiming.
 * The target is clamped to POUNCE_MAX_PX from the spider. */
bool behaviour_player_pounce(int32_t x, int32_t y);

/* Pounce with an explicit origin flag. behaviour_pounce_at() is the
 * auto-pounce wrapper (by_player = false). */
void behaviour_pounce_at_ex(int32_t x, int32_t y, bool by_player);

/* True if the most recent pounce was player-initiated. Read by prey.c. */
bool behaviour_pounce_by_player(void);
```

Implement `behaviour_pounce_at_ex` by moving the body of the existing
`behaviour_pounce_at` into it and setting `s_pounce_by_player = by_player`.
Keep `behaviour_pounce_at(x,y)` as a thin wrapper calling `_ex(x, y, false)` so
existing call sites still compile.

`behaviour_player_pounce` clamps:

```c
bool behaviour_player_pounce(int32_t x, int32_t y)
{
    if (!s_aim_active) return false;

    float dx = (float)(x - g.spider_x);
    float dy = (float)(y - g.spider_y);
    float d  = sqrtf(dx * dx + dy * dy);
    if (d > POUNCE_MAX_PX) {
        dx = dx / d * POUNCE_MAX_PX;
        dy = dy / d * POUNCE_MAX_PX;
        x = g.spider_x + (int32_t)dx;
        y = g.spider_y + (int32_t)dy;
    }

    s_aim_active = false;
    behaviour_pounce_at_ex(x, y, true);
    return true;
}
```

Also deduct energy in `behaviour_pounce_at_ex` so auto-pounces pay too:

```c
g.energy = (g.energy > POUNCE_ENERGY_COST)
         ? (uint8_t)(g.energy - POUNCE_ENERGY_COST) : 0;
```

### A5. `main/game.c` — route the tap

In `game_on_terrarium_tap`, the order of precedence must be:
**decor mode → aim pounce → pet.**

```c
void game_on_terrarium_tap(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_active();
    lv_point_t p = {0, 0};
    if (indev) lv_indev_get_point(indev, &p);

    /* 1. Decor placement keeps priority. */
    if (g.decor_mode) {
        /* ...existing placement code, unchanged... */
        return;
    }

    /* 2. Aiming: a tap off the body is a pounce. */
    if (behaviour_aim_active()) {
        float dx = (float)(p.x - g.spider_x);
        float dy = (float)(p.y - g.spider_y);
        if (dx * dx + dy * dy > (float)(BODY_R * BODY_R * 4)) {
            if (behaviour_player_pounce(p.x, p.y)) {
                g.taps++;
                return;
            }
        }
    }

    /* 3. Otherwise pet. */
    game_on_pet(e);
}
```

Note the existing function reads the indev point *inside* the decor branch. Hoist it
to the top as shown so both branches share it.

### A6. `main/prey.c` — catch quality

Add to `prey_t`: `bool wild;` (used in Phase B, set it now to `false`).

Rewrite the probability and resolution in `prey_resolve_pounce`:

```c
float p = CATCH_BASE - best_d * CATCH_PER_PX;
p += ((float)g.energy / 100.0f) * CATCH_ENERGY_BONUS;
p += ((float)g.stage / 2.0f) * CATCH_STAGE_BONUS;
if (g.sick) p *= 0.7f;

/* Player skill vs. the auto-pounce safety valve. */
p *= behaviour_pounce_by_player() ? PLAYER_POUNCE_BONUS : AUTO_POUNCE_PENALTY;

if (p < 0.20f) p = 0.20f;
if (p > 0.95f) p = 0.95f;
```

Then classify the landing **before** the roll, because quality is about aim, not luck:

```c
typedef enum { CATCH_MISS = 0, CATCH_GOOD, CATCH_PERFECT } catch_quality_t;

catch_quality_t q = CATCH_MISS;
if (best_d <= CATCH_PERFECT_PX)      q = CATCH_PERFECT;
else if (best_d <= CATCH_GOOD_PX)    q = CATCH_GOOD;
```

Resolution rules:
- `q == CATCH_PERFECT` → always caught. `FEED_HUNGER_GAIN + PERFECT_HUNGER_BONUS`,
  `FEED_MOOD_GAIN + PERFECT_MOOD_BONUS`, `XP_PERFECT`, `g.streak++`.
- `q == CATCH_GOOD` → caught if `roll <= p`. Normal gains, `XP_GOOD`, streak unchanged.
- `q == CATCH_MISS` → caught only if `roll <= p * 0.5`. If caught, normal gains,
  `XP_GOOD`. If missed, `XP_MISS`, `g.streak = 0`, prey flees.

Streak multiplies XP:

```c
uint32_t xp = base_xp;
uint32_t bonus = g.streak > STREAK_MAX ? STREAK_MAX : g.streak;
xp = (uint32_t)((float)xp * (1.0f + (float)bonus * STREAK_BONUS_PER));
game_add_xp(xp);
```

On a catch, also `g.energy = clamp100(g.energy + EAT_ENERGY_GAIN)` so skilled play is
roughly energy-neutral. On a miss, `g.streak = 0`.

Award `ACH_FIRST_PERFECT` on the first perfect. Add that bit to `game.h`:

```c
#define ACH_FIRST_PERFECT   (1u << 10)
```

Log the quality so soaks are readable:
`ESP_LOGI(TAG, "catch q=%d p=%.2f d=%.0f streak=%u", (int)q, p, best_d, g.streak);`

### A7. `main/game.h` — new state fields

Append to the **end** of `game_state_t`:

```c
    uint16_t streak;        /* consecutive successful catches */
    uint16_t best_streak;
```

### A8. `main/spider.c` — the telegraph

In `spider_render`, add the AIM crouch to the existing squash/stretch chain:

```c
} else if (g.state == SPIDER_AIM) {
    sx = 285; sy = 215;   /* coiled, ready to spring */
}
```

Add a persistent focus ring. **Do not create or delete LVGL objects per frame** — build
it once in `spider_init` and toggle visibility:

```c
static lv_obj_t *s_focus_ring;

/* in spider_init, after the body exists: */
s_focus_ring = lv_arc_create(parent);
lv_obj_set_size(s_focus_ring, 76, 76);
lv_obj_set_style_bg_opa(s_focus_ring, LV_OPA_TRANSP, 0);
lv_obj_set_style_border_width(s_focus_ring, 0, 0);
lv_obj_set_style_arc_width(s_focus_ring, 3, LV_PART_MAIN);
lv_obj_set_style_arc_color(s_focus_ring, lv_color_hex(0x2a2f3a), LV_PART_MAIN);
lv_obj_set_style_arc_width(s_focus_ring, 3, LV_PART_INDICATOR);
lv_obj_set_style_arc_color(s_focus_ring, lv_color_hex(0xf5b342), LV_PART_INDICATOR);
lv_obj_set_style_bg_opa(s_focus_ring, LV_OPA_TRANSP, LV_PART_KNOB);
lv_obj_set_clickable(s_focus_ring, false);
lv_obj_set_scrollable(s_focus_ring, false);
lv_arc_set_range(s_focus_ring, 0, 100);
lv_arc_set_bg_angles(s_focus_ring, 0, 360);
lv_obj_set_hidden(s_focus_ring, true);
```

In `spider_render`, drive it:

```c
bool aiming = behaviour_aim_active();
lv_obj_set_hidden(s_focus_ring, !aiming);
if (aiming) {
    lv_obj_set_pos(s_focus_ring, g.spider_x - 38, g.spider_y - 38 + dy);
    uint32_t left  = behaviour_aim_remaining_ms();
    uint32_t total = behaviour_aim_total_ms();
    if (total == 0) total = 1;
    int32_t pct = (int32_t)((uint64_t)left * 100u / total);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    lv_arc_set_value(s_focus_ring, pct);
}
```

`spider.c` will need `#include "behaviour.h"`. Check for a circular include —
`behaviour.h` includes `game.h` only, so this is safe.

### A9. `main/ui_hud.c` — the prompt

During AIM, show a "TAP!" prompt. Reuse the existing bubble rather than adding objects:

```c
/* in ui_hud_tick, before the bubble expiry block */
static bool s_was_aiming;
bool aiming = behaviour_aim_active();
if (aiming && !s_was_aiming) {
    ui_hud_bubble("TAP!", 0xf5b342, 1500);
}
s_was_aiming = aiming;
```

Add `#include "behaviour.h"` to `ui_hud.c`.

### A10. `main/audio.c` — the aim tick

Add `SFX_AIM` to `sfx_t` in `game.h` (before `SFX_COUNT`), and a case in `audio_play`:

```c
case SFX_AIM:
    /* Soft rising tick: the window is open. */
    n = render_sweep(520.0f, 760.0f, 90, 0.22f);
    break;
```

Add `#define SFX_MS_AIM 90` to `game_config.h`.

### A11. Heartbeat

Extend the `hb` line in `game_tick_logic` so unattended soaks are verifiable:

```c
ESP_LOGI(TAG, "hb state=%u pos=(%d,%d) h=%u m=%u e=%u hy=%u prey=%d sick=%u lvl=%lu aim=%d streak=%u",
         (unsigned)g.state, g.spider_x, g.spider_y,
         g.hunger, g.mood, g.energy, g.hygiene,
         prey_count(), g.sick, (unsigned long)g.level,
         behaviour_aim_active() ? 1 : 0, (unsigned)g.streak);
```

### Phase A gate

- `build.ps1` exit 0, no new warnings.
- `flash.ps1 COM7` exit 0.
- `stability_check.py COM7 30` → `VERDICT: PASS`.
- 5 min soak: the heartbeat must show `aim=1` at least once, and `state` must cycle
  through more than one value. If `aim` never appears, the AIM transition is not firing —
  check `AIM_RANGE_PX` against the actual stalk distance.
- Manual: the crouch is visible, a tap lands a pounce, a miss visibly costs energy.

---

## Phase B — Feeding dependency and the dead knobs

### B1. Replace the auto-spawn with a wild trickle

In `game.c` `game_tick_logic`, replace the 6 s spawn block:

```c
/* Wild prey drifts in occasionally. This keeps the tank alive without
 * feeding the spider for free: wild bugs are leaner than the ones the
 * player introduces, so the player is still necessary. */
static uint32_t last_wild;
if (prey_count() == 0 && (now - last_wild) >= WILD_SPAWN_MS) {
    last_wild = now;
    prey_spawn_wild();
}
```

`#define WILD_SPAWN_MS 1200000u` (20 min) in `game_config.h`.

### B2. Wild vs fed prey

In `prey.c`, add `prey_spawn_wild()` and give `prey_spawn_species` a wild flag:

```c
void prey_spawn_species_ex(prey_species_t species, bool wild);
void prey_spawn_species(prey_species_t species);   /* wrapper: wild = false */
void prey_spawn_wild(void);                        /* random species, wild = true */
```

Set `p->wild = wild` in the spawn. Then in `prey_resolve_pounce`, use the wild flag for
the gains:

```c
int hunger_gain = pr->wild ? WILD_HUNGER_GAIN : FEED_HUNGER_GAIN;
int mood_gain   = pr->wild ? WILD_MOOD_GAIN   : FEED_MOOD_GAIN;
```

`#define WILD_HUNGER_GAIN 8`, `#define WILD_MOOD_GAIN 4` in `game_config.h`.

Wild prey are also harder to catch — in `prey_tick`, scale the flee radius and speed:

```c
float flee_r = PREY_FLEE_RADIUS * (p->wild ? 1.25f : 1.0f);
float flee_s = PREY_FLEE_SPEED  * (p->wild ? 1.15f : 1.0f);
```

`needs_feed()` keeps calling `prey_spawn()` (fed, not wild). No change needed there.

### B3. Per-minute decay

Replace the integer accumulators in `needs.c` with per-minute floats. This is what makes
the game a check-in game rather than a 33-minute sprint.

`game_config.h`:

```c
#define HUNGER_PER_MIN          1.1f   /* ~90 min from full */
#define HYGIENE_PER_MIN         0.7f   /* ~143 min */
#define ENERGY_PER_MIN          0.5f   /* ~200 min awake */
#define ENERGY_REGEN_ASLEEP_PER_MIN 6.0f
#define HYGIENE_REGEN_PER_MIN   0.25f  /* only when the tank is clean */
```

In `needs.c`, keep the three accumulators but feed them per-minute rates scaled by the
elapsed time:

```c
float minutes = (float)DECAY_PERIOD_MS / 60000.0f * (float)GAME_TIME_SCALE;

s_hunger_acc  += HUNGER_PER_MIN  * minutes;
s_hygiene_acc += HYGIENE_PER_MIN * minutes;

if (g.state == SPIDER_SLEEP) {
    s_energy_acc -= ENERGY_REGEN_ASLEEP_PER_MIN * minutes;
} else {
    s_energy_acc += ENERGY_PER_MIN * minutes;
}
```

Delete `DECAY_HUNGER`, `DECAY_HYGIENE`, `DECAY_ENERGY`, `DECAY_ENERGY_ASLEEP`,
`ENERGY_REGEN_ASLEEP` from `game_config.h` once nothing references them.

### B4. Wire `GAME_TIME_SCALE`

It is now referenced in the decay above. Also apply it to aging:

```c
if (now_ms - s_last_age >= (AGE_TICK_MS / GAME_TIME_SCALE)) {
```

Guard against `GAME_TIME_SCALE == 0` with a `#if` or a compile-time assert.

### B5. Use or delete the remaining dead knobs

- **`MOLT_DAYS`**: add a mid-stage molt. In `needs_tick`'s aging block, after the day
  change:

```c
if (days - g.last_molt_day >= MOLT_DAYS && g.stage != STAGE_ADULT) {
    g.last_molt_day = days;
    behaviour_set_state(SPIDER_MOLT);
    spider_play_once(ANIM_MOLT, true);
    audio_play(SFX_MOLT);
    game_add_xp(XP_MOLT);
}
```

Add `#define XP_MOLT 8`.

- **`SLEEP_ENERGY_GAIN`**: delete it. It is superseded.

### B6. Poop actually happens

`terrarium_add_poop()` is never called and `FEED_HYGIENE_COST` is unused. Wire them up in
`prey_resolve_pounce` on a successful catch:

```c
g.hygiene = (g.hygiene > FEED_HYGIENE_COST)
          ? (uint8_t)(g.hygiene - FEED_HYGIENE_COST) : 0;
terrarium_add_poop(g.spider_x + (rand() % 17) - 8,
                   g.spider_y + (rand() % 17) - 8);
```

`prey.c` will need `#include "terrarium.h"`.

### B7. Passive hygiene recovery

In `needs.c`, before applying the hygiene accumulator, add recovery when the tank is
clean. Track poop count — add a helper to `terrarium.h`:

```c
int terrarium_poop_count(void);
```

Then:

```c
if (terrarium_poop_count() == 0) {
    s_hygiene_acc -= HYGIENE_REGEN_PER_MIN * minutes;
}
```

This removes the "unattended spider is always dirty" dead end while keeping CLEAN
valuable (instant reset + poop removal).

### B8. Player-chosen decor

Replace `rand() % 5` in `game_on_decorate`. Keep it on the existing button — no new screen:

```c
void game_on_decorate(lv_event_t *e)
{
    (void)e;
    g.taps++;

    if (!g.decor_mode) {
        /* First press: cycle the selected kind. */
        g.decor_selected = (uint8_t)((g.decor_selected + 1) % DECO_KINDS);
        char buf[32];
        snprintf(buf, sizeof buf, "%s", decor_kind_name(g.decor_selected));
        ui_hud_bubble(buf, 0xf5b342, 900);
        return;
    }

    /* Second press: enter placement mode. */
    g.decor_mode = 1;
    ui_hud_set_decor_mode(true);
    ui_hud_bubble("tap to place", 0xf5b342, 1200);
}
```

This needs a small `decor_kind_name()` and `DECO_KINDS` exported. Move `DECO_KINDS` from
`terrarium.c` into `game.h` (it is currently a private `#define` there) and add:

```c
const char *decor_kind_name(uint8_t kind);   /* in terrarium.h */
```

Names: `"pebble"`, `"leaf"`, `"twig"`, `"flower"`, `"crystal"`.

**Note the UX change:** one press cycles, a second press enters placement. Document it in
the bubble text so it is discoverable.

### Phase B gate

- Build/flash/stability PASS.
- 10 min soak: the tank is not empty, hunger has moved but not collapsed, the spider is
  not permanently sick, and `prey=` is not stuck at 0.
- Manual: press DECOR twice — the bubble names a kind, then placement mode opens.

---

## Phase C — Escalation

### C1. Prey scale with level

`game_config.h`:

```c
#define PREY_SPEED_PER_LEVEL    0.06f
#define PREY_SPEED_MAX_MULT     1.8f
#define PREY_ALERT_PER_LEVEL    0.05f
#define PREY_ALERT_MAX_MULT     1.6f
```

In `prey_tick`, compute once per bug:

```c
float lvl = (float)g.level;
float speed_mult = 1.0f + lvl * PREY_SPEED_PER_LEVEL;
if (speed_mult > PREY_SPEED_MAX_MULT) speed_mult = PREY_SPEED_MAX_MULT;

float alert_mult = 1.0f + lvl * PREY_ALERT_PER_LEVEL;
if (alert_mult > PREY_ALERT_MAX_MULT) alert_mult = PREY_ALERT_MAX_MULT;
```

Apply to `PREY_SPEED`, `PREY_FLEE_SPEED` and `PREY_FLEE_RADIUS`.

The aim window already shrinks with level via `AIM_WINDOW_PER_LEVEL`.

### C2. Species-specific evasion

Add a `move_style` to `prey_t` (or switch on `species`). In `prey_tick`:

- **Fly** — erratic. Retarget every ~600 ms instead of 1500, and add a small random
  heading jitter each tick. Fast, unpredictable, low stamina.
- **Cricket** — **hops.** Burst movement on a 900 ms cycle: fast for 200 ms, still for
  700 ms. Predictable if you watch the rhythm. This is the skill expression.
- **Moth** — **flutter.** Sinusoidal lateral offset on top of its heading:
  `lateral = sinf(now * 0.006f) * 14.0f`, applied perpendicular to the heading.
- **Beetle** — new species, unlock at level 6. Slow, but `CATCH_PERFECT_PX` is halved
  against it (armoured). High reward.

For the beetle, add `PREY_BEETLE` to `prey_species_t` in `game.h` (before
`PREY_SPECIES_COUNT`), a body in `build_body` (a squat dark oval with a seam), and update
`prey_unlocked_species()`:

```c
if (g.level >= 6) return 4;
if (g.level >= 4) return 3;
if (g.level >= 2) return 2;
return 1;
```

In `prey_resolve_pounce`, halve the perfect radius for a beetle. With
`CATCH_PERFECT_PX` at 22 this gives 11 px — tight but reachable, and it is the
point: the beetle is the "hard" catch:

```c
float perfect_px = (pr->species == PREY_BEETLE) ? CATCH_PERFECT_PX * 0.5f : CATCH_PERFECT_PX;
```

### C3. Daily wild visitor

In `game.c`, when the day changes (hook into the existing `days != g.days_lived` path in
`needs.c`, or poll in `game_tick_logic`), spawn one rare bug:

```c
/* One rare visitor per in-game day: the reason to come back tomorrow. */
int top = prey_unlocked_species();
prey_species_t rare = (prey_species_t)((top < PREY_SPECIES_COUNT) ? top : PREY_SPECIES_COUNT - 1);
prey_spawn_species_ex(rare, true);
ui_hud_bubble("a visitor!", 0x4fd1c5, 2000);
audio_play(SFX_ALERT);
```

Award `VISITOR_XP` (define `20`) on capturing it. Track with a `uint8_t visitor_today;`
field reset on day change.

### C4. Achievements get rewards

In `game_award`, grant XP per achievement and a free decor placement for a subset:

```c
void game_award(uint32_t achievement)
{
    if (g.achievements & achievement) return;
    g.achievements |= achievement;
    game_add_xp(XP_ACHIEVEMENT);   /* define 15 */
    ESP_LOGI(TAG, "achievement 0x%08lx", (unsigned long)achievement);
}
```

Careful: `game_add_xp` can itself call `game_award` (level 5). That is already the case
today and does not recurse infinitely because the bit is set before the XP is added —
verify this ordering holds after your change.

### Phase C gate

Build/flash/stability PASS. Manual: prey visibly differ in movement; the aim window is
noticeably tighter at a higher level (use `GAME_TIME_SCALE` or a temporary XP grant to
test).

---

## Phase D — Decor with mechanical weight

### D1. Effect table

Create `main/decor.h` / `main/decor.c` (new module — keeps `terrarium.c` about drawing).

```c
typedef struct {
    float energy_regen_mult;   /* pebble */
    float hygiene_decay_mult;  /* leaf   */
    float pounce_range_mult;   /* twig   */
    float aim_window_mult;     /* crystal */
    int   wild_spawn_bonus;    /* flower, count-based */
} decor_bonus_t;

void decor_compute(void);              /* call once per logic tick */
const decor_bonus_t *decor_bonus(void);
```

| Kind | Effect | Radius |
|---|---|---|
| pebble | `energy_regen_mult` 1.5 | 90 px |
| leaf | `hygiene_decay_mult` 0.6 | 90 px |
| twig | `pounce_range_mult` 1.2 | 90 px |
| flower | `wild_spawn_bonus` +1 per flower | global |
| crystal | `aim_window_mult` 1.25 | 90 px |

`decor_compute()` resets to neutral, then for each used slot tests distance to the
spider and multiplies. Flower is global (count-based), not proximity.

Call `decor_compute()` at the top of `game_tick_logic`, before `needs_tick`.

### D2. Consume the bonuses

- `behaviour.c` `aim_window_ms()`: `w = (uint32_t)((float)w * decor_bonus()->aim_window_mult);`
- `behaviour.c` `behaviour_player_pounce()`: clamp to `POUNCE_MAX_PX * pounce_range_mult`.
- `needs.c` energy regen: multiply by `energy_regen_mult`.
- `needs.c` hygiene decay: multiply by `hygiene_decay_mult`.
- `game.c` wild spawn: `WILD_SPAWN_MS / (1 + wild_spawn_bonus)`.

### D3. Legibility

- During placement mode, draw a faint ring at the tap point showing the effect radius
  before committing. Reuse the `make_dot` pattern; keep it a persistent object.
- Status page lists active effects and their current contribution (Phase F).

### D4. Level-gated decor kinds

In `game_on_decorate`, skip locked kinds when cycling:

```c
static const uint8_t k_unlock_level[DECO_KINDS] = {1, 2, 3, 5, 7};
```

Cycle to the next kind whose unlock level is `<= g.level`. If none, bubble "locked".

### Phase D gate

Build/flash/stability PASS. Manual: place a crystal, confirm the aim window visibly
lengthens (the focus ring drains slower).

---

## Phase E — Personality

### E1. Temperament

`game.h` — append to `game_state_t`:

```c
    uint8_t temperament;   /* 0 bold, 1 timid, 2 greedy, 3 lazy */
```

`game_config.h` — declare the type and the table (do **not** define the array here, or
every translation unit gets its own copy):

```c
typedef struct {
    const char *name;
    float aim_window_mult;
    float catch_mult;
    float speed_mult;
    float hunger_decay_mult;
    uint8_t sleep_threshold;
} temperament_t;

#define TEMPERAMENT_COUNT 4

extern const temperament_t k_temperaments[TEMPERAMENT_COUNT];
```

`game.c` — define it once:

```c
const temperament_t k_temperaments[TEMPERAMENT_COUNT] = {
    /* name      aim    catch  speed  hunger  sleep */
    { "bold",   0.85f, 1.10f, 1.10f, 1.00f, 15 },
    { "timid",  1.25f, 0.95f, 0.90f, 0.90f, 20 },
    { "greedy", 1.00f, 1.05f, 1.00f, 1.25f, 15 },
    { "lazy",   1.15f, 0.90f, 0.80f, 0.85f, 30 },
};
```

Add a convenience accessor so call sites stay readable:

```c
/* in game.h */
const temperament_t *game_temperament(void);
/* in game.c */
const temperament_t *game_temperament(void)
{
    uint8_t t = g.temperament;
    if (t >= TEMPERAMENT_COUNT) t = 0;
    return &k_temperaments[t];
}
```

Roll once at first boot. **Do not roll it in `state_defaults()`** — that runs before
`srand()` in `game_init()`, so every fresh boot would get the same temperament. Use a
sentinel instead:

```c
/* in state_defaults(): */
g.temperament = 0xFF;   /* unset; rolled after the RNG is seeded */

/* in game_init(), after srand() and state_load(): */
if (g.temperament >= TEMPERAMENT_COUNT) {
    g.temperament = (uint8_t)(rand() % TEMPERAMENT_COUNT);
    ESP_LOGI(TAG, "temperament: %s", game_temperament()->name);
}
```

This also gives Phase F's save reset a free temperament roll for the existing spider,
because the v1 blob is rejected outright and `state_defaults()` runs with the sentinel
intact.

Apply in `behaviour.c` (window, speed, sleep threshold) and `needs.c` (hunger decay).
Show it on the status page.

Concretely:
- `aim_window_ms()`: `w = (uint32_t)((float)w * game_temperament()->aim_window_mult);`
- `behaviour_tick()`: `speed_scale *= game_temperament()->speed_mult;`
- `SPIDER_IDLE` sleep check: `g.energy < game_temperament()->sleep_threshold`
- `prey_resolve_pounce()`: `p *= game_temperament()->catch_mult;`
- `needs_tick()`: `s_hunger_acc += HUNGER_PER_MIN * minutes * game_temperament()->hunger_decay_mult;`

### E2. Bond

`game.h` — append:

```c
    uint16_t bond;
```

Growth: player-assisted catch `+2`, perfect catch `+4`, pet `+1`. Decay: `-1` per decay
tick while SICK. Clamp 0–100.

Thresholds:
- 25 → the spider turns toward taps even when idle.
- 50 → aim window `+15%`.
- 75 → the spider occasionally brings a decor item (spawn a free decor slot).
- 100 → a distinct idle animation (a slow sway; reuse the existing part layout).

### E3. Mood gates hunting

In `behaviour.c` `SPIDER_IDLE`, before the prey check:

```c
} else if (g.mood < MOOD_REFUSE_HUNT) {
    /* Bored spiders will not hunt. This chains the verbs:
     * play -> hunt -> sleep -> clean. */
    if (now_ms >= s_idle_until) behaviour_set_state(SPIDER_WANDER);
}
```

`#define MOOD_REFUSE_HUNT 20`.

**Important:** this must not create a deadlock. If mood is low and the player is absent,
the spider must still be able to recover. Mood drifts toward the average of the other
stats, so it recovers on its own as hunger/hygiene/energy recover. Verify in the soak
that a low-mood spider does not sit forever. If it does, lower the threshold or exempt
SICK spiders.

### Phase E gate

Extend the heartbeat first, so the phase is verifiable unattended:

```c
ESP_LOGI(TAG, "hb state=%u pos=(%d,%d) h=%u m=%u e=%u hy=%u prey=%d sick=%u lvl=%lu "
              "aim=%d streak=%u bond=%u temp=%s",
         (unsigned)g.state, g.spider_x, g.spider_y,
         g.hunger, g.mood, g.energy, g.hygiene,
         prey_count(), g.sick, (unsigned long)g.level,
         behaviour_aim_active() ? 1 : 0, (unsigned)g.streak,
         (unsigned)g.bond, game_temperament()->name);
```

Build/flash/stability PASS. Manual: erase NVS twice, confirm two different temperaments
appear in the status page and the heartbeat.

**Deadlock check (do not skip):** run a 10 min soak with the spider deliberately starved
of play (do not press PLAY). Confirm from the heartbeat that mood recovers on its own and
the spider resumes hunting. If it sits in WANDER forever, the mood gate is too aggressive
— lower `MOOD_REFUSE_HUNT` or exempt SICK spiders.

---

## Phase F — Feedback and legibility

### F1. State-driven bubbles

In `ui_hud_tick`, drive the bubble from `g.state` when nothing more urgent is showing:

| State | Text |
|---|---|
| STALK | "hunting" |
| AIM | "TAP!" |
| EAT | "yum" |
| SLEEP | "zzz" |
| SICK | "not well" |
| low mood | "bored" |

Keep the existing explicit bubbles (from button presses) taking priority — track a
`s_bubble_until` and only auto-drive when it has expired.

### F2. Status page becomes a relationship page

Add to `ui_status.c`:
- temperament name
- bond meter (a bar, same style as the stat bars)
- streak / best streak
- next unlock ("next: beetle at level 6")
- active decor effects and their contribution

Keep the existing bars, stage, age, level, achievements, mute.

### F3. Audio

Add `SFX_PERFECT` (bright chime) to `sfx_t` and `audio_play`:

```c
case SFX_PERFECT:
    n = render_sweep(880.0f, 1760.0f, 160, 0.30f);
    break;
```

Play it on a perfect catch.

### F4. Save version — reset, do not migrate

**The human has explicitly said saves are unimportant and the game may be reset.** Do not
write a migration shim. This removes the single most error-prone part of the plan.

Bump `SAVE_VERSION` to `2` in `game_config.h`. The existing header check in `save.c`
already rejects any version mismatch, so a v1 blob is discarded and the game starts from
`state_defaults()`. That is the desired behaviour — no code change needed in `save.c` at
all.

Consequences, all acceptable:
- The current spider (level 49 in the logs) is discarded on first boot after the update.
- A fresh temperament is rolled, which is the intended replay hook.
- `state_defaults()` must therefore set every new field to a sane value, because it is now
  the only path that initialises them. Check `streak`, `best_streak`, `bond` are zeroed
  (the existing `memset(&g, 0, sizeof g)` handles this) and `temperament` is `0xFF`.

**Do not** add a v1→v2 prefix copy, a sentinel fixup in `save_load`, or a `#include
"game.h"` in `save.c`. If you find yourself writing any of that, stop — it is not wanted.

**Verify the round-trip:** boot, note stats, reboot, confirm they survived. Then erase NVS
and confirm a clean fresh start with a new temperament.

### Phase F gate

Build/flash/stability PASS, plus a 10 min soak reading the heartbeat. Confirm
`streak`, `bond`, `temp` appear and change.

---

## Files

| File | Phases | Why |
|---|---|---|
| `main/game_config.h` | A–F | All new tunables; the file's stated contract |
| `main/game.h` | A, C, E, F | `SPIDER_AIM`, `PREY_BEETLE`, `SFX_AIM/PERFECT`, `streak`, `temperament`, `bond`, `DECO_KINDS` |
| `main/behaviour.c` | A, D, E | AIM state, window, decor/temperament modifiers, mood gate |
| `main/prey.c` | A, B, C | Catch quality, wild flag, species evasion, level scaling, poop |
| `main/needs.c` | B, D, E | Per-minute decay, hygiene regen, bond, temperament, molt |
| `main/game.c` | A, B, C, D, E | Tap routing, wild spawn, daily visitor, decor picker, unlocks |
| `main/spider.c` | A, E | AIM pose, focus ring, bond idle |
| `main/ui_hud.c` | A, F | TAP prompt, state bubbles |
| `main/ui_status.c` | D, E, F | Temperament, bond, decor effects, next unlock |
| `main/terrarium.c` | B, D | `decor_kind_name`, `terrarium_poop_count`, effect radii |
| `main/decor.c` / `decor.h` | D | **New module.** Decor effect accumulation |
| `main/audio.c` | A, F | `SFX_AIM`, `SFX_PERFECT` |
| `main/save.c` | — | **No change.** The existing version check rejects v1 for us. |
| `main/CMakeLists.txt` | D | Add `decor.c` to SRCS |

---

## Constraints (hard-won; violating these costs hours)

### Hardware / platform
- **No death.** Neglect produces recoverable `SICK` only. No game-over, no restart, no
  permadeath. Every phase must preserve this.
- **LVGL 9.6 cannot mirror or rotate images.** `scale_x` is `uint32_t`; `LV_USE_MATRIX`
  is off. Directional art must be procedural or pre-rendered per direction.
- **Only Montserrat 12/14/24 are compiled in.**
- **`LV_USE_GESTURE_RECOGNITION` is off** — screen switching must be button-driven.
- **The panel is a squircle.** Corner wedges have no pixels; `UI_CORNER` (40 px) is the
  safe inset.

### LVGL correctness
- `lv_line_set_points()` stores the pointer. Point arrays must be `static`.
- **Deleting LVGL objects inside an event callback crashes.** Use the deferred delete
  queue, flushed at the top of the render timer.
- All LVGL access outside the worker task must hold `esp_lv_adapter_lock`.
- `lv_image_set_src()` stores the pointer, not a copy. Each asset needs its own
  persistent descriptor.
- **Never create or delete LVGL objects per frame.** The focus ring, reticle and decor
  radius preview must be built once and toggled with `lv_obj_set_hidden`.

### Timing
- Logic runs at 20 Hz, render at 30 Hz. **Do not drive animation from per-frame position
  deltas** — the sampling rates differ and it beats. Use a time base.
- `g.spider_x/y` are `int16_t` and quantise to whole pixels. Sub-pixel motion needs the
  float accumulator in `behaviour.c`.

### Build
- Always use the wrapper scripts, never `idf.py` directly.
- `python` on PATH lacks `pyserial`; use
  `C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe`.
- `sdkconfig.defaults` only seeds a *fresh* config. The existing `sdkconfig` must be
  edited too.

---

## Verification loop

```powershell
powershell -File build.ps1                 # writes build.log
powershell -File flash.ps1 COM7            # writes build\_flash.log
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' stability_check.py COM7 30
```

`stability_check.py` fails loudly on a silent port and on any `Guru Meditation` / `abort`
/ `assert` / `panic` / `E (...)` line.

**Use the heartbeat.** Three separate soaks in the previous run looked "clean" while the
spider was frozen in place. The heartbeat is the only way to verify behaviour unattended.
After Phase A it must include `aim=` and `streak=`; after Phase E, `bond=` and `temp=`.

---

## Tuning: the panel feels tight

The human reports that **everything feels tight on the OLED**. Treat this as a standing
instruction, not a one-off note. The 2.16" 480×480 panel is physically small, so anything
that requires precision is harder than it looks in a mockup.

The values in this plan are already widened from the first draft. If it still feels tight
after Phase A, adjust in this order — each is a single constant:

| Symptom | Knob | Suggested step |
|---|---|---|
| Window closes before you can react | `AIM_WINDOW_MS` | 2200 → 2600 |
| Still too tight at high level | `AIM_WINDOW_MIN_MS` | 1300 → 1600 |
| Level scaling bites too early | `AIM_WINDOW_PER_LEVEL` | 30 → 20 |
| Perfect is unreachable | `CATCH_PERFECT_PX` | 22 → 28 |
| Good is unreachable | `CATCH_GOOD_PX` | 44 → 55 |
| Taps land short of the bug | `POUNCE_MAX_PX` | 130 → 160 |
| Buttons hard to hit | `BTN_H 30` | 30 → 36 (check the tray still fits) |
| Stat rings hard to read | ring size 34 | 34 → 38 |

**Do not** compensate by making the spider slower or the prey dumber. The tightness is a
touch-target problem, not a difficulty problem, and slowing the spider makes the game feel
sluggish rather than generous.

Two structural notes:
- `BTN_H` is used by `ui_hud.c` for the tray and by the decor hint offset. Raising it
  shrinks the playfield (`HUD_BOTTOM_H` is separate — check both).
- The panel is a squircle. Anything placed within `UI_CORNER` (40 px) of a corner has no
  pixels. Widening touch targets must not push them into the corner wedges.

## Ordering and risk

Do the phases in order. Each is independently shippable, so if you run out of time the
tree is still better than it was.

- **Phase A is the whole point.** If you only finish A, the game is meaningfully better.
  Do not start B until A is verified on hardware.
- **Phase B changes the economy.** Expect to retune `HUNGER_PER_MIN` after playing it.
  That is normal; it is one constant.
- **Phase C and D are independent** and can be swapped if one proves easier.
- **Phase E is the riskiest** because the mood gate can deadlock. Verify the soak.
- **Phase F is polish.** It is safe to defer.

## Begin

1. Read this entire file, then `HANDOFF-gameplay.md`.
2. Back up `main/` per R2.
3. Execute Phase A. Verify it. Log it.
4. Continue in order, verifying and logging each phase.
5. Stop on R6 conditions, or when all phases are done.
6. Write the final summary per R9.
