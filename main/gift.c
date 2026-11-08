/*
 * Spidarium — the gift, implementation.
 *
 * Stable multiplier definitions live in species.c (k_gifts[]). Shop names,
 * progression, ownership and effective bonuses are owned here.
 */
#include "gift.h"

#include "game.h"
#include "game_config.h"
#include "species.h"
#include "esp_log.h"

static gift_bonus_t s_bonus;

const char *gift_kind_name(uint8_t kind)
{
    static const char *const names[GIFT_KINDS] = {
        "Resting Stone", "Leaf Shelter", "Jumping Branch", "Acorn Hide",
        "Moss Patch", "Sap Trail", "Flower Lure", "Crystal Lookout",
        "Amber Hearth", "Coral Scrubber", "Silk Hammock", "Bone Tunnel",
        "Pearl Toy", "Ruby Launchpad", "Sapphire Lookout", "Emerald Garden",
        "Diamond Retreat", "Royal Perch"
    };
    return names[kind % GIFT_KINDS];
}

const char *gift_kind_effect(uint8_t kind)
{
    static const char *const effects[GIFT_KINDS] = {
        "Rest here; energy recovers 50% faster",
        "Hide under leaves; grime falls 40% slower",
        "Climb and practice; jump range +20%",
        "Explore a hide; energy recovery +30%",
        "Hide in moss; fleeing prey move 15% slower",
        "Explore the trail; movement costs less energy",
        "Watch flowers; attracts more wild prey",
        "Watch from a perch; aim time +25%",
        "Rest by amber; mood gain +10%",
        "Groom here; grime falls 20% slower",
        "Rest in silk; catch margin +20%",
        "Explore the tunnel; calmer hunting",
        "Play with a pearl; mood gain +10%",
        "Practice jumps; range +25%, catch margin +15%",
        "Watch prey; detection +20%, aim time +15%",
        "Explore a garden; all needs decay 5% slower",
        "Rest in a retreat; balanced bonuses +20%",
        "Climb a royal perch; balanced bonuses +30%"
    };
    return effects[kind % GIFT_KINDS];
}

uint32_t gift_unlock_level(uint8_t kind)
{
    static const uint8_t levels[GIFT_KINDS] = {
        1,2,3,4,5,6,8,10,12,14,16,18,20,23,26,30,35,40
    };
    return levels[kind % GIFT_KINDS];
}

bool gift_unlocked(uint8_t kind)
{
    return kind < GIFT_KINDS && (gift_owned(kind) || g.level >= gift_unlock_level(kind));
}

uint32_t gift_cost(uint8_t kind) { return 12 + gift_unlock_level(kind) * 4; }
bool gift_owned(uint8_t kind)
{
    return kind < GIFT_KINDS && (g.gifts_owned & (1u << kind)) != 0;
}
bool gift_can_place(uint8_t kind)
{
    return gift_unlocked(kind) && (gift_owned(kind) || g.silk >= gift_cost(kind));
}
bool gift_purchase(uint8_t kind)
{
    if (!gift_can_place(kind)) {
        ESP_LOGW("spidarium.gift", "purchase rejected: kind=%u level=%lu Silk=%lu",
                 kind, (unsigned long)g.level, (unsigned long)g.silk);
        return false;
    }
    if (!gift_owned(kind)) {
        g.silk -= gift_cost(kind);
        g.gifts_owned |= 1u << kind;
    }
    return true;
}
const char *gift_interaction(uint8_t kind)
{
    static const char *const actions[GIFT_KINDS] = {
        "Resting", "Hiding", "Climbing", "Hiding", "Exploring", "Exploring",
        "Watching", "Watching", "Resting", "Grooming", "Resting", "Exploring",
        "Playing", "Climbing", "Watching", "Exploring", "Resting", "Climbing"
    };
    return actions[kind % GIFT_KINDS];
}
bool gift_economy_test(void)
{
    uint32_t wallet = g.silk, owned = g.gifts_owned, level = g.level;
    g.silk = 0; g.gifts_owned = 0; g.level = 1;
    bool ok = !gift_can_place(0) && !gift_unlocked(17);
    g.silk = gift_cost(0);
    ok = gift_purchase(0) && ok && g.silk == 0 && gift_owned(0);
    ok = gift_purchase(0) && ok && g.silk == 0;
    g.silk = 1000;
    ok = ok && !gift_can_place(17);
    g.level = 40;
    ok = gift_purchase(17) && ok && g.silk == 1000 - gift_cost(17);
    g.silk = wallet; g.gifts_owned = owned; g.level = level;
    return ok;
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

    /* There is only one gift: legendary synergy grants universal benefits,
     * rather than multiplying neutral values and producing no effect. */
    float synergy = s_bonus.bonus_synergy_mult;
    if (synergy > 1.3f) synergy = g.gift.kind == 16 ? 1.2f : 1.3f;
    s_bonus.energy_regen_mult *= synergy;
    s_bonus.hygiene_decay_mult /= synergy;
    s_bonus.pounce_range_mult *= synergy;
    s_bonus.aim_window_mult *= synergy;
    s_bonus.pounce_success_mult *= synergy;
    s_bonus.movement_efficiency_mult *= synergy;
    s_bonus.mood_mult *= synergy;
    s_bonus.fear_mult /= synergy;
    s_bonus.prey_flee_penalty /= synergy;
    s_bonus.catch_success_mult *= synergy;
    s_bonus.prey_detection_range_mult *= synergy;
    s_bonus.all_stats_decay_mult /= synergy;
}
