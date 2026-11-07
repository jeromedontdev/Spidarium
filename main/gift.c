/*
 * Spidarium — the gift, implementation.
 *
 * The catalogue itself lives in species.c (k_gifts[]). This file only applies
 * the current gift's multipliers. Adding a gift kind means adding one row to
 * that table -- nothing here changes.
 */
#include "gift.h"

#include "game.h"
#include "game_config.h"
#include "species.h"

static gift_bonus_t s_bonus;

const char *gift_kind_name(uint8_t kind)
{
    return gift_def(kind)->name;
}

const char *gift_kind_effect(uint8_t kind)
{
    return gift_def(kind)->effect;
}

uint32_t gift_unlock_level(uint8_t kind)
{
    return gift_def(kind)->unlock_level;
}

bool gift_unlocked(uint8_t kind)
{
    return g.level >= gift_unlock_level(kind);
}

bool gift_present(void)
{
    return g.gift.used != 0;
}

uint8_t gift_kind(void)
{
    return g.gift.used ? (uint8_t)(g.gift.kind % GIFT_KINDS) : 0xFF;
}

const gift_bonus_t *gift_bonus(void)
{
    return &s_bonus;
}

void gift_compute(void)
{
    /* Reset to neutral every tick so removing or replacing the gift stops the
     * old bonus immediately. */
    s_bonus.energy_regen_mult  = 1.0f;
    s_bonus.hygiene_decay_mult = 1.0f;
    s_bonus.pounce_range_mult  = 1.0f;
    s_bonus.aim_window_mult    = 1.0f;
    s_bonus.wild_spawn_bonus   = 0;

    if (!g.gift.used) return;

    const gift_def_t *d = gift_def(g.gift.kind);

    s_bonus.energy_regen_mult  *= d->energy_regen_mult;
    s_bonus.hygiene_decay_mult *= d->hygiene_decay_mult;
    s_bonus.pounce_range_mult  *= d->pounce_range_mult;
    s_bonus.aim_window_mult    *= d->aim_window_mult;
    s_bonus.wild_spawn_bonus   += d->wild_spawn_bonus;
}
