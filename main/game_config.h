/*
 * Spidarium — every tunable in one place.
 *
 * Nothing in the game modules should contain a bare magic number; if a value
 * affects feel, pacing or layout it belongs here.
 */
#pragma once

/* ------------------------------------------------------------------ */
/* Time                                                                */
/* ------------------------------------------------------------------ */
/* Compile-time time compression. Ship at 1. Phase 5 verification builds
 * with 60 to compress a day of decay into ~24 minutes. Developer knob only;
 * there is deliberately no runtime UI toggle. */
#define GAME_TIME_SCALE        1

/* GAME_TIME_SCALE divides AGE_TICK_MS, so 0 would be a divide-by-zero. */
#if GAME_TIME_SCALE < 1
#error "GAME_TIME_SCALE must be >= 1"
#endif

#define LOGIC_HZ               20
#define RENDER_HZ              30
#define LOGIC_PERIOD_MS        (1000 / LOGIC_HZ)
#define RENDER_PERIOD_MS       (1000 / RENDER_HZ)

#define TICKS_PER_DAY          2880u   /* 30 s ticks per in-game day */
#define AGE_TICK_MS            30000u
#define SAVE_PERIOD_MS         5000u

/* ------------------------------------------------------------------ */
/* Needs decay (points per MINUTE of game time)                        */
/* ------------------------------------------------------------------ */
/* Per-minute rates make this a check-in game rather than a 33-minute
 * sprint: hunger takes ~90 min to empty, so a session is a visit, not a
 * vigil. The accumulators in needs.c carry the fraction between ticks. */
#define DECAY_PERIOD_MS        3000u

#define HUNGER_PER_MIN          3.0f   /* ~33 min from full; ~2 min to hunt again */
#define HYGIENE_PER_MIN         0.7f   /* ~143 min */
#define ENERGY_PER_MIN          0.5f   /* ~200 min awake */
#define ENERGY_REGEN_ASLEEP_PER_MIN 6.0f
/* Only applies when the tank has no poop, and it REPLACES decay rather than
 * fighting it. The spec's 0.25 was a net loss against 0.7 decay, so an
 * unattended spider still ended up dirty -- the exact dead end this was meant
 * to remove. The rule is now legible: poop dirties the tank, a clean tank
 * cleans the spider. */
#define HYGIENE_REGEN_PER_MIN   2.0f

/* Poop decomposes. Without this the tank is never clean again after the first
 * meal, so hygiene can never recover and CLEAN becomes mandatory busywork. */
#define POOP_LIFETIME_MS        600000u  /* 10 min */

/* A bug left in the tank fouls it. Slower than the spider's own droppings so a
 * short visit is not punished, but a bug parked for a long time leaves a
 * visible mess -- that is the consequence of not catching it. */
#define BUG_POOP_MS             150000u  /* 2.5 min per live bug */

/* Dirt points per dropping, used to scale the Clean reward. A spider dropping
 * is a full meal's worth of waste; a bug speckle is smaller. */
#define POOP_DIRT_SPIDER        12
#define POOP_DIRT_BUG           6

#define MOOD_PENALTY_LOW       1      /* per low stat, per decay tick */
#define MOOD_LOW_THRESHOLD     25

/* ------------------------------------------------------------------ */
/* Care actions                                                        */
/* ------------------------------------------------------------------ */
#define FEED_HUNGER_GAIN       15

/* At or above this hunger the spider is full and refuses to hunt. The slingshot
 * is disabled rather than silently doing nothing, because a pull that produces
 * no result reads as a broken control. */
#define HUNGER_FULL            95
#define FEED_MOOD_GAIN         8
#define FEED_HYGIENE_COST      12     /* poop dirties the tank */
#define FEED_XP                5

#define CLEAN_HYGIENE_SET      100
/* Clean pays for how much the tank actually improved, not for the tap. The
 * score is the hygiene restored plus the dirt swept up, out of CLEAN_SCORE_FULL.
 * This replaces gating the button: a gate looked broken and could be bypassed
 * by dropping a bug in and tapping Clean repeatedly. */
#define CLEAN_XP_MAX           3
/* Score that earns the full CLEAN_XP_MAX. Two spider droppings (2 x 12), or
 * four bug speckles (4 x 6), or a mix, or a badly neglected tank's hygiene. */
#define CLEAN_SCORE_FULL       24

