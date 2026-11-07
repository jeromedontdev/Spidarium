/*
 * Spidarium — background themes.
 *
 * The tank's look is a table entry, so adding a theme is one row in theme.c.
 * Customisation is what keeps a virtual pet interesting over weeks, so this is
 * deliberately the easiest thing in the codebase to extend.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *name;
    uint32_t    unlock_level;
    uint32_t    substrate_top;   /* gradient top */
    uint32_t    substrate_bot;   /* gradient bottom */
    uint32_t    glass;           /* vignette / tank walls */
    uint32_t    web;             /* silk colour */
    uint32_t    dust;            /* floating motes */
} theme_def_t;

#define THEME_COUNT 4

/* The catalogue. Indexed by theme id. */
extern const theme_def_t k_themes[THEME_COUNT];

/* Look up a theme, clamped to a valid index. */
const theme_def_t *theme_get(uint8_t id);

/* The theme currently in use. */
const theme_def_t *theme_current(void);

/* Switch to a theme id. Ignored if locked. Returns true on success. */
bool theme_set(uint8_t id);

/* True if the theme is unlocked at the current level. */
bool theme_unlocked(uint8_t id);

/* Cycle to the next unlocked theme. Returns the new id. */
uint8_t theme_next(void);
