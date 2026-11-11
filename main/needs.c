/*
 * Spidarium — needs, care loop and life cycle implementation.
 */
#include "needs.h"

#include "audio.h"
#include "behaviour.h"
#include "cspider.h"
#include "game_config.h"
#include "gift.h"
#include "prey.h"
#include "rtc.h"
#include "spider.h"
#include "terrarium.h"

#include "esp_log.h"

#include <stdlib.h>
#include <math.h>
#include <sys/time.h>
#include <time.h>

static const char *TAG = "spidarium.needs";

/* Decay accumulators: stats are integers, so fractional decay is carried
 * between ticks to give each stat a distinct curve. */
static double s_hunger_acc;
static float s_hygiene_acc;
static float s_energy_acc;

static uint32_t s_last_decay;
static uint32_t s_last_age;
static uint32_t s_last_pet;
static bool s_petted_once;
/* Console-only day override. DAILY_NEVER means "use the real clock". */
static uint32_t s_daily_override = DAILY_NEVER;

static void daily_ensure_base(void);

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static uint8_t clamp100(int v)
{
    if (v < 0) return 0;
    if (v > 100) return 100;
    return (uint8_t)v;
}

static void hunger_decay(float minutes)
{
    s_hunger_acc += HUNGER_PER_MIN * minutes
                   * needs_age_scale()
                   * game_temperament()->hunger_decay_mult
                   * gift_bonus()->all_stats_decay_mult;
    if (s_hunger_acc >= 1.0f) {
        int whole = (int)s_hunger_acc;
        s_hunger_acc -= whole;
        g.hunger = clamp100((int)g.hunger - whole);
    }
}

/* How much faster an older spider burns through its needs. Stage is the
 * player-visible measure of age, so it is the input rather than raw days: a
 * spider that has not molted should not feel like an adult. */
float needs_age_scale(void)
{
    float scale = 1.0f + NEEDS_AGE_SCALE_PER_STAGE * (float)g.stage;
    return scale > NEEDS_AGE_SCALE_MAX ? NEEDS_AGE_SCALE_MAX : scale;
}

bool needs_hunger_test(void)
{
    uint8_t hunger = g.hunger;
    double accumulator = s_hunger_acc;
    float rate = HUNGER_PER_MIN * game_temperament()->hunger_decay_mult *
        gift_bonus()->all_stats_decay_mult * needs_age_scale();
    g.hunger = 100;
    s_hunger_acc = 0;
    for (int i = 0; i < 40; i++) hunger_decay(0.05f);
    int loss = 100 - g.hunger;
    bool ok = HUNGER_PER_MIN == 3.0f && loss == (int)(rate * 2) &&
        fabs(s_hunger_acc - (rate * 2 - loss)) < 0.001;
    g.hunger = hunger;
    s_hunger_acc = accumulator;
    ESP_LOGI(TAG, "hunger test: loss_120s=%d rate=%.2f restored=1", loss, (double)rate);
    return ok;
}

uint32_t needs_days_lived(void)
{
    return g.age_ticks / TICKS_PER_DAY;
}