#define PLAY_MOOD_GAIN         10
#define PLAY_ENERGY_COST       5
#define PLAY_XP                4

#define PET_MOOD_GAIN          6
#define PET_XP                 2

/* ------------------------------------------------------------------ */
/* Daily loop                                                          */
/* ------------------------------------------------------------------ */
/* A reason to come back tomorrow. The streak counts consecutive in-game days
 * with at least one care action, and the reward grows with it so a long streak
 * is worth protecting. Both are capped so the economy cannot run away. */
#define DAILY_STREAK_MAX       7
#define DAILY_XP_BASE          6      /* day 1 */
#define DAILY_XP_PER_DAY       2      /* +2 per consecutive day */
#define DAILY_SILK_BASE        4
#define DAILY_SILK_PER_DAY     2

/* "Never claimed". 0 is a real in-game day, so it cannot double as the
 * sentinel or a fresh pet could never claim its first check-in. */
#define DAILY_NEVER            0xFFFFFFFFu
/* One rotating task per in-game day. Completing it pays a flat bonus, so the
 * daily loop has a goal rather than only a check-in. */
#define DAILY_TASK_XP          12
#define DAILY_TASK_SILK        8
#define DAILY_TASK_COUNT       4

/* ------------------------------------------------------------------ */
/* Treats (recurring Silk sink)                                        */
/* ------------------------------------------------------------------ */
/* Gifts are permanent and one-time, so once all eighteen are owned Silk has
 * nothing to buy. Treats are consumables: they cost Silk every time and give a
 * short, visible boost. */
#define TREAT_COUNT            3
#define TREAT_HUNGER_COST      6      /* Silk */
#define TREAT_MOOD_COST        8
#define TREAT_ENERGY_COST      10
#define TREAT_HUNGER_GAIN      25
#define TREAT_MOOD_GAIN        30
#define TREAT_ENERGY_GAIN      35

/* ------------------------------------------------------------------ */
/* Durations (ms)                                                      */
/* ------------------------------------------------------------------ */
#define EAT_MS                 800
#define YUM_MS                 900
#define PET_MS                 900

/* Minimum duration of a player-initiated sleep. Without this, pressing SLEEP
 * at high energy bounced straight back to IDLE and the button looked broken. */
#define SLEEP_MIN_MS           6000u

/* How long the auto-hiding HUD chrome stays up after being revealed. */
#define CHROME_HOLD_MS         5000u

/* Height of the edge bands that reveal the chrome. The top band reveals the
 * STATS, the bottom band reveals the ACTIONS -- they are separate so reaching
 * for one never pops the other.
 *
 * 90 px was too wide: it covered a fifth of the screen and swallowed taps meant
 * for the tank. 56 px is still a comfortable swipe target on a 480 px panel
 * without eating the playfield. */
/* The edge bands are GONE. They were invisible 56px strips along the top and
 * bottom that revealed the chrome on tap, and they swallowed slingshot drags
 * that started near an edge -- which is exactly where a thumb rests. The chrome
 * is now toggled by a single dot in the top-left corner.
 *
 * The dot is small and faint so it does not compete with the tank, but the
 * TOUCH target is much larger than the dot: a 36px target was too small to hit
 * reliably on a 2.16" panel. */
#define HAMBURGER_TOUCH        64   /* invisible tap area */
#define HAMBURGER_DOT          28   /* visible translucent dot */
#define HAMBURGER_INSET        12   /* distance from the screen corner */

/* Placement preview ring radius. The gift's effect is global, so this is a
 * "here is where it goes" marker rather than an effect-radius circle. */
#define GIFT_PREVIEW_RADIUS    16
#define HOP_MS                 450
#define BLINK_MS               150
#define BLINK_MIN_MS           1500
#define BLINK_JITTER_MS        2500

/* ------------------------------------------------------------------ */
/* Spider body                                                         */
/* ------------------------------------------------------------------ */
#define BODY_R                 16

/* Pet hit radius. The body box is 68 px but the visible creature is ~32 px,
 * so a fingertip aimed at the spider often missed the box and fell through to
 * the tap layer -- which used to pet the spider from anywhere in the tank.
 * This radius is deliberately generous: petting is the friendly verb and
 * should never feel like a precision test. */
#define PET_RADIUS_PX          56
#define LEG_LEN                22.0f
#define SPIDER_SPRITE_W        96
#define SPIDER_SPRITE_H        96
#define SPIDER_PIVOT_X         48
#define SPIDER_PIVOT_Y         48

