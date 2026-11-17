/*
 * Spidarium — the spider's mind, implementation.
 *
 * ADD A BEHAVIOUR HERE: one row in k_mind_rules[]. Nothing else changes.
 */
#include "mind.h"

#include <string.h>

#include "decor.h"
#include "game.h"
#include "game_config.h"
#include "life.h"
#include "prey.h"

/* How often the mind re-evaluates. A few times a minute is enough for the
 * spider to feel deliberate; running it per tick would just make it twitchy. */
#define MIND_PERIOD_MS 4000u

/* The rule table.
 *
 * Rules are evaluated in order; the highest weight whose conditions all hold
 * wins. Ties go to the earlier row, so put the most urgent needs first.
 *
 * Columns: name, want, weight, hunger<, energy<, mood<, hygiene<, bond>,
 *          prey, period, climbable, gift
 *
 * The last three columns are what make the spider feel like it has a LIFE
 * rather than a stat readout: it notices the time of day, the things you have
 * put in its tank, and the gift you gave it. A rule whose condition cannot be
 * satisfied (no clock, no climbable decor, no gift) simply never fires, so the
 * table degrades cleanly on a fresh tank.
 */
const mind_rule_t k_mind_rules[MIND_RULE_COUNT] = {
    /* --- survival. These must never be outranked. --- */
    /* Starving beats everything. */
    { "starving",  WANT_FOOD,    100,  25, -1, -1, -1, -1, -1, -1, -1, -1 },

    /* Exhausted. */
    { "exhausted", WANT_REST,     95,  -1, 20, -1, -1, -1, -1, -1, -1, -1 },

    /* --- needs with something to act on. --- */
    /* Hungry and there is something to chase. */
    { "hungry",    WANT_FOOD,     70,  60, -1, -1, -1, -1,  1, -1, -1, -1 },

    /* Hungry but nothing to chase: go looking. */
    { "peckish",   WANT_EXPLORE,  50,  60, -1, -1, -1, -1,  0, -1, -1, -1 },

    /* Miserable. */
    { "sad",       WANT_PLAY,     60,  -1, -1, 25, -1, -1, -1, -1, -1, -1 },

    /* Filthy tank. */
    { "grimy",     WANT_CLEAN,    55,  -1, -1, -1, 25, -1, -1, -1, -1, -1 },

    /* --- personality. These are what the player actually watches. --- */
    /* Night: a jumping spider is crepuscular, so it settles down. This is
     * deliberately below the survival rules but above every comfort rule, so a
     * fed spider sleeps at night and a starving one still eats. */
    { "drowsy",    WANT_REST,     45,  -1, 70, -1, -1, -1, -1,  3, -1, -1 },

    /* Evening is when it is most active, so it goes exploring. */
    { "dusk",      WANT_EXPLORE,  35,  -1, -1, -1, -1, -1, -1,  2, -1, -1 },

    /* Morning: bask. A spider that is otherwise comfortable will take the sun,
     * which is the calmest thing it does. There is deliberately no mood
     * condition: the higher-weight `sad` and `grimy` rules already cover a
     * miserable spider, so basking only wins when nothing is wrong. */
    { "basking",   WANT_SUN,      32,  -1, -1, -1, -1, -1, -1,  0, -1, -1 },

    /* Bonded and content: hang out near the player. */
    { "content",   WANT_WATCH,    30,  -1, -1, 60, -1, 50, -1, -1, -1, -1 },

    /* Something tall to climb and the energy to do it. Climbing is the most
     * spectacular thing in the tank, so it should happen unprompted. */
    { "restless",  WANT_CLIMB,    28,  -1, 55, 45, -1, -1, -1, -1,  1, -1 },

    /* A small prop to sit on. Cheaper than climbing, so it happens more. */
    { "perching",  WANT_PERCH,    26,  -1, 40, 40, -1, -1, -1, -1, -1, -1 },

    /* The gift you gave it. A bonded spider visits more often, which is the
     * visible payoff for the bond track. */
    { "fond",      WANT_VISIT,    24,  -1, 30, 50, -1, 20, -1, -1, -1,  1 },

    /* Nothing pressing: potter about. */
    { "curious",   WANT_EXPLORE,  20,  -1, -1, -1, -1, -1, -1, -1, -1, -1 },

    /* --- fallbacks. --- */
    /* Tired but not exhausted: rest rather than wander. */
    { "weary",     WANT_REST,     15,  -1, 35, -1, -1, -1, -1, -1, -1, -1 },

    /* A shy spider with low bond hides rather than exploring. */
    { "shy",       WANT_HIDE,     14,  -1, -1, -1, -1, 10, -1, -1, -1, -1 },

    /* Idle fidget: something to do when nothing else applies. */
    { "fidget",    WANT_PERCH,     8,  -1, -1, -1, -1, -1, -1, -1, -1, -1 },

    /* Fallback: do nothing in particular. */
    { "idle",      WANT_NOTHING,   1,  -1, -1, -1, -1, -1, -1, -1, -1, -1 },

    /* Reserved tail rows. Kept so MIND_RULE_COUNT can grow without renumbering
     * anything a save or a test might reference. They never match. */
    { "reserved0", WANT_NOTHING,  -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1 },
    { "reserved1", WANT_NOTHING,  -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1 },
};

static want_t            s_want;
static const mind_rule_t *s_active;
static uint32_t          s_next_eval;
static uint32_t          s_since;

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
    "wants to climb",/* WANT_CLIMB */
    "looking for a perch", /* WANT_PERCH */
    "visiting its gift",   /* WANT_VISIT */
    "basking",       /* WANT_SUN */
};

/* Cached environment probes. decor_placed_count() walks the slot array, so it
 * is not something to call per rule per evaluation. */
