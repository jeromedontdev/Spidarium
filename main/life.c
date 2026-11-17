#include "life.h"
#include "game.h"
#include "save.h"
#include "net.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <math.h>
#include <string.h>
#include <time.h>

static const char *TAG = "spidarium.life";
static bool s_testing, s_test_accept;
static uint32_t s_test_epoch;
static uint32_t s_generation_ticket;
static bool enqueue(void) { return s_testing ? s_test_accept : save_request(&g, sizeof g); }

void life_defaults(life_state_t *s)
{
    memset(s, 0, sizeof *s);
    s->visitor_day = LIFE_NEVER;
    s->daynight = 1;
}

bool life_valid(const life_state_t *s)
{
    if (!s || s->daynight > 1 || s->weather > 1 || s->retired_count > 3 ||
        s->cooldown > 30 || s->light_gesture_disabled > 1 ||
        (s->visitor_day != LIFE_NEVER && s->visitor_day < 19723) ||
        (s->season_highwater && (s->season_highwater < 8097 || s->season_highwater > 65535)))
        return false;
    for (unsigned j = 0; j < 3; j++) if (s->progress[j] > 5) return false;
    for (unsigned i = 0; i < s->retired_count; i++) {
        const life_pet_t *p = &s->retired[i];
        /* Validate through the existing pet ABI without allocating a game state. */
        for (unsigned j = 0; j < 4; j++) if (p->core[j] > 100) return false;
        uint16_t bond;
        int16_t x, y;
        float heading;
        memcpy(&bond, p->core + offsetof(game_state_t, bond), sizeof bond);
        memcpy(&heading, p->core + offsetof(game_state_t, spider_heading), sizeof heading);
        memcpy(&x, p->core + offsetof(game_state_t, spider_x), sizeof x);
        memcpy(&y, p->core + offsetof(game_state_t, spider_y), sizeof y);
        unsigned temperament = p->core[offsetof(game_state_t, temperament)];
        if (p->core[offsetof(game_state_t, stage)] > 2 ||
            p->core[offsetof(game_state_t, sick)] > 1 || bond > 100 ||
            p->core[offsetof(game_state_t, state)] > SPIDER_PERCH || !isfinite(heading) ||
            (temperament != 255 && temperament >= TEMPERAMENT_COUNT) ||
            x < 0 || x > 480 || y < 0 || y > 480 ||
            (p->core[116] != 255 && p->core[116] >= 6) ||
            (p->core[117] != 255 && p->core[117] >= 9) ||
            (p->core[118] != 255 && p->core[118] >= 9) ||
            (p->core[119] != 255 && p->core[119] >= 9) ||
            p->daily[12] > 7 || p->daily[13] > 1 || p->daily[20] > 10 ||
            p->reserved || !memchr(p->core + offsetof(game_state_t, name), 0, 16) ||
            !companion_valid(&p->companion)) return false;
        for (unsigned j = 0; j < 3; j++) if (p->progress[j] > 5) return false;
    }
    return true;
}

life_clock_t life_clock(uint32_t epoch)
{
    life_clock_t c = {0};
    if (epoch < 1704067200u) return c;
    time_t t = epoch;
    struct tm tm;
    if (!gmtime_r(&t, &tm)) return c;
    unsigned hour = tm.tm_hour;
    c.available = true;
    c.day = epoch / 86400u;
    c.period = hour < 6 || hour >= 22 ? 3 : hour < 12 ? 0 : hour < 18 ? 1 : 2;
    c.season = tm.tm_mon / 3;
    c.season_key = (tm.tm_year + 1900) * 4u + c.season + 1;
    c.activity = hour >= 6 && hour < 12 ? 1 : hour < 18 && hour >= 12 ? 2 :
                 hour >= 18 && hour < 22 ? 3 : 0;
    c.visitor = c.day % 3 == 0 && hour >= 12 && hour < 18;
    return c;
}