/* Movement. These are px per SECOND. The old code used per-tick values, so
 * reusing them here made the spider move 0.1 px/tick -- which truncates to
 * zero against an int16_t position and freezes it in place. */
#define MOVE_SPEED             55.0f
#define TURN_RATE_DEG          540.0f  /* max heading change per second */
#define WANDER_RETARGET_MS     2000u
#define ARRIVE_RADIUS          12.0f

/* Gait */
#define GAIT_PHASE_STEP        0.6f
#define WALK_FRAME_MS          110
#define IDLE_FRAME_MS          900
#define EAT_FRAME_MS           180
#define POUNCE_FRAME_MS        90
#define MOLT_FRAME_MS          400

/* ------------------------------------------------------------------ */
/* Prey                                                                */
/* ------------------------------------------------------------------ */
#define PREY_SPRITE_W          48
#define PREY_SPRITE_H          48
#define PREY_PIVOT_X           24
#define PREY_PIVOT_Y           24
#define PREY_WALK_FRAME_MS     140
#define PREY_FLEE_FRAME_MS     70
#define PREY_SPEED             30.0f
#define PREY_FLEE_SPEED        70.0f
#define PREY_FLEE_RADIUS       70.0f
#define PREY_WANDER_RETARGET_MS 1500u

/* Aggro radius: the distance at which a bug notices the spider and runs.
 *
 * Outside this it goes about its business -- it wanders, feeds, flutters -- and
 * ignores the spider entirely. This is deliberately modest: a bug that bolts
 * from across the tank reads as scripted rather than alive, and it also made
 * the hunt impossible because the spider could never close.
 *
 * The hysteresis multiplier is how far past the radius a fleeing bug must get
 * before it calms down. It was 1.6, which meant a bug kept sprinting until it
 * was 112px away and the tank felt permanently panicked; 1.25 lets it settle
 * soon after it clears the radius. */
#define PREY_AGGRO_RADIUS      70.0f
#define PREY_AGGRO_HYSTERESIS  1.25f

/* Wild prey: drifts in on its own, leaner and warier than what the player
 * introduces. This keeps the tank alive without feeding the spider for free. */
#define WILD_SPAWN_MS          1200000u  /* 20 min */
#define WILD_HUNGER_GAIN       8
#define WILD_MOOD_GAIN         4
#define WILD_FLEE_RADIUS_MULT  1.25f
#define WILD_FLEE_SPEED_MULT   1.15f

/* ------------------------------------------------------------------ */
/* Escalation: prey get harder as the player levels                     */
/* ------------------------------------------------------------------ */
#define PREY_SPEED_PER_LEVEL    0.06f
#define PREY_SPEED_MAX_MULT     1.8f
#define PREY_ALERT_PER_LEVEL    0.05f
#define PREY_ALERT_MAX_MULT     1.6f

/* Prey tire. Continuous fleeing is both unrealistic and a hard deadlock: at
 * high level the flee radius exceeded AIM_RANGE_PX and the flee speed
 * outran the stalk, so the spider chased forever and the aim window could
 * never open (observed: 2 min pinned in STALK). Bursts give the spider a
 * closing window and give the player a readable rhythm. */
#define PREY_FLEE_BURST_MS      300u
#define PREY_FLEE_REST_MS       1000u

/* The flee radius must stay INSIDE the aim range, or the spider is repelled
 * before it can ever crouch. */
#define PREY_FLEE_RADIUS_MAX    (AIM_RANGE_PX - 8)

/* Species-specific movement. */
#define FLY_RETARGET_MS         600u    /* erratic */
#define FLY_JITTER              0.35f   /* radians of heading noise per tick */
#define CRICKET_HOP_CYCLE_MS    900u    /* burst rhythm */
#define CRICKET_HOP_BURST_MS    200u
#define CRICKET_HOP_SPEED_MULT  2.2f
#define MOTH_FLUTTER_HZ         0.006f  /* radians per ms */
#define MOTH_FLUTTER_PX         14.0f

#define BEETLE_UNLOCK_LEVEL     6
#define BEETLE_PERFECT_MULT     0.5f    /* armoured: half the perfect radius */

#define VISITOR_XP              20
#define XP_ACHIEVEMENT          15