static int s_climbable = -1;
static int s_gift_used = -1;

static void refresh_environment(void)
{
    s_climbable = 0;
    int slots = decor_slot_count();
    for (int i = 0; i < slots; i++) {
        const decor_item_t *item = decor_at(i);
        if (item && decor_climbable(item->kind)) { s_climbable = 1; break; }
    }
    s_gift_used = g.gift.used ? 1 : 0;
}

static bool rule_matches(const mind_rule_t *r)
{
    if (r->weight < 0) return false;

    if (r->hunger_below  >= 0 && (int)g.hunger  >= r->hunger_below)  return false;
    if (r->energy_below  >= 0 && (int)g.energy  >= r->energy_below)  return false;
    if (r->mood_below    >= 0 && (int)g.mood    >= r->mood_below)    return false;
    if (r->hygiene_below >= 0 && (int)g.hygiene >= r->hygiene_below) return false;
    if (r->bond_above    >= 0 && (int)g.bond    <= r->bond_above)    return false;

    if (r->prey_present == 1 && prey_count() == 0) return false;
    if (r->prey_present == 0 && prey_count() >  0) return false;

    /* A period rule needs a real clock. Without one the period is 255, which
     * matches no specific period, so time-of-day behaviour is simply absent
     * rather than wrong. */
    if (r->period >= 0 && (int)life_period_now() != r->period) return false;

    if (r->climbable == 1 && !s_climbable) return false;
    if (r->climbable == 0 &&  s_climbable) return false;

    if (r->gift_placed == 1 && !s_gift_used) return false;
    if (r->gift_placed == 0 &&  s_gift_used) return false;

    return true;
}

void mind_tick(uint32_t now_ms)
{
    if ((int32_t)(now_ms - s_next_eval) < 0) return;
    s_next_eval = now_ms + MIND_PERIOD_MS;

    refresh_environment();

    const mind_rule_t *best = NULL;

    for (int i = 0; i < MIND_RULE_COUNT; i++) {
        const mind_rule_t *r = &k_mind_rules[i];
        if (!rule_matches(r)) continue;
        if (!best || r->weight > best->weight) best = r;
    }

    want_t want = best ? best->want : WANT_NOTHING;
    /* Only restart the dwell timer when the INTENT changes, not when the rule
     * that produced it changes. Two rules can both want WANT_EXPLORE, and
     * resetting the timer as they swap would keep the spider from ever acting
     * on either. */
    if (want != s_want) s_since = now_ms;

    s_active = best;
    s_want   = want;
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

/* Stable identifiers. These are what the console and the tests use, so they
 * must not be reworded for flavour -- k_thoughts is the player-facing text. */
static const char *const k_want_names[WANT_COUNT] = {
    "nothing", "food", "rest", "play", "clean", "explore", "watch", "hide",
    "climb", "perch", "visit", "sun",
};

const char *mind_want_name(want_t want)
{
    if (want >= WANT_COUNT) return "unknown";
    return k_want_names[want];
}

const mind_rule_t *mind_active_rule(void)
{
    return s_active;
}

uint32_t mind_held_ms(uint32_t now_ms)
{
    return now_ms - s_since;
}

void mind_wake(void)
{
    s_next_eval = 0;
}

bool mind_test(void)
{
    bool ok = true;

    /* Every row must be well formed: a real want, and a weight that is either
     * negative (disabled) or positive. A zero weight would tie with nothing and
     * silently never win, which is a rule that looks live but is not. */
    for (int i = 0; i < MIND_RULE_COUNT; i++) {
        const mind_rule_t *r = &k_mind_rules[i];
        if (!r->name || !r->name[0]) { ok = false; break; }
        if (r->want >= WANT_COUNT) { ok = false; break; }
        if (r->weight == 0) { ok = false; break; }
        if (r->period >= 0 && r->period > 3) { ok = false; break; }
    }

    /* The survival rules must outrank every comfort rule, or a comfortable
     * spider could starve while admiring the view. */
    int starving = -1, exhausted = -1, best_comfort = -1;
    for (int i = 0; i < MIND_RULE_COUNT; i++) {
        const mind_rule_t *r = &k_mind_rules[i];
        if (r->weight < 0) continue;
        if (!strcmp(r->name, "starving"))  starving  = r->weight;
        if (!strcmp(r->name, "exhausted")) exhausted = r->weight;
        bool survival = !strcmp(r->name, "starving") || !strcmp(r->name, "exhausted") ||
                        !strcmp(r->name, "hungry")   || !strcmp(r->name, "peckish") ||
                        !strcmp(r->name, "sad")      || !strcmp(r->name, "grimy");
        if (!survival && r->weight > best_comfort) best_comfort = r->weight;
    }
    ok = ok && starving > 0 && exhausted > 0 && best_comfort > 0;
    ok = ok && starving > best_comfort && exhausted > best_comfort;

    /* The dwell timer must not restart when the intent is unchanged.
     *
     * This drives the live engine, so the whole state has to be saved and put
     * back: a diagnostic that left s_want at WANT_NOTHING and s_since at zero
     * would freeze the spider's intent until the next evaluation, and if the
     * tank happened to be hidden (so mind_tick() is not called) it would stay
     * frozen indefinitely. */
    want_t saved_want = s_want;
    const mind_rule_t *saved_active = s_active;
    uint32_t saved_since = s_since, saved_next = s_next_eval;

    s_want = WANT_NOTHING; s_since = 0; s_next_eval = 0;
    mind_tick(1000);
    want_t first = s_want;
    uint32_t since = s_since;
    s_next_eval = 0;
    mind_tick(2000);
    ok = ok && s_want == first && s_since == since;

    s_want = saved_want;
    s_active = saved_active;
    s_since = saved_since;
    s_next_eval = saved_next;

    return ok;
}
