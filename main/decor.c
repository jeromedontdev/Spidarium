/*
 * Spidarium — placeable decor.
 *
 * See decor.h for the model. The important design point is that blocking is a
 * STEERING input: decor_nearest_blocking() reports the nearest blocking item
 * and behaviour.c turns away from it before the spider reaches it. Nothing here
 * shoves the spider, so it walks around an item instead of grinding against it.
 */
#include "decor.h"

#include "game.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

static const char *TAG = "spidarium.decor";

/* Shop order. Costs are tuned against the treat prices (12..80 Silk) so decor
 * is a long-term sink rather than something you buy out in one session. */
static const decor_def_t k_decor_kinds[DECOR_COUNT] = {
    { "Small Rock",   "spider walks over it",   18,  1,  9, 0, "decor_rock_small" },
    { "Large Rock",   "spider walks around it", 34,  2, 16, 1, "decor_rock_large" },
    { "Pebbles",      "spider walks over it",   14,  1,  8, 0, "decor_pebbles" },
    { "Log",          "spider walks around it", 46,  4, 22, 1, "decor_log" },
    { "Bark Slab",    "spider walks over it",   22,  3, 14, 0, "decor_bark" },
    { "Fern",         "spider walks over it",   38,  5, 15, 0, "decor_fern" },
    { "Moss Clump",   "spider walks over it",   20,  2, 11, 0, "decor_moss_clump" },
    { "Succulent",    "spider walks around it", 52,  7, 14, 1, "decor_succulent" },
    { "White Flower", "spider walks over it",   26,  3,  8, 0, "decor_flower_white" },
    { "Pink Flower",  "spider walks over it",   30,  6,  8, 0, "decor_flower_pink" },
    { "Mushroom",     "spider walks over it",   28,  4, 10, 0, "decor_mushroom" },
    { "Shell",        "spider walks over it",   24,  5,  9, 0, "decor_shell" },
    { "Crystal",      "spider walks around it", 68,  9, 12, 1, "decor_crystal" },
    { "Leaf Pile",    "spider walks over it",   16,  1, 13, 0, "decor_leaf_pile" },
    { "Twig",         "spider walks over it",   12,  1, 12, 0, "decor_twig" },
    { "Bone",         "spider walks around it", 58,  8, 13, 1, "decor_bone" },
};

const decor_def_t *decor_def(uint8_t kind)
{
    return kind < DECOR_COUNT ? &k_decor_kinds[kind] : NULL;
}

bool decor_unlocked(uint8_t kind)
{
    const decor_def_t *def = decor_def(kind);
    return def && g.level >= def->unlock_level;
}

bool decor_owned(uint8_t kind)
{
    return kind < DECOR_COUNT && (g.decor_owned & (1u << kind)) != 0;
}

bool decor_can_buy(uint8_t kind)
{
    const decor_def_t *def = decor_def(kind);
    if (!def) return false;
    if (!decor_unlocked(kind)) return false;
    return g.silk >= def->cost;
}

bool decor_buy(uint8_t kind)
{
    const decor_def_t *def = decor_def(kind);
    if (!def) {
        ESP_LOGW(TAG, "buy rejected: kind %u out of range", kind);
        return false;
    }
    if (!decor_unlocked(kind)) {
        ESP_LOGW(TAG, "buy rejected: %s needs level %u (have %lu)", def->name,
                 def->unlock_level, (unsigned long)g.level);
        return false;
    }
    if (g.silk < def->cost) {
        ESP_LOGW(TAG, "buy rejected: %s costs %u Silk (have %lu)", def->name,
                 def->cost, (unsigned long)g.silk);
        return false;
    }
    g.silk -= def->cost;
    g.decor_owned |= 1u << kind;
    ESP_LOGI(TAG, "bought %s for %u Silk (%lu left)", def->name, def->cost,
             (unsigned long)g.silk);
    return true;
}

int decor_placed_count(void)
{
    int count = 0;
    for (int i = 0; i < DECOR_SLOTS; i++)
        if (g.decor[i].kind != 0xFF) count++;
    return count;
}

const decor_item_t *decor_at(int slot)
{
    if (slot < 0 || slot >= DECOR_SLOTS) return NULL;
    return g.decor[slot].kind == 0xFF ? NULL : &g.decor[slot];
}

/* Keep a placed item fully inside the playfield. */
static void clamp_to_playfield(int16_t *x, int16_t *y, uint8_t radius)
{
    int32_t lo_x = g_play.left + radius, hi_x = g_play.right - radius;
    int32_t lo_y = g_play.top + radius, hi_y = g_play.bottom - radius;
    if (lo_x > hi_x) lo_x = hi_x = (g_play.left + g_play.right) / 2;
    if (lo_y > hi_y) lo_y = hi_y = (g_play.top + g_play.bottom) / 2;
    if (*x < lo_x) *x = (int16_t)lo_x;
    if (*x > hi_x) *x = (int16_t)hi_x;
    if (*y < lo_y) *y = (int16_t)lo_y;
    if (*y > hi_y) *y = (int16_t)hi_y;
}

