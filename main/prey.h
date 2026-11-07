/*
 * Spidarium — prey.
 *
 * Fly / cricket / moth with wander + flee AI, and the catch resolution that
 * turns a pounce into a meal (or a miss).
 */
#pragma once

#include "game.h"

#include <stdbool.h>

/* Spawn prey of a random unlocked species at a random playfield point. */
void prey_spawn(void);

/* Spawn a specific species (used by progression unlocks). */
void prey_spawn_species(prey_species_t species);

/* Spawn a specific species with an explicit wild flag. Wild prey are leaner
 * and warier: they keep the tank alive without feeding the spider for free. */
void prey_spawn_species_ex(prey_species_t species, bool wild);

/* Spawn a random unlocked species as wild prey. */
void prey_spawn_wild(void);

/* Advance all prey one logic tick. */
void prey_tick(uint32_t now_ms, float dt_s);

/* Push prey positions to their LVGL objects. */
void prey_render(uint32_t now_ms);

/* Number of live (not dead) prey. */
int prey_count(void);

/* Nearest live prey position. Returns false if none. */
bool prey_nearest(int32_t *x, int32_t *y);

/* Resolve a pounce that just landed. Called by the behaviour module. */
void prey_resolve_pounce(void);

/* Remove all prey (used by clean). */
void prey_clear(void);

/* COUNT of species unlocked at the current level. */
int prey_unlocked_species(void);

/* Index of the next species still locked, or -1 if all are unlocked. */
int prey_next_locked_species(void);

/* Human-readable species name ("fly", "cricket", ...). */
const char *prey_species_name(prey_species_t species);

/* Level at which a species unlocks. */
uint32_t prey_unlock_level(prey_species_t species);
