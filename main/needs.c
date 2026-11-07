/*
 * Spidarium — needs, care loop and life cycle implementation.
 */
#include "needs.h"

#include "audio.h"
#include "behaviour.h"
#include "game_config.h"
#include "gift.h"
#include "prey.h"
#include "spider.h"
#include "terrarium.h"

#include "esp_log.h"

#include <stdlib.h>

static const char *TAG = "spidarium.needs";

/* Decay accumulators: stats are integers, so fractional decay is carried
 * between ticks to give each stat a distinct curve. */
static float s_hunger_acc;
static float s_hygiene_acc;
static float s_energy_acc;

static uint32_t s_last_decay;
static uint32_t s_last_age;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static uint8_t clamp100(int v)
{
    if (v < 0) return 0;
    if (v > 100) return 100;
    return (uint8_t)v;
}

uint32_t needs_days_lived(void)
{
    return g.age_ticks / TICKS_PER_DAY;
}

const char *needs_stage_name(void)
{
    switch (g.stage) {
    case STAGE_SPIDERLING: return "spiderling";
    case STAGE_JUVENILE:   return "juvenile";
    case STAGE_ADULT:      return "adult";
    default:               return "spiderling";
    }
}

bool needs_is_sick(void)
{
    return g.sick != 0;
}

void needs_recompute_mood(void)
{
    /* Mood drifts toward the average of the other needs, minus penalties for
     * anything critically low. */
    int base = ((int)g.hunger + (int)g.hygiene + (int)g.energy) / 3;

    /* While SICK the penalties are halved. At full strength they pin mood at
     * 0 whenever two stats are low, and recovery requires mood > 25 -- so the
     * spider could never climb out (observed: mood 0 for 4+ min with hunger
     * 31, hygiene 20, energy 26). The spider is already being punished by
     * being sick; the penalties must not make SICK permanent. */
    int penalty = g.sick ? 6 : 12;

    if (g.hunger  < MOOD_LOW_THRESHOLD) base -= penalty;
    if (g.hygiene < MOOD_LOW_THRESHOLD) base -= penalty;
    if (g.energy  < MOOD_LOW_THRESHOLD) base -= penalty;

    /* Ease toward the target rather than snapping. */
    int cur = (int)g.mood;
    int next = cur + (base - cur) / 4;
    g.mood = clamp100(next);
}

/* ------------------------------------------------------------------ */
/* Care actions                                                        */
/* ------------------------------------------------------------------ */
void needs_feed(void)
{
    /* Feeding spawns prey; the spider must catch it. */
    prey_spawn();
    audio_play(SFX_TAP);
    ESP_LOGI(TAG, "feed: prey spawned");
}

void needs_clean(void)
{
    g.hygiene = CLEAN_HYGIENE_SET;
    g.cleans++;

    terrarium_clear_poop();
    prey_clear();

    game_add_xp(XP_CLEAN);
    game_award(ACH_FIRST_CLEAN);
    audio_play(SFX_TAP);
}

void needs_play(void)
{
    g.mood = clamp100((int)g.mood + PLAY_MOOD_GAIN);
    g.energy = clamp100((int)g.energy - PLAY_ENERGY_COST);
    g.plays++;

    behaviour_set_state(SPIDER_PLAY);
    spider_play_once(ANIM_POUNCE, true);

    game_add_xp(XP_PLAY);
    game_award(ACH_FIRST_PLAY);
    audio_play(SFX_POUNCE);
}

void needs_sleep(void)
{
    /* Toggle, but through the forced-sleep path so the state is held for a
     * minimum duration. Setting SPIDER_SLEEP directly let the wake condition
     * fire on the very next tick at high energy, so the button appeared to do
     * nothing. */
    if (g.state == SPIDER_SLEEP) {
        behaviour_force_sleep(false);
    } else {
        behaviour_force_sleep(true);
    }
    audio_play(SFX_TAP);
}

/* ------------------------------------------------------------------ */
/* Stage / molt                                                        */
/* ------------------------------------------------------------------ */
void needs_update_stage(void)
{
    uint32_t days = needs_days_lived();
    g.days_lived = days;

    uint8_t want = g.stage;
    if (days >= STAGE_ADULT_DAYS)      want = STAGE_ADULT;
    else if (days >= STAGE_JUVENILE_DAYS) want = STAGE_JUVENILE;
    else                               want = STAGE_SPIDERLING;

    if (want != g.stage) {
        g.stage = want;
        g.last_molt_day = days;

        /* Molting is a visible event. */
        behaviour_set_state(SPIDER_MOLT);
        spider_play_once(ANIM_MOLT, true);
        audio_play(SFX_MOLT);
        game_award(ACH_FIRST_MOLT);
        if (want == STAGE_ADULT) game_award(ACH_ADULT);

        ESP_LOGI(TAG, "molted to %s (day %u)", needs_stage_name(),
                 (unsigned)days);
    }
}