int decor_place(uint8_t kind, int16_t x, int16_t y)
{
    const decor_def_t *def = decor_def(kind);
    if (!def) {
        ESP_LOGW(TAG, "place rejected: kind %u out of range", kind);
        return -1;
    }
    if (!decor_owned(kind)) {
        ESP_LOGW(TAG, "place rejected: %s not owned", def->name);
        return -1;
    }
    int slot = -1;
    for (int i = 0; i < DECOR_SLOTS; i++)
        if (g.decor[i].kind == 0xFF) { slot = i; break; }
    if (slot < 0) {
        ESP_LOGW(TAG, "place rejected: all %d slots full", DECOR_SLOTS);
        return -1;
    }
    clamp_to_playfield(&x, &y, def->radius);
    g.decor[slot].kind = kind;
    g.decor[slot].blocking = def->blocking;
    g.decor[slot].x = x;
    g.decor[slot].y = y;
    ESP_LOGI(TAG, "placed %s at (%d,%d) slot %d blocking=%u", def->name, x, y,
             slot, g.decor[slot].blocking);
    return slot;
}

bool decor_move(int slot, int16_t x, int16_t y)
{
    if (slot < 0 || slot >= DECOR_SLOTS) return false;
    if (g.decor[slot].kind == 0xFF) return false;
    const decor_def_t *def = decor_def(g.decor[slot].kind);
    if (!def) return false;
    clamp_to_playfield(&x, &y, def->radius);
    g.decor[slot].x = x;
    g.decor[slot].y = y;
    ESP_LOGI(TAG, "moved slot %d to (%d,%d)", slot, x, y);
    return true;
}

bool decor_remove(int slot)
{
    if (slot < 0 || slot >= DECOR_SLOTS) return false;
    if (g.decor[slot].kind == 0xFF) return false;
    ESP_LOGI(TAG, "removed slot %d", slot);
    g.decor[slot].kind = 0xFF;
    g.decor[slot].blocking = 0;
    g.decor[slot].x = 0;
    g.decor[slot].y = 0;
    return true;
}

bool decor_toggle_blocking(int slot)
{
    if (slot < 0 || slot >= DECOR_SLOTS) return false;
    if (g.decor[slot].kind == 0xFF) return false;
    g.decor[slot].blocking = g.decor[slot].blocking ? 0 : 1;
    ESP_LOGI(TAG, "slot %d blocking=%u", slot, g.decor[slot].blocking);
    return true;
}

int decor_hit_test(int16_t x, int16_t y)
{
    /* Later slots are drawn on top, so search backwards to pick the visible one. */
    for (int i = DECOR_SLOTS - 1; i >= 0; i--) {
        if (g.decor[i].kind == 0xFF) continue;
        const decor_def_t *def = decor_def(g.decor[i].kind);
        if (!def) continue;
        float dx = (float)(x - g.decor[i].x);
        float dy = (float)(y - g.decor[i].y);
        if (dx * dx + dy * dy <= (float)def->radius * def->radius) return i;
    }
    return -1;
}

bool decor_blocks_point(int16_t x, int16_t y)
{
    for (int i = 0; i < DECOR_SLOTS; i++) {
        if (g.decor[i].kind == 0xFF || !g.decor[i].blocking) continue;
        const decor_def_t *def = decor_def(g.decor[i].kind);
        if (!def) continue;
        float dx = (float)(x - g.decor[i].x);
        float dy = (float)(y - g.decor[i].y);
        if (dx * dx + dy * dy <= (float)def->radius * def->radius) return true;
    }
    return false;
}

bool decor_nearest_blocking(int16_t x, int16_t y, float max_distance,
                            int16_t *out_x, int16_t *out_y, float *out_distance)
{
    float best = max_distance * max_distance;
    int found = -1;
    for (int i = 0; i < DECOR_SLOTS; i++) {
        if (g.decor[i].kind == 0xFF || !g.decor[i].blocking) continue;
        float dx = (float)(g.decor[i].x - x);
        float dy = (float)(g.decor[i].y - y);
        float d2 = dx * dx + dy * dy;
        if (d2 < best) { best = d2; found = i; }
    }
    if (found < 0) return false;
    if (out_x) *out_x = g.decor[found].x;
    if (out_y) *out_y = g.decor[found].y;
    if (out_distance) *out_distance = sqrtf(best);
    return true;
}

