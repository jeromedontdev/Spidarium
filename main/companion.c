#include "companion.h"
#include "game.h"
#include "achievements.h"
#include "species.h"
#include "gift.h"
#include "cspider.h"
#include "ui_hud.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "spidarium.companion";
static const char *const s_games[] = {"Bug Hunt", "Color Match", "Quick Tap",
                                      "Web Weave", "Pounce"};
static bool s_testing;

void companion_defaults(companion_state_t *state)
{
    memset(state, 0, sizeof *state);
    state->favorite_treat = state->favorite_gift = state->favorite_play = COMPANION_UNKNOWN;
}

static unsigned goal(uint8_t kind) { return kind == COMPANION_PET ? 3 : 1; }

bool companion_valid(const companion_state_t *s)
{
    if (s->diary_count > COMPANION_DIARY_CAP || s->clock_mode > 2 ||
        s->initialized > 1 || (s->initialized ? !s->clock_mode : s->clock_mode != 0) ||
        (s->seen_achievements & ~0xfffffu) ||
        s->bond_level > 4 || s->last_stage > 2 || s->was_sick > 1 ||
        (s->favorite_treat != 255 && s->favorite_treat >= 12) ||
        (s->favorite_gift != 255 && s->favorite_gift >= 36) ||
        (s->favorite_play != 255 && s->favorite_play >= 5)) return false;
    if ((s->favorite_treat != 255 && s->treat_counts[s->favorite_treat] < 3) ||
        (s->favorite_gift != 255 && s->gift_counts[s->favorite_gift] < 3) ||
        (s->favorite_play != 255 && s->play_counts[s->favorite_play] < 3)) return false;
    for (unsigned i = 0; i < 2; i++) {
        const companion_request_t *r = &s->requests[i];
        if (!s->initialized && !r->kind && !r->target && !r->progress && !r->state) continue;
        if (r->state > 2 || r->progress > goal(r->kind) ||
            (!r->state && r->progress == goal(r->kind)) ||
            (r->state == 1 && r->progress != goal(r->kind))) return false;
        if (r->kind == COMPANION_HUNT) {
            if (r->target != 255 && r->target >= PREY_SPECIES_COUNT) return false;
        } else if (r->kind == COMPANION_VISIT) {
            if (r->target != 255 && r->target >= GIFT_KINDS) return false;
        } else if (r->kind == COMPANION_PLAY) {
            if (r->target != 255 && r->target >= 3) return false;
        } else if ((r->kind != COMPANION_PET &&
                    r->kind != COMPANION_CLEAN) || r->target != 255) return false;
    }
    unsigned pins = 0;
    for (unsigned i = 0; i < s->diary_count; i++) {
        const companion_memory_t *m = &s->diary[i];
        if (m->pinned > 1 || m->calendar > 1 || m->kind < 1 || m->kind > 8) return false;
        if ((m->kind == 1 && m->value >= 12) || (m->kind == 2 && m->value >= 36) ||
            (m->kind == 3 && m->value >= 3) ||
            (m->kind == 4 && (m->value < 1 || m->value > 4)) ||
            (m->kind == 5 && (m->value < 1 || m->value > 2)) ||
            (m->kind == 6 && m->value) || (m->kind == 7 && m->value >= 20) ||
            (m->kind == 8 && (m->value < 1 || m->value > 2))) return false;
        pins += m->pinned;
    }
    return pins <= COMPANION_PIN_CAP;
}

static void remember(uint8_t kind, uint8_t value)
{
    companion_state_t *s = &g.companion;
    if (kind == 8) {
        for (unsigned i = 0; i < s->diary_count; i++) {
            companion_memory_t *m = &s->diary[i];
            if (m->kind == 8 && m->day == s->request_day &&
                m->calendar == (s->clock_mode == 1)) {
                if (m->value < 2) m->value++;
                return;
            }
        }
    }
    if (s->diary_count == COMPANION_DIARY_CAP) {
        unsigned discard = 0;
        while (discard < s->diary_count && s->diary[discard].pinned) discard++;
        if (discard == s->diary_count) {
            ESP_LOGE(TAG, "diary full of pinned entries");
            return;
        }
        if (!s_testing) game_diary_removed(discard);
        memmove(&s->diary[discard], &s->diary[discard + 1],
                (s->diary_count - discard - 1) * sizeof s->diary[0]);
        s->diary_count--;
    }
    s->diary[s->diary_count++] = (companion_memory_t){
        s->request_day, kind, value, 0, s->clock_mode == 1
    };
}

