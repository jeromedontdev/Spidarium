/*
 * Spidarium — the gift.
 *
 * There is exactly ONE gift in the tank at a time. The player gives it, and the
 * spider keeps it until the player replaces it. This replaces the old 12-slot
 * decor system: placing a dozen items was fiddly on a 2.16" panel and read as
 * clutter rather than as care.
 *
 * Each kind does one legible thing. Unlike the old decor, the gift's effect is
 * GLOBAL — there is only one, so proximity gating would just mean the spider
 * spends most of its time not benefiting from the thing the player gave it.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float energy_regen_mult;   /* pebble, acorn */
    float hygiene_decay_mult;  /* leaf */
    float pounce_range_mult;   /* twig, ruby */
    float aim_window_mult;     /* crystal, sapphire */
    int   wild_spawn_bonus;    /* flower */
    /* New multipliers (2026-11-07 expansion) */
    float pounce_success_mult; /* moss */
    float movement_efficiency_mult; /* sap */
    float mood_mult;           /* pearl */
    float fear_mult;           /* bone */
    float prey_flee_penalty;   /* moss: below 1 = fleeing slowed */
    float catch_success_mult;  /* ruby */
    float prey_detection_range_mult; /* sapphire */
    float all_stats_decay_mult; /* emerald: slows decay of all needs */
    float bonus_synergy_mult;   /* diamond, crown: multiplicative bonus */
} gift_bonus_t;

/* Recompute the active bonus from the current gift. Call once per logic tick,
 * before needs_tick(). */
void gift_compute(void);

/* The bonus computed by the last gift_compute(). Never NULL. */
const gift_bonus_t *gift_bonus(void);

/* True if a gift is currently in the tank. */
bool gift_present(void);

/* The kind of the current gift, or 0xFF when there is none. */
uint8_t gift_kind(void);

/* Human-readable name for a gift kind ("pebble", "leaf", ...). */
const char *gift_kind_name(uint8_t kind);

/* Plain-language description of what a kind does, e.g. "rests faster".
 * The status page shows this instead of raw multipliers, which meant nothing
 * to a player ("eR1.50 hD1.00 aW1.00"). */
const char *gift_kind_effect(uint8_t kind);

/* Level at which a gift kind becomes available. */
uint32_t gift_unlock_level(uint8_t kind);

/* True if the kind is unlocked at the current level. */
bool gift_unlocked(uint8_t kind);
uint32_t gift_cost(uint8_t kind);
bool gift_owned(uint8_t kind);
void gift_mark_owned(uint8_t kind);
bool gift_can_place(uint8_t kind);
bool gift_purchase(uint8_t kind);
const char *gift_interaction(uint8_t kind);
bool gift_economy_test(void);