/* ------------------------------------------------------------------ */
/* Personality                                                         */
/* ------------------------------------------------------------------ */

/* A bored spider refuses to hunt. Chained with the mood drift this keeps the
 * verbs in a loop: play -> hunt -> sleep -> clean. */
#define MOOD_REFUSE_HUNT        20

/* Hunger beats boredom. Below this hunger value the spider hunts regardless of
 * mood, so the mood gate can never starve it into an unrecoverable SICK. */
#define MOOD_HUNGER_OVERRIDE    40

/* Bond growth and decay. */
#define BOND_PER_CATCH          2
#define BOND_PER_PERFECT        4
#define BOND_PER_PET            1
#define BOND_SICK_DECAY         1

/* Bond thresholds. */
#define BOND_TURN_TO_TAP        25
#define BOND_AIM_BONUS          50
#define BOND_AIM_BONUS_MULT     1.15f
#define BOND_GIFT               75
#define BOND_SWAY               100

/* The temperament_t type and the k_temperaments table live in game.h, which
 * is the shared contract. The table is DEFINED once in game.c -- defining it
 * here would give every translation unit its own copy. */

/* Catch probability inputs */
#define CATCH_BASE             0.55f
#define CATCH_PER_PX           0.004f  /* closer = better */
#define CATCH_ENERGY_BONUS     0.20f
#define CATCH_STAGE_BONUS      0.10f

/* ------------------------------------------------------------------ */
/* Hunting: aim window and catch quality                               */
/* ------------------------------------------------------------------ */
/* These are deliberately GENEROUS. The first pass at 1400 ms / 14 px felt
 * tight on the 480 px AMOLED: the window closed before the player could
 * react, and a 14 px perfect radius is smaller than a fingertip. Widen
 * before you narrow. If it now feels too easy, cut AIM_WINDOW_MS first. */
/* Stalk -> aim transition distance. 78 px meant the window opened when the bug
 * was already close, so the player had to tap a small, fast-moving target.
 * Opening earlier gives a bigger, slower target to aim at. */
#define AIM_RANGE_PX            110

/* Widened per the plan's "panel feels tight" table. The human reports the
 * 2.16" 480x480 panel feels tight, so these use the sanctioned steps rather
 * than invented numbers. Do NOT compensate by slowing the spider. */
/* NOTE: these no longer gate the PLAYER. The slingshot is available whenever
 * there is prey in the tank, with no countdown and no expiry. What these still
 * control is the SPIDER's own dwell: how long it crouches before lunging on its
 * own if the player does nothing. That fallback is what keeps an unattended
 * spider from starving, and it is invisible to the player -- the focus-ring
 * countdown that used to show it has been removed. */
#define AIM_WINDOW_MS           2600u  /* spider's own crouch dwell */
#define AIM_WINDOW_MIN_MS       1600u  /* floor, even at high level */
#define AIM_WINDOW_PER_LEVEL    20u    /* dwell shrinks as you level */

#define POUNCE_MAX_PX           160.0f /* longest possible leap */
#define PLAYER_POUNCE_BONUS     1.15f
#define AUTO_POUNCE_PENALTY     0.55f

/* ------------------------------------------------------------------ */
/* Aiming: golf-style direction sweep + power meter                    */
/* ------------------------------------------------------------------ */
/* Tapping a small moving bug was a precision test, not a game. Aiming is now
 * two timed taps, like an old golf game:
 *
 *   1. An arrow sweeps back and forth across an arc. Tap to lock DIRECTION.
 *   2. A power meter fills and empties. Tap to lock POWER.
 *
 * Skill is timing, not pixel accuracy, so the tap radius stops mattering.
 * The arc is centred on the bug, so a well-timed first tap already points the
 * right way -- the sweep only decides how precisely. */
/* Slingshot. The player drags back from the spider and releases; the drag
 * vector is inverted, so pulling down-left fires up-right. Pull length sets
 * power. Direct manipulation, no timing meter to read. */
#define AIM_MIN_PULL_PX         18.0f  /* shorter than this is a cancel, not a shot */

/* How close the locked power must be to the ideal for a perfect catch. */
#define AIM_POWER_PERFECT       0.10f
#define AIM_POWER_GOOD          0.22f