const char *needs_stage_name(void)
{
    switch (g.stage) {
    case STAGE_SPIDERLING: return "Spiderling";
    case STAGE_JUVENILE:   return "Juvenile";
    case STAGE_ADULT:      return "Adult";
    default:               return "Spiderling";
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
    base = (int)((float)base * gift_bonus()->mood_mult);

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
    if (prey_count() >= MAX_PREY) {
        ESP_LOGW(TAG, "feed rejected: tank already has %d live prey", MAX_PREY);
        return;
    }
    /* Feeding spawns prey; the spider must catch it. */
    prey_spawn();
    audio_play(SFX_TAP);
    ESP_LOGI(TAG, "feed: prey spawned");
}

/* True when there is actually something for Clean to do: droppings to sweep,
 * live prey to shoo out, or hygiene below full. */
bool needs_clean_needed(void)
{
    return terrarium_poop_count() > 0 || prey_count() > 0 ||
           g.hygiene < CLEAN_HYGIENE_SET;
}

/* XP for cleaning, proportional to how much the tank actually improved. The
 * "clean score" is hygiene, and dirt is measured in the same units: a spider
 * dropping costs FEED_HYGIENE_COST hygiene when it is made, and POOP_DIRT_SPIDER
 * matches it. So the score is simply the hygiene that cleaning restores plus the
 * dirt it sweeps up, out of CLEAN_SCORE_FULL.
 *
 * Scaling rather than gating means the button always works, and repeatedly
 * tapping Clean cannot farm XP: the mess is gone after the first tap, so every
 * later tap scores 0. */
uint32_t needs_clean_reward(void)
{
    int score = terrarium_dirt();
    if (g.hygiene < CLEAN_HYGIENE_SET) score += CLEAN_HYGIENE_SET - g.hygiene;
    if (score <= 0) return 0;
    /* Round up, so any real mess pays at least 1 XP and a full tank pays the
     * maximum. Truncation would pay 0 for a single dropping. */
    uint32_t xp = ((uint32_t)score * CLEAN_XP_MAX + CLEAN_SCORE_FULL - 1) / CLEAN_SCORE_FULL;
    if (xp > CLEAN_XP_MAX) xp = CLEAN_XP_MAX;
    return xp;
}

uint32_t needs_clean(void)
{
    /* The guard is "is there anything to do", NOT "is there XP to pay". A tank
     * holding only bugs has no dirt and full hygiene, so it scores 0 XP, but
     * Clean still shooed the bugs out -- returning early on xp == 0 would leave
     * the bugs in the tank while reporting success. */
    if (!needs_clean_needed()) {
        ESP_LOGI(TAG, "clean: tank already clean, no reward (hygiene=%u poop=%d prey=%d)",
                 g.hygiene, terrarium_poop_count(), prey_count());
        return 0;
    }

    uint32_t xp = needs_clean_reward();
    int dirt = terrarium_dirt();
    g.hygiene = CLEAN_HYGIENE_SET;
    g.cleans++;

    terrarium_clear_poop();
    prey_clear();

    game_add_xp(xp);
    game_award(ACH_FIRST_CLEAN);
    audio_play(SFX_TAP);
    ESP_LOGI(TAG, "clean: dirt=%d xp=%lu", dirt, (unsigned long)xp);
    return xp;
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

/* Petting: a tap that lands on the spider's body. Returns true when the tap was
 * consumed as a pet, so the caller can stop treating it as an empty tap.
 *
 * The cooldown exists because a tap is a cheap gesture: without it a player
 * could hold a finger on the spider and farm mood, XP and the daily task. */
bool needs_pet(int x, int y)
{
    int dx = x - g.spider_x;
    int dy = y - g.spider_y;
    if (dx * dx + dy * dy > PET_RADIUS_PX * PET_RADIUS_PX) return false;

    uint32_t now = game_now();
    /* A separate "has petted" flag, not `s_last_pet != 0`: the clock can
     * legitimately read 0 early in a session, and then the guard would never
     * engage and a held finger could farm mood. */
    if (s_petted_once && now - s_last_pet < PET_COOLDOWN_MS) return false;
    s_petted_once = true;
    s_last_pet = now;

    g.mood = clamp100((int)g.mood + PET_MOOD_GAIN);
    g.pets++;
    if (g.bond < 100) g.bond++;

    /* Affection is uncapped; the currency is not. A tap is the cheapest gesture
     * in the game, so an uncapped reward turned a bored thumb into an XP
     * faucet. Mood and bond still rise on every pet, and the spider still
     * reacts, so the cap is invisible unless the player is farming. */
    uint32_t today = needs_daily_today();
    if (g.pet_xp_day != today) {
        g.pet_xp_day = today;
        g.pet_xp_today = 0;
    }
    uint8_t paid = 0;
    if (g.pet_xp_today < PET_XP_DAILY_MAX) {
        paid = (uint8_t)(PET_XP_DAILY_MAX - g.pet_xp_today < PET_XP ?
                         PET_XP_DAILY_MAX - g.pet_xp_today : PET_XP);
        g.pet_xp_today = (uint8_t)(g.pet_xp_today + paid);
        game_add_xp(paid);
    }

    behaviour_set_state(SPIDER_PLAY);
    cspider_pet_react();

    game_award(ACH_FIRST_PET);
    audio_play(SFX_CHIRP);
    ESP_LOGI(TAG, "pet: mood=%u bond=%u pets=%lu xp=%u/%u",
             g.mood, (unsigned)g.bond, (unsigned long)g.pets,
             (unsigned)g.pet_xp_today, (unsigned)PET_XP_DAILY_MAX);
    return true;
}

void needs_sleep(void)
{    /* Toggle, but through the forced-sleep path so the state is held for a
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
/* Offline catch-up                                                    */
/* ------------------------------------------------------------------ */
/* The device is not always running: the player switches it off, the screen
 * sleeps, or the battery dies. Without this the pet came back exactly as it
 * was left, which made hunger and hygiene meaningless over a real day.
 *
 * The RTC is the clock of record. At boot the saved `last_seen` epoch is
 * compared with the RTC and the difference is applied as decay. The span is
 * clamped so a long absence is a setback, not a wipe: the spider never dies,
 * and coming back to four zeroed stats is indistinguishable from a reset. */
#define OFFLINE_MAX_SECONDS (12u * 3600u)

void needs_apply_offline(uint32_t seconds)
{
    if (seconds == 0) return;
    if (seconds > OFFLINE_MAX_SECONDS) seconds = OFFLINE_MAX_SECONDS;

    float minutes = (float)seconds / 60.0f;

    /* Hunger and hygiene always decay. Energy recovers, because an absent
     * spider is a resting spider - that is the one stat absence should help. */
    hunger_decay(minutes);

    s_hygiene_acc += HYGIENE_PER_MIN * minutes
                   * gift_bonus()->hygiene_decay_mult
                   * gift_bonus()->all_stats_decay_mult;
    if (s_hygiene_acc >= 1.0f) {
        int whole = (int)s_hygiene_acc;
        s_hygiene_acc -= (float)whole;
        g.hygiene = clamp100((int)g.hygiene - whole);
    }

    s_energy_acc -= ENERGY_REGEN_ASLEEP_PER_MIN * minutes
                  * gift_bonus()->energy_regen_mult;
    if (s_energy_acc <= -1.0f) {
        int whole = (int)(-s_energy_acc);
        s_energy_acc += (float)whole;
        g.energy = clamp100((int)g.energy + whole);
    }

    /* Age advances with real time too, so a spider left alone still grows. */
    uint32_t ticks = (uint32_t)((uint64_t)seconds * 1000u / AGE_TICK_MS);
    if (ticks) {
        g.age_ticks += ticks;
        uint32_t days = needs_days_lived();
        if (days != g.days_lived) {
            g.days_lived = days;
            needs_update_stage();
        }
    }

    needs_recompute_mood();
    ESP_LOGI(TAG, "offline %us: hunger=%u hygiene=%u energy=%u mood=%u",
             (unsigned)seconds, g.hunger, g.hygiene, g.energy, g.mood);
}

/* ------------------------------------------------------------------ */
void needs_tick(uint32_t now_ms, float dt_s)
{
    (void)dt_s;

    /* Establish the task baseline for the current day. This is a no-op once the
     * day matches, so it costs one comparison per tick, and it covers the two
     * cases the rollover path cannot: a freshly migrated save (which has no
     * baseline at all) and a save restored from a backup. */
    daily_ensure_base();

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
            /* Snapshot the task baseline the moment the day turns over, before
             * the player can act on it. */
            daily_ensure_base();
            if (g.daily_day != days) {
                ESP_LOGI(TAG, "daily: day %u available (streak %u)",
                         (unsigned)days, (unsigned)g.daily_streak);
            }
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
    hunger_decay(minutes);

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
                       * needs_age_scale()
                       * gift_bonus()->hygiene_decay_mult
                       * gift_bonus()->all_stats_decay_mult;
    }

    if (g.state == SPIDER_SLEEP) {
        /* A pebble gift speeds recovery. */
        s_energy_acc -= ENERGY_REGEN_ASLEEP_PER_MIN * minutes
                      * gift_bonus()->energy_regen_mult;
    } else {
        s_energy_acc += ENERGY_PER_MIN * minutes
                      * needs_age_scale()
                      * gift_bonus()->all_stats_decay_mult
                      / gift_bonus()->movement_efficiency_mult;
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

/* ------------------------------------------------------------------ */
/* Daily loop                                                          */
/* ------------------------------------------------------------------ */
/* The daily loop is deliberately driven by the in-game day counter rather than
 * the RTC. The RTC is optional (no battery, no sync without WiFi), so a
 * wall-clock daily would either never fire or fire on a bogus 1970 epoch. The
 * in-game day advances with AGE_TICK_MS, which GAME_TIME_SCALE compresses for
 * verification builds -- so the whole loop is testable in minutes. */

uint32_t needs_daily_today(void)
{
    /* A console override wins over both clocks. It exists so the streak and the
     * task rotation can be exercised without waiting a real day, and it has to
     * bypass the RTC because the RTC refuses dates before 2000. */
    if (s_daily_override != DAILY_NEVER) return s_daily_override;

    /* Prefer the RTC. A real calendar day is what the player expects: the
     * check-in should reset at midnight, not after 24 hours of play. The RTC is
     * optional (no backup battery, no sync without WiFi), so when it is not
     * valid we fall back to the in-game day counter, which advances with
     * AGE_TICK_MS and is compressed by GAME_TIME_SCALE for verification. */
    time_t now = 0;
    if (pet_rtc_read(&now) == ESP_OK && now > 0) {
        return (uint32_t)(now / 86400);
    }
    return needs_days_lived();
}

/* The task rotates deterministically from the day number, so the same day
 * always offers the same task and a reload cannot reroll it. */
static uint8_t daily_task_for_day(uint32_t day)
{
    return (uint8_t)(day % DAILY_TASK_COUNT);
}

/* Each task counts a different lifetime counter. The snapshot taken when the
 * day began is subtracted, so progress is "since this day started" without
 * needing a separate per-day counter in the save. */
static uint32_t daily_counter(uint8_t task)
{
    switch (task) {
    case 0: return g.meals;
    case 1: return g.cleans;
    case 2: return g.plays;
    default: return g.pets;
    }
}

uint32_t needs_daily_task_goal(void)
{
    switch (needs_daily_task()) {
    case 0: return 3;   /* catch three prey */
    case 1: return 2;   /* clean twice */
    case 2: return 3;   /* play three times */
    default: return 4;  /* pet four times */
    }
}

const char *needs_daily_task_name(uint8_t task)
{
    switch (task) {
    case 0: return "Catch 3 Prey";
    case 1: return "Clean 2 Times";
    case 2: return "Play 3 Times";
    default: return "Pet 4 Times";
    }
}

uint8_t needs_daily_task(void)
{
    return daily_task_for_day(needs_daily_today());
}

/* The snapshot is taken when the day rolls over, not lazily on first read.
 * Taking it lazily meant the baseline could be captured AFTER the player had
 * already done the actions, so progress read 0/3 forever. */
static void daily_ensure_base(void)
{
    uint32_t today = needs_daily_today();
    if (g.daily_base_day == today) return;
    g.daily_base_day = today;
    g.daily_task_base = daily_counter(daily_task_for_day(today));
    g.daily_task_done = 0;
}

uint32_t needs_daily_task_progress(void)
{
    uint32_t now = daily_counter(needs_daily_task());
    uint32_t base = g.daily_task_base;
    if (now < base) return 0;
    uint32_t done = now - base;
    uint32_t goal = needs_daily_task_goal();
    return done > goal ? goal : done;
}

bool needs_daily_task_ready(void)
{
    if (g.daily_task_done) return false;
    return needs_daily_task_progress() >= needs_daily_task_goal();
}

bool needs_daily_available(void)
{
    /* A streak of 0 means "never claimed", which is self-healing: any save
     * whose daily_day happens to equal today but that has no streak is treated
     * as unclaimed rather than permanently locked out. */
    if (g.daily_streak == 0) return true;
    return g.daily_day != needs_daily_today();
}

uint32_t needs_daily_claim(void)
{
    uint32_t today = needs_daily_today();
    if (!needs_daily_available()) {
        ESP_LOGI(TAG, "daily: already claimed for day %u", (unsigned)today);
        return 0;
    }

    /* A streak survives only if the previous claim was the immediately
     * preceding day. Anything else -- including a first-ever claim -- starts
     * over at 1. */
    if (g.daily_day != DAILY_NEVER && g.daily_day + 1 == today && g.daily_streak > 0) {
        if (g.daily_streak < DAILY_STREAK_MAX) g.daily_streak++;
    } else {
        g.daily_streak = 1;
    }

    uint32_t step = (uint32_t)g.daily_streak - 1u;
    uint32_t xp = DAILY_XP_BASE + step * DAILY_XP_PER_DAY;
    uint32_t silk = DAILY_SILK_BASE + step * DAILY_SILK_PER_DAY;

    g.daily_day = today;

    game_add_xp(xp);
    game_add_silk(silk);
    audio_play(SFX_PERFECT);
    ESP_LOGI(TAG, "daily: day %u streak %u -> xp=%lu silk=%lu",
             (unsigned)today, (unsigned)g.daily_streak,
             (unsigned long)xp, (unsigned long)silk);
    return xp;
}

uint32_t needs_daily_task_claim(void)
{
    if (!needs_daily_task_ready()) return 0;
    g.daily_task_done = 1;
    game_add_xp(DAILY_TASK_XP);
    game_add_silk(DAILY_TASK_SILK);
    audio_play(SFX_PERFECT);
    ESP_LOGI(TAG, "daily task %u complete -> xp=%u silk=%u",
             (unsigned)needs_daily_task(), DAILY_TASK_XP, DAILY_TASK_SILK);
    return DAILY_TASK_XP;
}

/* Force the in-game day, for the serial console. This exists so the streak and
 * the task rotation can be exercised without waiting AGE_TICK_MS *
 * TICKS_PER_DAY. It snapshots the task baseline exactly as a real rollover
 * does, so the task starts at 0 rather than inheriting the previous day's
 * counters. */
void needs_daily_force_day(uint32_t day)
{
    /* The day number comes from the RTC when it is valid, and the RTC refuses
     * dates before 2000, so a small day number cannot be written to it. An
     * explicit override is the only way to exercise the streak and the task
     * rotation without waiting a real day. */
    s_daily_override = day;
    g.age_ticks = day * TICKS_PER_DAY;
    g.days_lived = day;
    g.daily_base_day = day;
    g.daily_task_base = daily_counter(daily_task_for_day(day));
    g.daily_task_done = 0;
    ESP_LOGI(TAG, "daily: forced to day %u (task %u, base %lu)",
             (unsigned)day, (unsigned)daily_task_for_day(day),
             (unsigned long)g.daily_task_base);
}

/* Drop the console override so the real clock takes over again. */
void needs_daily_clear_override(void)
{
    s_daily_override = DAILY_NEVER;
    ESP_LOGI(TAG, "daily: override cleared, day %u", (unsigned)needs_daily_today());
}

/* ------------------------------------------------------------------ */
/* Treats                                                              */
/* ------------------------------------------------------------------ */
uint32_t treat_cost(treat_t kind)
{
    switch (kind) {
    case TREAT_HUNGER: return TREAT_HUNGER_COST;
    case TREAT_MOOD:   return TREAT_MOOD_COST;
    default:           return TREAT_ENERGY_COST;
    }
}

const char *treat_name(treat_t kind)
{
    switch (kind) {
    case TREAT_HUNGER: return "Juicy Fly";
    case TREAT_MOOD:   return "Silk Ribbon";
    default:           return "Warm Pebble";
    }
}

bool treat_buy(treat_t kind)
{
    uint32_t cost = treat_cost(kind);
    if (g.silk < cost) {
        ESP_LOGW(TAG, "treat %u refused: silk=%lu cost=%lu",
                 (unsigned)kind, (unsigned long)g.silk, (unsigned long)cost);
        return false;
    }
    g.silk -= cost;

    switch (kind) {
    case TREAT_HUNGER:
        g.hunger = clamp100((int)g.hunger + TREAT_HUNGER_GAIN);
        break;
    case TREAT_MOOD:
        g.mood = clamp100((int)g.mood + TREAT_MOOD_GAIN);
        break;
    default:
        g.energy = clamp100((int)g.energy + TREAT_ENERGY_GAIN);
        break;
    }
    needs_recompute_mood();
    audio_play(SFX_EAT);
    ESP_LOGI(TAG, "treat %u bought: silk=%lu", (unsigned)kind,
             (unsigned long)g.silk);
    return true;
}