const char *life_trick_name(unsigned n)
{
    static const char *names[] = {"Wave", "Spin", "Bow"};
    return n < 3 ? names[n] : "Unknown";
}
const char *life_phase_name(unsigned n)
{
    static const char *names[] = {"Morning", "Afternoon", "Evening", "Night"};
    return n < 4 ? names[n] : "Clock unavailable";
}
const char *life_season_name(unsigned n)
{
    static const char *names[] = {"Winter", "Spring", "Summer", "Autumn"};
    return n < 4 ? names[n] : "Clock unavailable";
}
const char *life_activity_name(unsigned n)
{
    static const char *names[] = {"Rest", "Morning stretch", "Afternoon explore", "Evening quiet"};
    return n < 4 ? names[n] : "Clock unavailable";
}
static life_clock_t current_clock(void)
{
    time_t now = time(NULL);
    return life_clock(s_testing ? s_test_epoch :
        now > 0 && (uint64_t)now <= UINT32_MAX ? (uint32_t)now : 0);
}

uint8_t life_period_now(void)
{
    life_clock_t c = current_clock();
    return c.available ? c.period : 255;
}
bool life_visitor_available(void)
{
    life_clock_t c = current_clock();
    return c.available && c.visitor &&
        (g.life.visitor_day == LIFE_NEVER || c.day > g.life.visitor_day);
}

void life_tick(void) { life_tick_active(lv_tick_get(), true); }

void life_tick_active(uint32_t now, bool active)
{
    static uint32_t last, remainder;
    uint32_t elapsed = last ? now - last : 0;
    last = now;
    /* A paused display is not active practice time. */
    if (!active || elapsed > 1000) { remainder = 0; return; }
    if (!g.life.cooldown) { remainder = 0; return; }
    remainder += elapsed;
    while (remainder >= 1000 && g.life.cooldown) {
        remainder -= 1000;
        g.life.cooldown--;
    }
}

static bool ready(unsigned trick)
{
    if (trick >= LIFE_TRICKS || g.sick || g.energy < 20 ||
        g.state == SPIDER_SLEEP || g.state == SPIDER_MOLT) {
        ESP_LOGW(TAG, "trick rejected: invalid trick or care gate");
        return false;
    }
    return true;
}

bool life_train(unsigned trick)
{
    if (!ready(trick)) return false;
    if (g.life.cooldown || g.life.progress[trick] >= 5) {
        ESP_LOGW(TAG, "practice rejected: cooldown or already mastered");
        return false;
    }
    g.life.progress[trick]++;
    g.life.cooldown = 30;
    g.energy -= 2;
    if (g.mood < 100) g.mood++;
    return true;
}

bool life_perform(unsigned trick)
{
    if (!ready(trick)) return false;
    if (g.life.progress[trick] != 5) {
        ESP_LOGW(TAG, "performance rejected: trick not mastered");
        return false;
    }
    return true;
}

static bool reward(bool visitor)
{
    life_clock_t c = current_clock();
    uint32_t amount = visitor ? 3 : 10;
    if (!c.available || (visitor ? !c.visitor ||
        (g.life.visitor_day != LIFE_NEVER && c.day <= g.life.visitor_day) :
        c.season_key <= g.life.season_highwater) || g.silk > UINT32_MAX - amount) {
        ESP_LOGW(TAG, "calendar reward rejected: unavailable, already claimed or overflow");
        return false;
    }
    uint32_t old = visitor ? g.life.visitor_day : g.life.season_highwater;
    if (visitor) g.life.visitor_day = c.day;
    else g.life.season_highwater = c.season_key;
    g.silk += amount;
    if (enqueue()) return true;
    g.silk -= amount;
    if (visitor) g.life.visitor_day = old;
    else g.life.season_highwater = old;
    ESP_LOGW(TAG, "calendar reward rolled back: save queue rejected");
    return false;
}

