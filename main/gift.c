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
    /* New fields (2026-11-07 expansion) */
    s_bonus.pounce_success_mult = 1.0f;
    s_bonus.movement_efficiency_mult = 1.0f;
    s_bonus.mood_mult          = 1.0f;
    s_bonus.fear_mult          = 1.0f;
    s_bonus.prey_flee_penalty  = 1.0f;
    s_bonus.catch_success_mult = 1.0f;
    s_bonus.prey_detection_range_mult = 1.0f;
    s_bonus.all_stats_decay_mult = 1.0f;
    s_bonus.bonus_synergy_mult = 1.0f;

    if (!g.gift.used) return;

    const gift_def_t *d = gift_def(g.gift.kind);

    s_bonus.energy_regen_mult  *= d->energy_regen_mult;
    s_bonus.hygiene_decay_mult *= d->hygiene_decay_mult;
    s_bonus.pounce_range_mult  *= d->pounce_range_mult;
    s_bonus.aim_window_mult    *= d->aim_window_mult;
    s_bonus.wild_spawn_bonus   += d->wild_spawn_bonus;
    /* Apply new multipliers */
    s_bonus.pounce_success_mult *= d->pounce_success_mult;
    s_bonus.movement_efficiency_mult *= d->movement_efficiency_mult;
    s_bonus.mood_mult          *= d->mood_mult;
    s_bonus.fear_mult          *= d->fear_mult;
    s_bonus.prey_flee_penalty  *= d->prey_flee_penalty;
    s_bonus.catch_success_mult *= d->catch_success_mult;
    s_bonus.prey_detection_range_mult *= d->prey_detection_range_mult;
    s_bonus.all_stats_decay_mult *= d->all_stats_decay_mult;
    s_bonus.bonus_synergy_mult *= d->bonus_synergy_mult;
}
