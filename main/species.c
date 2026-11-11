/*
 * Spidarium — species and gift catalogue, definitions.
 *
 * Gameplay values live here; append matching IDs/art/mappings for new content.
 */
#include "species.h"

#include "game_config.h"

/* ------------------------------------------------------------------ */
/* Bugs                                                                */
/* ------------------------------------------------------------------ */
/* Order MUST match prey_species_t in game.h.
 *
 * Nutrition scales with the unlock ladder: a fruit fly is a snack, a waxworm is
 * a meal. The multipliers are applied to FEED_HUNGER_GAIN / FEED_MOOD_GAIN /
 * EAT_ENERGY_GAIN, so the base constants remain the single tuning surface.
 * Energy is the flattest of the three because a meal's job is to pay back the
 * pounce, and a bug that left the spider hungrier than before would be a trap
 * rather than a reward. */
const species_def_t k_species[PREY_SPECIES_COUNT] = {
    /* name       lvl  move          spd   flee  rad   perf  body      dark      wing      br  hr  wings legs  seam  hun  mood en */
    { "fly",       1, MOVE_ERRATIC, 1.00f, 1.00f, 1.00f, 1.00f,
      0x6b5a48, 0x3f352a, 0x8a8478, 5, 3, true,  false, false,
      1.00f, 1.00f, 1.00f },

    { "cricket",   2, MOVE_HOP,     1.00f, 1.00f, 1.00f, 1.00f,
      0x6b5a48, 0x3f352a, 0x8a8478, 5, 3, false, true,  false,
      1.20f, 1.15f, 1.10f },

    { "moth",      4, MOVE_FLUTTER, 1.00f, 1.00f, 1.00f, 1.00f,
      0x6b5a48, 0x3f352a, 0x8a8478, 4, 3, true,  false, false,
      1.10f, 1.30f, 1.00f },

    { "beetle",    6, MOVE_SLOW,    0.55f, 1.00f, 1.00f, 0.50f,
      0x2b2620, 0x3f352a, 0x3a332b, 7, 3, false, false, true,
      1.60f, 1.10f, 1.30f },
    { "roach",    10, MOVE_WANDER, 0.75f, 1.25f, 0.85f, 0.85f,
      0x936044, 0x493329, 0xb58158, 7, 3, false, true, true,
      1.50f, 1.00f, 1.20f },
    { "mealworm", 13, MOVE_SLOW,   0.30f, 0.40f, 0.65f, 1.10f,
      0xc7a267, 0x745138, 0xe5c38e, 6, 2, false, false, false,
      1.90f, 1.20f, 1.40f },
    { "fruit fly",16, MOVE_ERRATIC,1.25f, 1.20f, 1.10f, 0.85f,
      0xb68c58, 0x58362f, 0xc5d4c6, 3, 2, true, false, false,
      0.70f, 1.40f, 0.80f },
    { "waxworm",  20, MOVE_SLOW,   0.20f, 0.30f, 0.55f, 1.20f,
      0xdfd5ac, 0x8a6f4e, 0xf1e7c7, 7, 2, false, false, false,
      2.40f, 1.50f, 1.60f },
};

const species_def_t *species_get(prey_species_t species)
{
    int i = (int)species;
    if (i < 0 || i >= PREY_SPECIES_COUNT) i = 0;
    return &k_species[i];
}
bool species_unlocked(prey_species_t species)
{
    return species >= 0 && species < PREY_SPECIES_COUNT &&
        g.level >= k_species[species].unlock_level;
}

prey_species_t species_choose_unlocked(uint32_t roll)
{
    int count = 0;
    for (int i = 0; i < PREY_SPECIES_COUNT; i++)
        if (species_unlocked(i)) count++;
    if (count == 0) return PREY_FLY;
    int choice = roll % count;
    for (int i = 0; i < PREY_SPECIES_COUNT; i++)
        if (species_unlocked(i) && choice-- == 0) return (prey_species_t)i;
    return PREY_FLY;
}

int species_next_locked(void)
{
    /* Walk the table in unlock order and return the first one still locked.
     * This is what the status page needs -- prey_unlocked_species() returns a
     * COUNT of unlocked species, which is a different quantity and was the
     * cause of the bogus "all species unlocked" message. */
    int best = -1;
    uint32_t best_lvl = 0xFFFFFFFFu;

    for (int i = 0; i < PREY_SPECIES_COUNT; i++) {
        if (g.level >= k_species[i].unlock_level) continue;
        if (k_species[i].unlock_level < best_lvl) {
            best_lvl = k_species[i].unlock_level;
            best = i;
        }
    }
    return best;
}