static void new_requests(uint32_t day)
{
    companion_state_t *s = &g.companion;
    uint32_t mix = g.seed ^ (day * 2654435761u);
    s->request_day = day;
    s->requests[0] = (companion_request_t){mix & 1 ? COMPANION_PET : COMPANION_PLAY, 255, 0, 0};
    companion_request_t *r = &s->requests[1];
    *r = (companion_request_t){COMPANION_PET, 255, 0, 0};
    if (g.gift.used && !g.sick && g.energy >= 20 && g.hunger >= 60) {
        r->kind = COMPANION_VISIT;
        r->target = g.gift.kind;
    } else if (g.hygiene < 90) {
        r->kind = COMPANION_CLEAN;
    } else if (!g.sick && g.energy >= 30 && g.hunger < 90) {
        uint8_t unlocked[PREY_SPECIES_COUNT], count = 0;
        for (unsigned i = 0; i < PREY_SPECIES_COUNT; i++)
            if (species_unlocked((prey_species_t)i)) unlocked[count++] = i;
        if (count) {
            r->kind = COMPANION_HUNT;
            r->target = unlocked[(mix >> 8) % count];
        }
    }
    /* Two identical pet requests would pay twice for the same three taps. */
    if (r->kind == s->requests[0].kind) s->requests[0].kind = COMPANION_PLAY;
}

static bool ensure_day(uint32_t day, uint8_t mode)
{
    companion_state_t *s = &g.companion;
    if (!s->initialized) {
        s->initialized = 1;
        s->clock_mode = mode;
        s->seen_achievements = g.achievements;
        s->bond_level = g.bond / 25;
        s->last_stage = g.stage;
        s->was_sick = !!g.sick;
        new_requests(day);
        return true;
    }
    if (s->clock_mode == 2 && mode == 1) {
        s->clock_mode = 1;
        s->request_day = day;
        return true;
    }
    if (s->clock_mode != mode || day <= s->request_day) return false;
    new_requests(day);
    return true;
}

static void persist(void)
{
    if (!s_testing && !game_save_now()) ESP_LOGW(TAG, "companion save rejected");
}

void companion_tick(void)
{
    time_t now = time(NULL);
    bool changed = ensure_day(now >= 1704067200 ? (uint32_t)(now / 86400) : g.age_ticks / 2880,
                              now >= 1704067200 ? 1 : 2);
    companion_state_t *s = &g.companion;
    uint8_t level = g.bond / 25;
    if (level > s->bond_level) {
        for (uint8_t i = s->bond_level + 1; i <= level; i++) remember(4, i);
        s->bond_level = level;
        changed = true;
    }
    if (g.stage > s->last_stage) { remember(5, g.stage); changed = true; }
    if (s->was_sick && !g.sick) { remember(6, 0); changed = true; }
    if (s->last_stage != g.stage || s->was_sick != !!g.sick) changed = true;
    s->last_stage = g.stage;
    s->was_sick = !!g.sick;
    uint32_t fresh = g.achievements & ~s->seen_achievements;
    for (unsigned i = 0; i < 20; i++) if (fresh & (1u << i)) remember(7, i);
    if (fresh) { s->seen_achievements |= fresh; changed = true; }
    if (changed) persist();
}

