/*
 * Spidarium — the spider's mind, implementation.
 *
 * ADD A BEHAVIOUR HERE: one row in k_mind_rules[]. Nothing else changes.
 */
#include "mind.h"

#include "game.h"
#include "game_config.h"
#include "prey.h"

/* How often the mind re-evaluates. A few times a minute is enough for the
 * spider to feel deliberate; running it per tick would just make it twitchy. */
#define MIND_PERIOD_MS 4000u

/* The rule table.
 *
 * Rules are evaluated in order; the highest weight whose conditions all hold
 * wins. Ties go to the earlier row, so put the most urgent needs first.
 *
 * Columns: name, want, weight, hunger<, energy<, mood<, hygiene<, bond>, prey */
const mind_rule_t k_mind_rules[MIND_RULE_COUNT] = {
    /* Starving beats everything. */
    { "starving",  WANT_FOOD,    100,  25, -1, -1, -1, -1, -1 },

    /* Exhausted. */
    { "exhausted", WANT_REST,     90,  -1, 20, -1, -1, -1, -1 },

    /* Hungry and there is something to chase. */
    { "hungry",    WANT_FOOD,     70,  60, -1, -1, -1, -1,  1 },

    /* Hungry but nothing to chase: go looking. */
    { "peckish",   WANT_EXPLORE,  50,  60, -1, -1, -1, -1,  0 },

    /* Miserable. */
    { "sad",       WANT_PLAY,     60,  -1, -1, 25, -1, -1, -1 },

    /* Filthy tank. */
    { "grimy",     WANT_CLEAN,    55,  -1, -1, -1, 25, -1, -1 },

    /* Bonded and content: hang out near the player. */
    { "content",   WANT_WATCH,    30,  -1, -1, 60, -1, 50, -1 },

    /* Nothing pressing: potter about. */
    { "curious",   WANT_EXPLORE,  20,  -1, -1, -1, -1, -1, -1 },

    /* Fallback: do nothing in particular. */
    { "idle",      WANT_NOTHING,   1,  -1, -1, -1, -1, -1, -1 },
};

static want_t            s_want;
static const mind_rule_t *s_active;
static uint32_t          s_next_eval;

/* Short phrases for the HUD. Indexed by want_t. */
static const char *const k_thoughts[WANT_COUNT] = {
    "",              /* WANT_NOTHING */
    "hungry",        /* WANT_FOOD */
    "sleepy",        /* WANT_REST */
    "wants to play", /* WANT_PLAY */
    "feels grubby",  /* WANT_CLEAN */
    "exploring",     /* WANT_EXPLORE */
    "watching you",  /* WANT_WATCH */
    "hiding",        /* WANT_HIDE */
};

static bool rule_matches(const mind_rule_t *r)
{
    if (r->hunger_below  >= 0 && (int)g.hunger  >= r->hunger_below)  return false;
    if (r->energy_below  >= 0 && (int)g.energy  >= r->energy_below)  return false;
    if (r->mood_below    >= 0 && (int)g.mood    >= r->mood_below)    return false;
    if (r->hygiene_below >= 0 && (int)g.hygiene >= r->hygiene_below) return false;
    if (r->bond_above    >= 0 && (int)g.bond    <= r->bond_above)    return false;

    if (r->prey_present == 1 && prey_count() == 0) return false;
    if (r->prey_present == 0 && prey_count() >  0) return false;

    return true;
}

void mind_tick(uint32_t now_ms)
{
    if ((int32_t)(now_ms - s_next_eval) < 0) return;
    s_next_eval = now_ms + MIND_PERIOD_MS;

    const mind_rule_t *best = NULL;

    for (int i = 0; i < MIND_RULE_COUNT; i++) {
        const mind_rule_t *r = &k_mind_rules[i];
        if (!rule_matches(r)) continue;
        if (!best || r->weight > best->weight) best = r;
    }

    s_active = best;
    s_want   = best ? best->want : WANT_NOTHING;
}

want_t mind_want(void)
{
    return s_want;
}

const char *mind_thought(void)
{
    if (s_want >= WANT_COUNT) return "";
    return k_thoughts[s_want];
}

const mind_rule_t *mind_active_rule(void)
{
    return s_active;
}