void decor_clear_slots(decor_item_t *slots, int count)
{
    if (!slots) return;
    for (int i = 0; i < count; i++) {
        slots[i].kind = 0xFF;
        slots[i].blocking = 0;
        slots[i].x = 0;
        slots[i].y = 0;
    }
}

void decor_clear(void)
{
    decor_clear_slots(g.decor, DECOR_SLOTS);
}

bool decor_test(void)
{
    uint32_t silk = g.silk, owned = g.decor_owned, level = g.level;
    decor_item_t saved[DECOR_SLOTS];
    memcpy(saved, g.decor, sizeof saved);
    bool ok = true;

    decor_clear();
    g.decor_owned = 0;
    g.level = 1;
    g.silk = 0;

    /* Cannot buy without Silk, and cannot buy a locked kind. */
    ok = ok && !decor_can_buy(DECOR_ROCK_SMALL);
    ok = ok && !decor_buy(DECOR_ROCK_SMALL);
    ok = ok && !decor_owned(DECOR_ROCK_SMALL);
    ok = ok && !decor_unlocked(DECOR_CRYSTAL);

    /* Buy the cheapest unlocked kind. */
    g.silk = k_decor_kinds[DECOR_ROCK_SMALL].cost;
    ok = ok && decor_can_buy(DECOR_ROCK_SMALL);
    ok = ok && decor_buy(DECOR_ROCK_SMALL);
    ok = ok && decor_owned(DECOR_ROCK_SMALL);
    ok = ok && g.silk == 0;

    /* Placing an unowned kind is refused. */
    ok = ok && decor_place(DECOR_LOG, 100, 100) < 0;

    /* Placing an owned kind succeeds and records the default blocking flag. */
    int slot = decor_place(DECOR_ROCK_SMALL, 100, 100);
    ok = ok && slot >= 0;
    ok = ok && decor_placed_count() == 1;
    ok = ok && decor_at(slot) != NULL;
    ok = ok && decor_at(slot)->blocking == 0;
    ok = ok && !decor_blocks_point(100, 100);

    /* Toggling blocking makes the point blocked. */
    ok = ok && decor_toggle_blocking(slot);
    ok = ok && decor_at(slot)->blocking == 1;
    ok = ok && decor_blocks_point(100, 100);
    ok = ok && !decor_blocks_point(100 + k_decor_kinds[DECOR_ROCK_SMALL].radius + 4, 100);

    /* Nearest-blocking reports it, and only within range. */
    int16_t nx = 0, ny = 0;
    float nd = 0.0f;
    ok = ok && decor_nearest_blocking(110, 100, 60.0f, &nx, &ny, &nd);
    ok = ok && nx == 100 && ny == 100;
    ok = ok && !decor_nearest_blocking(110, 100, 2.0f, &nx, &ny, &nd);

    /* Hit testing finds the item, and removal frees the slot. */
    ok = ok && decor_hit_test(100, 100) == slot;
    ok = ok && decor_hit_test(100 + k_decor_kinds[DECOR_ROCK_SMALL].radius + 6, 100) < 0;

    /* Moving keeps the slot, the kind and the blocking flag, and clamps. */
    ok = ok && decor_move(slot, 200, 150);
    ok = ok && decor_at(slot)->x == 200 && decor_at(slot)->y == 150;
    ok = ok && decor_at(slot)->kind == DECOR_ROCK_SMALL;
    ok = ok && decor_at(slot)->blocking == 1;
    ok = ok && decor_hit_test(200, 150) == slot;
    ok = ok && decor_hit_test(100, 100) < 0;
    ok = ok && decor_move(slot, -900, -900);
    ok = ok && decor_at(slot)->x >= g_play.left && decor_at(slot)->y >= g_play.top;
    ok = ok && !decor_move(DECOR_SLOTS, 100, 100);

    ok = ok && decor_remove(slot);
    ok = ok && decor_placed_count() == 0;
    ok = ok && !decor_blocks_point(100, 100);

    /* A blocking kind defaults to blocking. */
    g.level = 9;
    g.silk = k_decor_kinds[DECOR_CRYSTAL].cost;
    ok = ok && decor_buy(DECOR_CRYSTAL);
    slot = decor_place(DECOR_CRYSTAL, 200, 200);
    ok = ok && slot >= 0 && decor_at(slot)->blocking == 1;

    /* Placement is clamped inside the playfield. */
    slot = decor_place(DECOR_ROCK_SMALL, -500, -500);
    ok = ok && slot >= 0;
    ok = ok && decor_at(slot)->x >= g_play.left;
    ok = ok && decor_at(slot)->y >= g_play.top;

    g.silk = silk; g.decor_owned = owned; g.level = level;
    memcpy(g.decor, saved, sizeof saved);
    return ok;
}