void companion_event(companion_event_t event, uint8_t value)
{
    if ((event == COMPANION_PLAY && value >= 3) ||
        (event == COMPANION_TREAT && value >= 12) ||
        (event == COMPANION_VISIT && value >= 36) ||
        (event == COMPANION_HUNT && value >= 8) || event < 1 || event > 6) {
        ESP_LOGW(TAG, "invalid interaction %u/%u", event, value);
        return;
    }
    if (!s_testing) companion_tick();
    companion_state_t *s = &g.companion;
    uint8_t *count = NULL, *favorite = NULL, memory = 0;
    if (event == COMPANION_TREAT) { count = &s->treat_counts[value]; favorite = &s->favorite_treat; memory = 1; }
    if (event == COMPANION_VISIT) { count = &s->gift_counts[value]; favorite = &s->favorite_gift; memory = 2; }
    if (event == COMPANION_PLAY) { count = &s->play_counts[value]; favorite = &s->favorite_play; memory = 3; }
    if (count) {
        if (*count < 255) (*count)++;
        if (*favorite == 255 && *count >= 3) {
            *favorite = value;
            remember(memory, value);
            if (!s_testing) ui_hud_bubble("A Favorite Discovered / Menu > Your Spider", 0x76d6b1, 2600);
        }
        if (*favorite == value) {
            g.mood = g.mood > 98 ? 100 : g.mood + 2;
            if (!s_testing) cspider_pet_react();
        }
    }
    for (unsigned i = 0; i < 2; i++) {
        companion_request_t *r = &s->requests[i];
        if (r->state || r->kind != event || (r->target != 255 && r->target != value)) continue;
        if (r->progress < goal(r->kind)) r->progress++;
        if (r->progress == goal(r->kind)) {
            r->state = 1;
            game_add_silk(5);
            remember(8, 1);
            if (!s_testing) ui_hud_bubble("Request Complete / +5 Silk", 0x76d6b1, 2200);
        }
    }
    persist();
}

bool companion_dismiss(unsigned slot)
{
    if (slot >= 2 || g.companion.requests[slot].state) {
        ESP_LOGW(TAG, "request dismissal rejected: %u", slot);
        return false;
    }
    g.companion.requests[slot].state = 2;
    persist();
    return true;
}

bool companion_pin(unsigned slot)
{
    companion_state_t *s = &g.companion;
    if (slot >= s->diary_count) { ESP_LOGW(TAG, "invalid diary slot %u", slot); return false; }
    unsigned pins = 0;
    for (unsigned i = 0; i < s->diary_count; i++) pins += s->diary[i].pinned;
    if (!s->diary[slot].pinned && pins >= COMPANION_PIN_CAP) {
        ESP_LOGW(TAG, "pin rejected: eight-memory limit");
        if (!s_testing) ui_hud_bubble("Eight Pins Full / Unpin a Memory First", 0xf5c86b, 2400);
        return false;
    }
    s->diary[slot].pinned ^= 1;
    persist();
    return true;
}

void companion_request_text(unsigned slot, char *title, size_t ts, char *detail, size_t ds)
{
    if (slot >= 2) { snprintf(title, ts, "Unavailable"); snprintf(detail, ds, "Invalid Request"); return; }
    const companion_request_t *r = &g.companion.requests[slot];
    switch (r->kind) {
    case COMPANION_PET: snprintf(title, ts, "A Little Attention"); break;
    case COMPANION_PLAY: snprintf(title, ts, "Play Together"); break;
    case COMPANION_HUNT: snprintf(title, ts, "Catch %s Together", r->target == 255 ? "Any Prey" : species_get(r->target)->name); break;
    case COMPANION_VISIT: snprintf(title, ts, "A Cozy Visit"); break;
    default: snprintf(title, ts, "Freshen the Enclosure"); break;
    }
    const char *action = r->kind == COMPANION_PET ? "Pet Gently" :
        r->kind == COMPANION_PLAY ? (r->target == 255 ? "Finish Any Minigame" : s_games[r->target]) :
        r->kind == COMPANION_HUNT ? "Manual Catch" :
        r->kind == COMPANION_VISIT ? (r->target == 255 ? "Visit Any Furnishing" : gift_kind_name(r->target)) : "Clean When Needed";
    snprintf(detail, ds, "%s / %u of %u\n%s", action, r->progress, goal(r->kind),
             r->state == 1 ? "Complete / +5 Silk Paid" :
             r->state == 2 ? "Not Today / No Penalty" : "Optional / +5 Silk");
}