/* ------------------------------------------------------------------ */
/* Tick                                                                */
/* ------------------------------------------------------------------ */
void needs_tick(uint32_t now_ms, float dt_s)
{
    (void)dt_s;

    /* --- aging ------------------------------------------------------- */
    /* GAME_TIME_SCALE compresses the clock for verification builds. Guarded
     * against 0 so a bad edit cannot divide by zero. */
    if (now_ms - s_last_age >= (AGE_TICK_MS / GAME_TIME_SCALE)) {
        s_last_age = now_ms;
        g.age_ticks++;

        uint32_t days = needs_days_lived();
        if (days != g.days_lived) {
            g.days_lived = days;
            game_add_xp(XP_DAY);
            needs_update_stage();
        }

        /* Mid-stage molt: MOLT_DAYS was defined but never referenced, so
         * molting only ever happened on a stage change. */
        if (days - g.last_molt_day >= MOLT_DAYS && g.stage != STAGE_ADULT) {
            g.last_molt_day = days;
            behaviour_set_state(SPIDER_MOLT);
            spider_play_once(ANIM_MOLT, true);
            audio_play(SFX_MOLT);
            game_add_xp(XP_MOLT);
            game_award(ACH_FIRST_MOLT);
            ESP_LOGI(TAG, "mid-stage molt (day %u)", (unsigned)days);
        }
    }

    /* --- decay ------------------------------------------------------- */
    if (now_ms - s_last_decay < DECAY_PERIOD_MS) return;
    s_last_decay = now_ms;

    /* Per-minute rates scaled by the elapsed game time. Distinct curves:
     * hunger fastest, hygiene slower, energy slowest while awake and
     * regenerating while asleep. */
    float minutes = (float)DECAY_PERIOD_MS / 60000.0f * (float)GAME_TIME_SCALE;

    /* Temperament: a greedy spider burns through food faster. */
    s_hunger_acc += HUNGER_PER_MIN * minutes
                   * game_temperament()->hunger_decay_mult;

    /* Bond erodes while the spider is sick: neglect costs affection. */
    if (g.sick && g.bond > 0) {
        uint16_t d = BOND_SICK_DECAY;
        g.bond = (g.bond > d) ? (uint16_t)(g.bond - d) : 0;
    }

    /* A clean tank cleans the spider; a dirty one dirties it. These are
     * mutually exclusive so the net direction is always unambiguous. */
    if (terrarium_poop_count() == 0) {
        s_hygiene_acc -= HYGIENE_REGEN_PER_MIN * minutes;
    } else {
        /* A leaf gift slows the tank getting dirty. */
        s_hygiene_acc += HYGIENE_PER_MIN * minutes
                       * gift_bonus()->hygiene_decay_mult;
    }

    if (g.state == SPIDER_SLEEP) {
        /* A pebble gift speeds recovery. */
        s_energy_acc -= ENERGY_REGEN_ASLEEP_PER_MIN * minutes
                      * gift_bonus()->energy_regen_mult;
    } else {
        s_energy_acc += ENERGY_PER_MIN * minutes;
    }

    if (s_hunger_acc >= 1.0f) {
        int whole = (int)s_hunger_acc;
        s_hunger_acc -= (float)whole;
        g.hunger = clamp100((int)g.hunger - whole);
    }
    if (s_hygiene_acc >= 1.0f) {
        int whole = (int)s_hygiene_acc;
        s_hygiene_acc -= (float)whole;
        g.hygiene = clamp100((int)g.hygiene - whole);
    } else if (s_hygiene_acc <= -1.0f) {
        int whole = (int)(-s_hygiene_acc);
        s_hygiene_acc += (float)whole;
        g.hygiene = clamp100((int)g.hygiene + whole);
    }
    if (s_energy_acc >= 1.0f) {
        int whole = (int)s_energy_acc;
        s_energy_acc -= (float)whole;
        g.energy = clamp100((int)g.energy - whole);
    } else if (s_energy_acc <= -1.0f) {
        int whole = (int)(-s_energy_acc);
        s_energy_acc += (float)whole;
        g.energy = clamp100((int)g.energy + whole);
    }
    needs_recompute_mood();

    /* --- sickness ---------------------------------------------------- */
    /* Neglect produces a recoverable SICK state. The spider never dies.
     *
     * Hygiene is deliberately NOT a standalone trigger. It is only restored by
     * the CLEAN button, so treating it as one made the spider flap between
     * sick and recovered every decay tick (observed: hygiene pinned at 0,
     * "recovered"/"sick" alternating every ~3 s forever). Hygiene still
     * matters: it drags mood down in needs_recompute_mood(), and mood IS a
     * trigger, so a dirty tank still eventually makes the spider sick. */
    bool neglected = (g.hunger < 10) || (g.mood < 10);

    if (neglected && !g.sick) {
        g.sick = 1;
        behaviour_set_state(SPIDER_SICK);
        audio_play(SFX_ALERT);
        ESP_LOGW(TAG, "spider is sick (hunger=%u mood=%u hygiene=%u)",
                 g.hunger, g.mood, g.hygiene);
    } else if (g.sick) {
        /* Recovery must be reachable. Requiring every stat above 30 was not:
         * hunger decays fastest, so it fell back below the line before the
         * others caught up and the spider stayed sick forever. Recovery now
         * needs hunger and mood up, which feeding and petting can deliver. */
        if (g.hunger > 25 && g.mood > 25) {
            g.sick = 0;
            behaviour_set_state(SPIDER_IDLE);
            game_award(ACH_SURVIVED_SICK);
            ESP_LOGI(TAG, "spider recovered (hunger=%u mood=%u)",
                     g.hunger, g.mood);
        }
    }
}