/* ------------------------------------------------------------------ */
/* Gifts                                                               */
/* ------------------------------------------------------------------ */
/* Order MUST match the gift kind indices used by gift.c and the renderer.
 * Expanded from 5 to 18 gifts (2026-11-07). */
const gift_def_t k_gifts[GIFT_KINDS] = {
    /* Tier 1-3: Common (The Basics) */
    /* name       effect              lvl  colour     eR     hD     pR     aW     wild  pS     mE     mo     fe     pf     cS     pr     all    syn */
    { "pebble",  "rests faster",       1, 0x58d68d, 1.50f, 1.00f, 1.00f, 1.00f, 0,    1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f },
    { "leaf",    "stays cleaner",      2, 0x7c9cff, 1.00f, 0.60f, 1.00f, 1.00f, 0,    1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f },
    { "twig",    "leaps further",      3, 0xf5b342, 1.00f, 1.00f, 1.20f, 1.00f, 0,    1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f },
    { "acorn",   "builds energy",      2, 0xc9a876, 1.30f, 1.00f, 1.00f, 1.00f, 0,    1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 0.95f, 1.00f },
    { "moss",    "natural camouflage", 1, 0x6b8e23, 1.00f, 1.00f, 1.00f, 1.00f, 0,    1.15f, 1.00f, 1.00f, 1.00f, 0.85f, 1.00f, 1.00f, 1.00f, 1.00f },
    { "sap",     "uses less energy",   3, 0xd4a574, 1.00f, 1.00f, 1.00f, 1.00f, 0,    1.00f, 1.10f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f },

    /* Tier 4-6: Uncommon (Specialization) */
    { "flower",  "more wild bugs",     5, 0xff7eb6, 1.00f, 1.00f, 1.00f, 1.00f, 1,    1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f },
    { "crystal", "longer aim window",  7, 0x4fd1c5, 1.00f, 1.00f, 1.00f, 1.25f, 0,    1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f },
    { "amber",   "warmth bonus",       4, 0xffb347, 1.00f, 1.00f, 1.00f, 1.00f, 0,    1.00f, 1.00f, 1.10f, 1.00f, 1.00f, 1.00f, 1.00f, 0.95f, 1.00f },
    { "coral",   "less grime",         5, 0xff6b9d, 1.00f, 0.80f, 1.00f, 1.00f, 0,    1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f },
    { "silk",    "steadier catches",   6, 0xe6e6fa, 1.00f, 1.00f, 1.00f, 1.00f, 0,    1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.20f, 1.00f, 1.00f, 1.00f },
    { "bone",    "calmer prey",        4, 0xf5f5dc, 1.00f, 1.00f, 1.00f, 1.00f, 0,    1.00f, 1.00f, 1.00f, 0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f },

    /* Tier 7-10: Rare (Mastery) */
    { "pearl",   "iridescent glow",    7, 0xfdeef4, 1.00f, 1.00f, 1.00f, 1.00f, 0,    1.00f, 1.00f, 1.10f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f },
    { "ruby",    "aggressive bonus",   8, 0xe0115f, 1.00f, 1.00f, 1.25f, 1.00f, 0,    1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.15f, 1.00f, 1.00f, 1.00f },
    { "sapphire","clarity",            9, 0x0f52ba, 1.00f, 1.00f, 1.00f, 1.15f, 0,    1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.20f, 1.00f, 1.00f },
    { "emerald", "vitality",           10, 0x50c878, 1.05f, 1.00f, 1.00f, 1.00f, 0,    1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 0.95f, 1.00f },

    /* Tier 11+: Legendary (Endgame) */
    { "diamond", "perfection",         12, 0xb9f2ff, 1.00f, 1.00f, 1.00f, 1.00f, 0,    1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.50f },
    { "crown",   "ruler vivarium",     15, 0xffd700, 1.00f, 1.00f, 1.00f, 1.00f, 0,    1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 2.00f },
};

const gift_def_t *gift_def(uint8_t kind)
{
    return &k_gifts[kind % GIFT_KINDS];
}