bool life_claim_visitor(void) { return reward(true); }
bool life_claim_season(void) { return reward(false); }

static void capture(life_pet_t *p)
{
    memset(p, 0, sizeof *p);
    memcpy(p->core, &g, sizeof p->core);
    memcpy(p->daily, (uint8_t *)&g + 136, sizeof p->daily);
    p->companion = g.companion;
    memcpy(p->progress, g.life.progress, 3);
}

static void restore(const life_pet_t *p)
{
    gift_t gift = g.gift;
    uint8_t muted = g.muted, mode = g.gift_mode, selected = g.gift_selected;
    memcpy(&g, p->core, sizeof p->core);
    memcpy((uint8_t *)&g + 136, p->daily, sizeof p->daily);
    g.companion = p->companion;
    memcpy(g.life.progress, p->progress, 3);
    g.gift = gift;
    g.muted = muted;
    g.gift_mode = mode;
    g.gift_selected = selected;
    g.life.cooldown = 30;
    g.state = g.sick ? SPIDER_SICK : SPIDER_IDLE;
    g.eat_until_ms = 0;
    g.blink_until = g.blink_window = 0;
    if (g.temperament == 255) g.temperament = g.seed % TEMPERAMENT_COUNT;
    if (g.colour == 255) g.colour = 0;
    if (g.pupil_colour == 255) g.pupil_colour = 0;
    if (g.leg_colour == 255) g.leg_colour = 2;
    if (g.fang_colour == 255) g.fang_colour = 1;
}

static bool generation(unsigned slot, bool retire)
{
    if ((!s_testing && (save_busy() || net_busy() || sd_busy())) || !life_valid(&g.life) || (retire ?
        g.stage != STAGE_ADULT || g.sick || g.life.retired_count >= 3 :
        slot >= g.life.retired_count)) {
        ESP_LOGW(TAG, "generation rejected: adult/health/capacity/slot gate");
        return false;
    }
    game_state_t *before = heap_caps_malloc(sizeof g, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!before) { ESP_LOGW(TAG, "generation rejected: snapshot allocation"); return false; }
    *before = g;
    if (retire) {
        capture(&g.life.retired[g.life.retired_count++]);
        life_pet_t newborn = {0};
        newborn.core[0] = 80;
        newborn.core[1] = 80;
        newborn.core[2] = 80;
        newborn.core[3] = 100;
        memcpy(newborn.core + offsetof(game_state_t, colour),
               (uint8_t *)before + offsetof(game_state_t, colour), 4);
        uint32_t level = 1;
        memcpy(newborn.core + offsetof(game_state_t, level), &level, 4);
        uint32_t seed = before->seed * 1664525u + 1013904223u;
        newborn.core[offsetof(game_state_t, temperament)] = seed % TEMPERAMENT_COUNT;
        memcpy(newborn.core + offsetof(game_state_t, seed), &seed, 4);
        memcpy(newborn.core + offsetof(game_state_t, spider_x), &before->spider_x, 4);
        memset(newborn.daily, 255, 8);
        memset(newborn.daily + 16, 255, 4);
        companion_defaults(&newborn.companion);
        restore(&newborn);
    } else {
        life_pet_t pet = g.life.retired[slot];
        capture(&g.life.retired[slot]);
        restore(&pet);
    }
    bool ok = s_testing ? s_test_accept :
        save_request_tracked(&g, sizeof g, &s_generation_ticket);
    if (!ok) {
        g = *before;
        ESP_LOGW(TAG, "generation rolled back: save queue rejected");
    }
    heap_caps_free(before);
    return ok;
}

bool life_retire_confirmed(void) { return generation(0, true); }
bool life_resume(unsigned slot) { return generation(slot, false); }
int life_generation_save_status(void)
{
    bool ok;
    return save_tracked_result(s_generation_ticket, &ok) ? (ok ? 1 : 0) : -1;
}

