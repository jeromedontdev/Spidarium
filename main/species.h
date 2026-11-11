/*
 * Spidarium — species and gift catalogue.
 *
 * Gameplay values are table-driven; IDs and sprite assets must also match.
 *
 * To add a bug:
 *   1. Append an ID in game.h and matching k_species[] entry in species.c.
 *   2. Generate five pixel-art frames and add their sprites.c mapping.
 *   3. Update asset validation/inventory and test unlock boundaries.
 *
 * To add a gift:
 *   1. Add an entry to k_gifts[] in species.c.
 *   2. Keep gift IDs, generated art, and sprite mappings aligned.
 *
 * The enums in game.h still exist because the save format and the renderer
 * index by them, but the ORDER of the tables must match the enum order.
 */
#pragma once

#include "game.h"

#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Bugs                                                                */
/* ------------------------------------------------------------------ */

/* How a bug moves. The renderer and the tick both switch on this, so a new
 * movement style is one enum value plus one case in each. */
typedef enum {
    MOVE_WANDER = 0,   /* steady drift, occasional retarget */
    MOVE_ERRATIC,      /* constant heading jitter (fly) */
    MOVE_HOP,          /* burst then still (cricket) */
    MOVE_FLUTTER,      /* sinusoidal lateral offset (moth) */
    MOVE_SLOW,         /* slow and steady (beetle) */
} move_style_t;

typedef struct {
    const char  *name;
    uint32_t     unlock_level;
    move_style_t move;
    float        speed_mult;      /* multiplies PREY_SPEED */
    float        flee_speed_mult; /* multiplies PREY_FLEE_SPEED */
    float        flee_radius_mult;/* multiplies PREY_FLEE_RADIUS */
    float        perfect_mult;    /* multiplies CATCH_PERFECT_PX (armour) */
    uint32_t     body_col;        /* primary body colour */
    uint32_t     dark_col;        /* head/dark accent */
    uint32_t     wing_col;        /* wings, if any */
    uint8_t      body_r;          /* primary body radius */
    uint8_t      head_r;          /* head radius */
    bool         has_wings;
    bool         has_hind_legs;
    bool         has_seam;        /* beetle wing-case seam */

    /* Nutrition. A waxworm is a meal; a fruit fly is a snack. Without this a
     * level-20 spider got exactly as much from the easiest bug in the game as
     * from the hardest, so the unlock ladder paid nothing but variety. These
     * multiply the base gains, so the constants stay the tuning surface. */
    float        hunger_mult;
    float        mood_mult;
    float        energy_mult;
} species_def_t;

/* The catalogue. Indexed by prey_species_t. */
extern const species_def_t k_species[PREY_SPECIES_COUNT];

/* Look up a species definition, clamped to a valid index. */
const species_def_t *species_get(prey_species_t species);
bool species_unlocked(prey_species_t species);
prey_species_t species_choose_unlocked(uint32_t roll);

/* Index of the next species the player has NOT unlocked, or -1 if all are
 * unlocked. This is what the status page needs; prey_unlocked_species()
 * returns a COUNT, which is a different thing. */
int species_next_locked(void);

/* ------------------------------------------------------------------ */
/* Gifts                                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *name;
    const char *effect;        /* plain words for the status page */
    uint32_t    unlock_level;
    uint32_t    colour;
    /* Multipliers. 1.0 = no effect. */
    float       energy_regen_mult;
    float       hygiene_decay_mult;
    float       pounce_range_mult;
    float       aim_window_mult;
    int         wild_spawn_bonus;
    /* New fields (2026-11-07 expansion) */
    float       pounce_success_mult;
    float       movement_efficiency_mult;
    float       mood_mult;
    float       fear_mult;
    float       prey_flee_penalty;
    float       catch_success_mult;
    float       prey_detection_range_mult;
    float       all_stats_decay_mult;
    float       bonus_synergy_mult;
} gift_def_t;

/* The catalogue. Indexed by gift kind. */
extern const gift_def_t k_gifts[GIFT_KINDS];

/* Look up a gift definition, clamped to a valid index. */
const gift_def_t *gift_def(uint8_t kind);
