/*
 * Spidarium — species and gift catalogue, definitions.
 *
 * THIS IS THE FILE TO EDIT when adding content. Each table entry is one bug or
 * one gift; nothing else needs to change.
 */
#include "species.h"

#include "game_config.h"

/* ------------------------------------------------------------------ */
/* Bugs                                                                */
/* ------------------------------------------------------------------ */
/* Order MUST match prey_species_t in game.h. */
const species_def_t k_species[PREY_SPECIES_COUNT] = {
    /* name       lvl  move          spd   flee  rad   perf  body      dark      wing      br  hr  wings legs  seam */
    { "fly",       1, MOVE_ERRATIC, 1.00f, 1.00f, 1.00f, 1.00f,
      0x6b5a48, 0x3f352a, 0x8a8478, 5, 3, true,  false, false },

    { "cricket",   2, MOVE_HOP,     1.00f, 1.00f, 1.00f, 1.00f,
      0x6b5a48, 0x3f352a, 0x8a8478, 5, 3, false, true,  false },

    { "moth",      4, MOVE_FLUTTER, 1.00f, 1.00f, 1.00f, 1.00f,
      0x6b5a48, 0x3f352a, 0x8a8478, 4, 3, true,  false, false },

    { "beetle",    6, MOVE_SLOW,    0.55f, 1.00f, 1.00f, 0.50f,
      0x2b2620, 0x3f352a, 0x3a332b, 7, 3, false, false, true },
};

const species_def_t *species_get(prey_species_t species)
{
    int i = (int)species;
    if (i < 0 || i >= PREY_SPECIES_COUNT) i = 0;
    return &k_species[i];
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
/* Order MUST match the gift kind indices used by gift.c and the renderer. */
const gift_def_t k_gifts[GIFT_KINDS] = {
    /* name       effect              lvl  colour     eR     hD     pR     aW     wild */
    { "pebble",  "rests faster",       1, 0x58d68d, 1.50f, 1.00f, 1.00f, 1.00f, 0 },
    { "leaf",    "stays cleaner",      2, 0x7c9cff, 1.00f, 0.60f, 1.00f, 1.00f, 0 },
    { "twig",    "leaps further",      3, 0xf5b342, 1.00f, 1.00f, 1.20f, 1.00f, 0 },
    { "flower",  "more wild bugs",     5, 0xff7eb6, 1.00f, 1.00f, 1.00f, 1.00f, 1 },
    { "crystal", "longer aim window",  7, 0x4fd1c5, 1.00f, 1.00f, 1.00f, 1.25f, 0 },
};

const gift_def_t *gift_def(uint8_t kind)
{
    return &k_gifts[kind % GIFT_KINDS];
}