void companion_memory_text(unsigned slot, char *title, size_t ts, char *detail, size_t ds)
{
    if (slot >= g.companion.diary_count) {
        snprintf(title, ts, "No Memories Yet"); snprintf(detail, ds, "Make Memories Together"); return;
    }
    const companion_memory_t *m = &g.companion.diary[slot];
    switch (m->kind) {
    case 1: snprintf(title, ts, "Favorite Treat"); snprintf(detail, ds, "Enjoys %s", treat_name(m->value)); break;
    case 2: snprintf(title, ts, "Favorite Place"); snprintf(detail, ds, "Feels at Home: %s", gift_kind_name(m->value)); break;
    case 3: snprintf(title, ts, "Favorite Game"); snprintf(detail, ds, "Enjoys %s Together", s_games[m->value]); break;
    case 4: snprintf(title, ts, "Growing Closer"); snprintf(detail, ds, "Reached Bond %u", m->value * 25); break;
    case 5: snprintf(title, ts, "Growing Up"); snprintf(detail, ds, "Became %s", m->value == 1 ? "a Juvenile" : "an Adult"); break;
    case 6: snprintf(title, ts, "Feeling Better"); snprintf(detail, ds, "Recovered from Sickness"); break;
    case 7: snprintf(title, ts, "A Milestone Together"); snprintf(detail, ds, "Earned %s", ACHIEVEMENTS[m->value].name); break;
    default: snprintf(title, ts, "Time Together"); snprintf(detail, ds, "%u Optional Request%s Completed", m->value, m->value == 1 ? "" : "s"); break;
    }
}

bool companion_test(void)
{
    game_state_t *backup = heap_caps_malloc(sizeof g, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!backup) { ESP_LOGE(TAG, "test backup allocation failed"); return false; }
    *backup = g;
    s_testing = true;
    companion_defaults(&g.companion);
    g.seed = 7; g.sick = 0; g.bond = 0; g.stage = 0; g.gift.used = 0; g.silk = 100;
    g.hygiene = 100; g.energy = 80; g.hunger = 70;
    bool ok = companion_valid(&g.companion) && ensure_day(5, 2);
    g.companion.requests[0] = (companion_request_t){COMPANION_PET, 255, 0, 0};
    uint32_t silk = g.silk;
    for (unsigned i = 0; i < 4; i++) companion_event(COMPANION_PET, 0);
    ok = ok && g.companion.requests[0].state == 1 && g.companion.diary_count == 1 &&
        g.companion.diary[0].value == 1 && g.silk == silk + 5 && !ensure_day(4, 2);
    ok = ensure_day(20000, 1) && g.companion.requests[0].state == 1 && ok;
    ok = !ensure_day(6, 2) && !ensure_day(19999, 1) && ok;
    for (unsigned i = 0; i < 3; i++) companion_event(COMPANION_TREAT, 2);
    ok = ok && g.companion.favorite_treat == 2 && companion_valid(&g.companion);
    for (unsigned i = 0; i < 3; i++) companion_event(COMPANION_TREAT, 3);
    ok = ok && g.companion.favorite_treat == 2;
    for (unsigned i = 0; i < 3; i++) {
        companion_event(COMPANION_PLAY, 1);
        companion_event(COMPANION_VISIT, 0);
    }
    ok = ok && g.companion.favorite_play == 1 && g.companion.favorite_gift == 0;
    ok = companion_dismiss(1) && !companion_dismiss(1) && ok;
    for (unsigned i = g.companion.diary_count; i < 64; i++) remember(7, 0);
    for (unsigned i = 0; i < 8; i++) ok = companion_pin(i) && ok;
    ok = !companion_pin(8) && ok;
    remember(6, 0);
    ok = ok && g.companion.diary_count == 64 && g.companion.diary[0].pinned &&
        g.companion.diary[63].kind == 6 && companion_valid(&g.companion);
    g.companion.requests[0].progress = 255;
    ok = !companion_valid(&g.companion) && ok;
    g = *backup;
    heap_caps_free(backup);
    s_testing = false;
    ESP_LOGI(TAG, "preferences/requests/clock/pins/eviction/validation %s", ok ? "PASS" : "FAIL");
    return ok;
}