static uint32_t state_hash(void)
{
    const uint8_t *bytes = (const uint8_t *)&g;
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < sizeof g; i++) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

bool life_test(void)
{
    game_state_t *before = heap_caps_malloc(sizeof g, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!before) { ESP_LOGW(TAG, "life test rejected: allocation"); return false; }
    *before = g;
    s_testing = true;
    s_test_accept = true;
    life_defaults(&g.life);
    bool ok = life_valid(&g.life) && g.life.daynight && !g.life.weather &&
        !g.life.light_gesture_disabled &&
        !life_clock(0).available && life_clock(1704067200).period == 3;
    g.life.light_gesture_disabled = 2;
    ok = !life_valid(&g.life) && ok;
    g.life.light_gesture_disabled = 1;
    g.life.progress[0] = 6;
    ok = ok && !life_valid(&g.life);
    g.life.progress[0] = 0;
    g.stage = STAGE_JUVENILE;
    g.sick = 0;
    ok = !life_retire_confirmed() && ok;
    g.stage = STAGE_ADULT;
    g.energy = 80;
    g.state = SPIDER_IDLE;
    uint32_t silk = g.silk;
    ok = life_train(0) && !life_train(0) && g.silk == silk && !life_perform(0) && ok;
    g.life.progress[0] = 5;
    ok = life_perform(0) && ok;
    s_test_accept = false;
    uint32_t hash = state_hash();
    ok = !life_retire_confirmed() && state_hash() == hash && ok;
    s_test_accept = true;
    life_pet_t original;
    capture(&original);
    ok = life_retire_confirmed() && g.stage == STAGE_SPIDERLING &&
        g.life.retired_count == 1 && g.silk == silk && g.life.light_gesture_disabled == 1 && ok;
    ok = life_resume(0) && g.stage == STAGE_ADULT && g.silk == silk &&
        g.life.light_gesture_disabled == 1 &&
        !memcmp(&g, original.core, 38) &&
        !memcmp((uint8_t *)&g + 46, original.core + 46, 2) &&
        !memcmp((uint8_t *)&g + 52, original.core + 52, 16) &&
        !memcmp((uint8_t *)&g + 72, original.core + 72, 4) &&
        !memcmp((uint8_t *)&g + 88, original.core + 88, 32) &&
        !memcmp((uint8_t *)&g + 136, original.daily, 24) &&
        !memcmp(&g.companion, &original.companion, sizeof g.companion) &&
        !memcmp(g.life.progress, original.progress, 3) && ok;
    s_test_accept = false;
    hash = state_hash();
    ok = !life_resume(0) && state_hash() == hash && ok;
    s_test_accept = true;
    g.life.retired_count = LIFE_RETIRED_CAP;
    for (unsigned i = 0; i < 3; i++) g.life.retired[i] = original;
    ok = !life_retire_confirmed() && !life_resume(3) && ok;
    g.silk = 100;
    s_test_epoch = 0;
    ok = !life_claim_visitor() && !life_claim_season() && g.silk == 100 && ok;
    s_test_epoch = 1704283200u; /* 2024-01-03 noon, visitor day. */
    ok = life_claim_visitor() && !life_claim_visitor() && g.silk == 103 &&
        life_claim_season() && !life_claim_season() && g.silk == 113 && ok;
    s_test_epoch = 1711929600u; /* 2024-04-01. */
    ok = life_claim_season() && g.silk == 123 && ok;
    s_test_epoch = 1704283200u;
    ok = !life_claim_season() && !life_claim_visitor() && g.silk == 123 && ok;
    s_test_epoch = 1719792000u; /* 2024-07-01. */
    s_test_accept = false;
    uint32_t season = g.life.season_highwater;
    ok = !life_claim_season() && g.silk == 123 && g.life.season_highwater == season && ok;
    g = *before;
    s_testing = false;
    heap_caps_free(before);
    return ok;
}
