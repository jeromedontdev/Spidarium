/*
 * Spidarium — the spider's mind.
 *
 * A small rules engine that decides what the spider WANTS to do, so it reads
 * as a creature with intentions rather than a state machine reacting to
 * thresholds. It runs at a low rate (a few times a minute) and produces a
 * "mood thought" plus a preferred activity.
 *
 * Why rules and not an LLM: this is a 240 MHz microcontroller with no network
 * and a few hundred KB of RAM. A rules engine is deterministic, costs nothing
 * to run, and is trivially extensible -- adding a behaviour is one row in the
 * table in mind.c.
 *
 * The engine does NOT drive the FSM directly. It publishes an intent that
 * behaviour.c consults, so the existing safety guarantees (never stuck, never
 * dies) still hold. behaviour.c is the only place that may override an intent,
 * and it does so only for the needs that must never be ignored.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* What the spider currently wants. */
typedef enum {
    WANT_NOTHING = 0,
    WANT_FOOD,
    WANT_REST,
    WANT_PLAY,
    WANT_CLEAN,
    WANT_EXPLORE,
    WANT_WATCH,      /* sit still and observe */
    WANT_HIDE,       /* retreat to a corner */
    WANT_CLIMB,      /* go up something tall */
    WANT_PERCH,      /* sit on a small prop */
    WANT_VISIT,      /* go and see the gift */
    WANT_SUN,        /* bask in the warm part of the day */
    WANT_COUNT,
} want_t;

/* A rule: if every condition holds, the spider wants `want` with `weight`.
 * Higher weight wins. Conditions are simple threshold tests so the table stays
 * readable and adding a rule needs no new code.
 *
 * Every condition is optional: -1 means "ignore this condition". */
typedef struct {
    const char *name;
    want_t      want;
    int         weight;

    /* Stat thresholds. Fire when the stat is BELOW the value. */
    int hunger_below;
    int energy_below;
    int mood_below;
    int hygiene_below;

    /* Fire when bond is ABOVE the value. */
    int bond_above;

    /* Prey: 1 = require prey present, 0 = require none, -1 = ignore. */
    int prey_present;

    /* Day period (0 morning, 1 afternoon, 2 evening, 3 night): -1 = ignore.
     * 255 (clock unavailable) never matches a specific period, so a rule that
     * depends on the time of day simply does not fire without a clock. */
    int period;

    /* Climbable decor: 1 = require at least one placed, 0 = require none,
     * -1 = ignore. */
    int climbable;

    /* The gift: 1 = require one placed, 0 = require none, -1 = ignore. */
    int gift_placed;
} mind_rule_t;

#define MIND_RULE_COUNT 20

/* The rule table. Add a row to add a behaviour. */
extern const mind_rule_t k_mind_rules[MIND_RULE_COUNT];

/* Re-evaluate the spider's intent. Call a few times a minute, not per tick. */
void mind_tick(uint32_t now_ms);

/* What the spider currently wants. */
want_t mind_want(void);

/* A short phrase describing the current intent, for the HUD. */
const char *mind_thought(void);

/* The stable identifier for a want, for logs and the console. Unlike
 * mind_thought() this never changes wording, so it is safe to assert on. */
const char *mind_want_name(want_t want);

/* The rule that produced the current intent, or NULL. */
const mind_rule_t *mind_active_rule(void);

/* How long the current intent has been held, in milliseconds. A creature that
 * changes its mind every four seconds reads as twitchy, so behaviour.c uses
 * this to give an intent a minimum dwell time before acting on it. */
uint32_t mind_held_ms(uint32_t now_ms);

/* Force a re-evaluation on the next mind_tick(), so a state change (being fed,
 * being petted) is reflected immediately instead of up to four seconds later. */
void mind_wake(void);

/* Self-test: the table is well formed and the intent is stable. */
bool mind_test(void);
