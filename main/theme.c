/*
 * Spidarium — background themes, definitions.
 *
 * ADD A THEME HERE. One row, and it appears in the cycle automatically.
 */
#include "theme.h"

#include "game.h"

const theme_def_t k_themes[THEME_COUNT] = {
    /* name        lvl  substrate top  bottom     glass      web        dust */
    { "night",      1, 0x1b2433, 0x0a0e15, 0x2e3d55, 0x6b7f9e, 0xa8bcd8 },
    { "forest",     3, 0x1a2a1e, 0x0a120c, 0x2c4433, 0x7fa07a, 0xb8d4a8 },
    { "desert",     5, 0x2e2418, 0x140f0a, 0x4a3a26, 0xa08a6b, 0xd8c8a8 },
    { "amethyst",   8, 0x241a33, 0x0e0a15, 0x3d2e55, 0x9e7fbe, 0xc8a8d8 },
};

static uint8_t s_current;

const theme_def_t *theme_get(uint8_t id)
{
    return &k_themes[id % THEME_COUNT];
}

const theme_def_t *theme_current(void)
{
    return theme_get(s_current);
}

bool theme_unlocked(uint8_t id)
{
    return g.level >= theme_get(id)->unlock_level;
}

bool theme_set(uint8_t id)
{
    if (!theme_unlocked(id)) return false;
    s_current = (uint8_t)(id % THEME_COUNT);
    return true;
}

uint8_t theme_next(void)
{
    /* Walk forward to the next unlocked theme. If none is unlocked beyond the
     * current one, wrap to the first unlocked. */
    for (int i = 1; i <= THEME_COUNT; i++) {
        uint8_t cand = (uint8_t)((s_current + i) % THEME_COUNT);
        if (theme_unlocked(cand)) {
            s_current = cand;
            return s_current;
        }
    }
    return s_current;
}
