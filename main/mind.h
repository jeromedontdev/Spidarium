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
 * behaviour.c may consult, so the existing safety guarantees (never stuck,
 * never dies) still hold.
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
    WANT_COUNT,
} want_t;

/* A rule: if every condition holds, the spider wants `want` with `weight`.
 * Higher weight wins. Conditions are simple threshold tests so the table stays
 * readable and adding a rule needs no new code. */
typedef struct {
    const char *name;
    want_t      want;
    int         weight;

    /* Conditions. -1 means "ignore this condition". */
    int hunger_below;    /* fire if hunger < this */
    int energy_below;    /* fire if energy < this */
    int mood_below;      /* fire if mood < this */
    int hygiene_below;   /* fire if hygiene < this */
    int bond_above;      /* fire if bond > this */
    int prey_present;    /* 1 = require prey, 0 = require none, -1 = ignore */
} mind_rule_t;

#define MIND_RULE_COUNT 9

/* The rule table. Add a row to add a behaviour. */
extern const mind_rule_t k_mind_rules[MIND_RULE_COUNT];

/* Re-evaluate the spider's intent. Call a few times a minute, not per tick. */
void mind_tick(uint32_t now_ms);

/* What the spider currently wants. */
want_t mind_want(void);

/* A short phrase describing the current intent, for the HUD. */
const char *mind_thought(void);

/* The rule that produced the current intent, or NULL. */
const mind_rule_t *mind_active_rule(void);