/* Touch targets. A fingertip is ~40 px on this panel, so the perfect radius
 * must be comfortably larger than the visual size of the bug.
 *
 * These were 28/55, which made the catch land only when the spider's MOUTH was
 * on the bug -- the body is ~34 px across, so a hit anywhere on the body still
 * measured 20-30 px from the bug centre and fell outside the perfect band. The
 * radii are now measured against the spider's whole footprint rather than a
 * point, so landing any part of the body on the bug counts. */
#define CATCH_PERFECT_PX        30.0f
#define CATCH_GOOD_PX           54.0f

/* The spider's body radius, used to measure a catch from the EDGE of the
 * spider rather than its centre.
 *
 * The catch used to be measured centre-to-bug, so only the mouth counted: the
 * body is ~40 px across, and landing any other part of it on the bug still
 * measured 20-30 px from the bug centre. Subtracting this radius means landing
 * ANY part of the body on the bug counts, which is what the player expects. */
#define SPIDER_BODY_R_PX        22.0f

/* Tap assist: a tap within this distance of a bug aims AT the bug instead of
 * at the finger. The bug is a few pixels wide and moving; demanding a direct
 * hit is a precision test rather than a game. This widens the target without
 * aiming for the player: a tap far from any bug still lands where you tapped.
 * Set to 0 to disable assist entirely. */
/* The hunt target is the whole tank, not the bug. A drag can start anywhere and
 * the shot is aimed by the pull direction, so there is no small target to hit.
 * TAP_ASSIST_PX still snaps a tap to a nearby bug for the tap-to-pounce path. */
#define TAP_ASSIST_PX           64

#define XP_PERFECT              18
#define XP_GOOD                 10
#define XP_MISS                 1
#define PERFECT_HUNGER_BONUS    5
#define PERFECT_MOOD_BONUS      4

#define STREAK_BONUS_PER        0.05f
#define STREAK_MAX              10

#define POUNCE_ENERGY_COST      3
#define EAT_ENERGY_GAIN         6

/* ------------------------------------------------------------------ */
/* Life cycle                                                          */
/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
#define STAGE_JUVENILE_DAYS    2
#define STAGE_ADULT_DAYS       5
#define MOLT_DAYS              2

/* ------------------------------------------------------------------ */
/* Progression                                                         */
/* ------------------------------------------------------------------ */
#define XP_PER_LEVEL           50
#define XP_MEAL                5
#define XP_PET                 2
#define XP_PLAY                4
/* Clean's reward is scaled by the dirt removed; see CLEAN_XP_MAX. */
#define XP_DAY                 10
#define XP_MOLT                8

/* ------------------------------------------------------------------ */
/* Layout                                                              */
/* ------------------------------------------------------------------ */
/* The 2.16" AMOLED is a squircle: the corner wedges have no pixels. */
#define UI_CORNER              40

/* The spider's body container is 68 px wide, so its visual extent is ~34 px
 * from the centre. The playfield is inset by that much and no more: the spider
 * roams the WHOLE screen, but never gets half-clipped at an edge. */
#define PLAY_MARGIN            34

#define HUD_TOP_H              130
#define STAT_BAR_H             8

/* Persistent labelled care buttons include native 48px pixel-art icons. */
#define BTN_H                  64

/* Two rows of three reserve a stable corner-safe action area. */
#define HUD_BOTTOM_H           (2 * BTN_H + 8 + UI_CORNER)

/* Stat ring diameter. 34 px was hard to read and the numbers inside were
 * cramped; 40 px matches the fingertip scale. */
#define RING_SIZE              40

/* ------------------------------------------------------------------ */
/* Audio                                                               */
/* ------------------------------------------------------------------ */
#define AUDIO_SAMPLE_RATE      16000
#define AUDIO_VOLUME           70
#define SFX_MS_EAT             140
#define SFX_MS_POUNCE          90
#define SFX_MS_MOLT            260
#define SFX_MS_TAP             60
#define SFX_MS_ALERT           180
#define SFX_MS_AIM             90
#define SFX_MS_PERFECT         160

/* ------------------------------------------------------------------ */
/* Save format                                                         */
/* ------------------------------------------------------------------ */
#define SAVE_MAGIC             0x53504944u   /* "SPID" */
/* v3 uses two former padding bytes for opt-in automation. v2 progress is
 * retained, with both flags cleared; unsupported v1 layouts remain rejected. */
#define SAVE_VERSION           6
#define SAVE_NAMESPACE         "spidarium"
#define SAVE_KEY               "st"
