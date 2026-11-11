/*
 * Spidarium — background themes.
 *
 * Palette entries pair with logical-pixel scenes in terrarium.c.
 * The selected background persists separately from the pet-save payload.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *name;
    uint32_t    unlock_level;
    uint32_t    substrate_top;   /* sky / upper scene color */
    uint32_t    substrate_bot;   /* ground / lower scene color */
    uint32_t    glass;           /* far silhouette */
    uint32_t    web;             /* near silhouette */
    uint32_t    dust;            /* fixed highlight */
} theme_def_t;

#define THEME_COUNT 16

/* The catalogue. Indexed by theme id. */
extern const theme_def_t k_themes[THEME_COUNT];
/* Load the separate background preference before LVGL starts. */
void theme_init(void);

/* Look up a theme, clamped to a valid index. */
const theme_def_t *theme_get(uint8_t id);

/* The theme currently in use. */
const theme_def_t *theme_current(void);

/* Switch to a theme id. Ignored if locked. Returns true on success. */
bool theme_set(uint8_t id);

/* True if the theme is unlocked at the current level. */
bool theme_unlocked(uint8_t id);
bool theme_owned(uint8_t id);
uint16_t theme_cost(uint8_t id);
void theme_restore(void);

/* Cycle to the next unlocked theme. Returns the new id. */
uint8_t theme_next(void);
