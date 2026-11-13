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
#include "save.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
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
    { "Fern Planter", "spider walks around it", 48,  6, 14, 1, "decor_fern_pot" },
    { "Acorn Hide",   "spider shelters inside",72, 10, 15, 1, "decor_acorn_hide" },
    { "Driftwood Arch","spider walks around it",82, 12, 18, 1, "decor_driftwood_arch" },
    { "Web Hammock",  "spider walks over it",   64,  8, 15, 0, "decor_web_hammock" },
    { "Dew Pool",     "spider walks over it",   54,  7, 14, 0, "decor_dew_pool" },
    { "Mushroom Pair","spider walks over it",   62,  8, 13, 0, "decor_mushroom_pair" },
    { "Flower Planter","spider walks around it",68,  9, 15, 1, "decor_flower_planter" },
    { "Crystal Cluster","spider walks around it",98, 14, 15, 1, "decor_crystal_cluster" },
    { "Shell Cove",   "spider shelters inside",74, 11, 16, 1, "decor_shell_cove" },
    { "Twig Ladder",  "spider walks over it",   46,  6, 13, 0, "decor_twig_ladder" },
    { "Mossy Stone",  "spider walks over it",   42,  5, 13, 0, "decor_moss_stone" },
    { "Seed Pod",     "spider walks over it",   38,  4, 10, 0, "decor_seed_pod" },
    { "Silk Tent",    "spider shelters inside",112, 16, 19, 1, "decor_silk_tent" },
    { "Leaf Umbrella","spider walks around it", 88, 13, 15, 1, "decor_leaf_umbrella" },
    { "Bone Arch",    "spider walks around it", 96, 15, 16, 1, "decor_bone_arch" },
    { "Fossil",       "spider walks over it",   58,  8, 12, 0, "decor_fossil" },
    { "Lantern Cap",  "spider walks over it",   92, 14, 13, 0, "decor_lantern_cap" },
    { "Stone Bridge", "spider walks around it",120, 18, 20, 1, "decor_stone_bridge" },
    { "Fairy Ring", "woodland fairy / walk over", 44, 4, 17, 0, "decor_fairy_ring" },
    { "Foxglove Lantern", "woodland fairy / walk around", 58, 6, 12, 1, "decor_foxglove_lantern" },
    { "Pixie Door", "woodland fairy / walk around", 70, 8, 14, 1, "decor_pixie_door" },
    { "Gargoyle", "gothic / walk around", 78, 9, 15, 1, "decor_gargoyle" },
    { "Iron Gate", "gothic / walk around", 84, 10, 19, 1, "decor_iron_gate" },
    { "Rose Urn", "gothic / walk around", 54, 6, 12, 1, "decor_rose_urn" },
    { "Book Stack", "academia / walk around", 40, 3, 12, 1, "decor_book_stack" },
    { "Globe", "academia / walk around", 66, 7, 13, 1, "decor_globe" },
    { "Quill Desk", "academia / walk around", 90, 11, 19, 1, "decor_quill_desk" },
    { "Tea Cup", "household / walk around", 32, 2, 13, 1, "decor_tea_cup" },
    { "Spool", "household / walk around", 26, 2, 10, 1, "decor_spool" },
    { "Thimble", "household / walk around", 30, 3, 10, 1, "decor_thimble" },
    { "Cauldron", "witchy / walk around", 74, 8, 17, 1, "decor_cauldron" },
    { "Broom", "witchy / walk over", 38, 4, 15, 0, "decor_broom" },
    { "Potion Rack", "witchy / walk around", 86, 10, 17, 1, "decor_potion_rack" },
    { "Amber Fossil", "prehistoric / walk over", 58, 6, 12, 0, "decor_amber_fossil" },
    { "Cycad", "prehistoric / walk around", 62, 7, 16, 1, "decor_cycad" },
    { "Dinosaur Egg", "prehistoric / walk around", 72, 9, 13, 1, "decor_dinosaur_egg" },
    { "Neon Sign", "cyberpunk / walk around", 80, 9, 14, 1, "decor_neon_sign" },
    { "Holo Terminal", "cyberpunk / walk around", 96, 12, 15, 1, "decor_holo_terminal" },
    { "Cable Coil", "cyberpunk / walk over", 42, 4, 12, 0, "decor_cable_coil" },
    { "Moon Rover", "sci-fi / walk around", 104, 13, 18, 1, "decor_moon_rover" },
    { "Solar Array", "sci-fi / walk around", 88, 10, 18, 1, "decor_solar_array" },
    { "Oxygen Pod", "sci-fi / walk around", 92, 11, 14, 1, "decor_oxygen_pod" },
    { "Wizard Tower", "fantasy / walk around", 112, 14, 17, 1, "decor_wizard_tower" },
    { "Dragon Statue", "fantasy / walk around", 120, 16, 18, 1, "decor_dragon_statue" },
    { "Treasure Chest", "fantasy / walk around", 98, 12, 15, 1, "decor_treasure_chest" },
    { "Sun Obelisk", "lost civilization / walk around", 108, 13, 13, 1, "decor_sun_obelisk" },
    { "Glyph Tablet", "lost civilization / walk over", 64, 7, 14, 0, "decor_glyph_tablet" },
    { "Broken Column", "lost civilization / walk around", 76, 8, 15, 1, "decor_broken_column" },
    { "Stone Lantern", "Japanese zen / walk around", 70, 6, 13, 1, "decor_stone_lantern" },
    { "Bamboo Fountain", "Japanese zen / walk around", 84, 9, 16, 1, "decor_bamboo_fountain" },
    { "Bonsai", "Japanese zen / walk around", 94, 11, 16, 1, "decor_bonsai" },
    { "Fairy Bridge", "woodland fairy / walk around", 102, 12, 20, 1, "decor_fairy_bridge" },
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
    if (kind >= DECOR_COUNT) return false;
    if (kind < 32) return (g.decor_owned & (1u << kind)) != 0;
    if (kind < 64) return (g.decor_owned_extra & (1u << (kind - 32))) != 0;
    return (g.catalogue_owned_high & (1u << (kind - 64 + 4))) != 0;
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
    if (kind < 32) g.decor_owned |= 1u << kind;
    else if (kind < 64) g.decor_owned_extra |= 1u << (kind - 32);
    else g.catalogue_owned_high |= 1u << (kind - 64 + 4);
    ESP_LOGI(TAG, "bought %s for %u Silk (%lu left)", def->name, def->cost,
             (unsigned long)g.silk);
    return true;
}

static decor_item_t *s_extra;

int decor_slot_count(void) { return DECOR_SLOTS + (int)g.decor_extra_count; }
const decor_item_t *decor_extra_data(void) { return s_extra; }

decor_item_t *decor_detach_extra(void)
{
    decor_item_t *saved = s_extra;
    s_extra = NULL;
    g.decor_extra_count = 0;
    return saved;
}

void decor_restore_extra(decor_item_t *items)
{
    heap_caps_free(s_extra);
    s_extra = items;
}

static decor_item_t *slot_at(int slot)
{
    if (slot < 0 || slot >= decor_slot_count()) return NULL;
    return slot < DECOR_SLOTS ? &g.decor[slot] : &s_extra[slot - DECOR_SLOTS];
}

int decor_placed_count(void)
{
    int count = 0;
    for (int i = 0; i < decor_slot_count(); i++)
        if (slot_at(i)->kind != 0xFF) count++;
    return count;
}

const decor_item_t *decor_at(int slot)
{
    const decor_item_t *item = slot_at(slot);
    return item && item->kind != 0xFF ? item : NULL;
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
    for (int i = 0; i < decor_slot_count(); i++)
        if (slot_at(i)->kind == 0xFF) { slot = i; break; }
    if (slot < 0) {
        size_t count = g.decor_extra_count + 1u;
        if (count > (SAVE_MAX_PAYLOAD - sizeof g) / sizeof *s_extra) {
            ESP_LOGE(TAG, "place rejected: NVS storage capacity exhausted");
            return -1;
        }
        decor_item_t *grown = heap_caps_realloc(s_extra, count * sizeof *grown,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!grown) { ESP_LOGE(TAG, "place rejected: PSRAM exhausted"); return -1; }
        s_extra = grown;
        slot = decor_slot_count();
        g.decor_extra_count = count;
    }
    clamp_to_playfield(&x, &y, def->radius);
    *slot_at(slot) = (decor_item_t){kind, def->blocking, x, y};
    ESP_LOGI(TAG, "placed %s at (%d,%d) slot %d blocking=%u", def->name, x, y,
             slot, slot_at(slot)->blocking);
    return slot;
}

bool decor_move(int slot, int16_t x, int16_t y)
{
    decor_item_t *item = slot_at(slot);
    if (!item || item->kind == 0xFF) return false;
    const decor_def_t *def = decor_def(item->kind);
    if (!def) return false;
    clamp_to_playfield(&x, &y, def->radius);
    item->x = x;
    item->y = y;
    ESP_LOGI(TAG, "moved slot %d to (%d,%d)", slot, x, y);
    return true;
}

bool decor_remove(int slot)
{
    decor_item_t *item = slot_at(slot);
    if (!item || item->kind == 0xFF) return false;
    ESP_LOGI(TAG, "removed slot %d", slot);
    *item = (decor_item_t){.kind = 0xFF};
    return true;
}

bool decor_toggle_blocking(int slot)
{
    decor_item_t *item = slot_at(slot);
    if (!item || item->kind == 0xFF) return false;
    return decor_set_path(slot, (item->blocking + 1) % DECOR_PATH_COUNT);
}

bool decor_set_path(int slot, uint8_t path)
{
    decor_item_t *item = slot_at(slot);
    if (!item || item->kind == 0xFF || path >= DECOR_PATH_COUNT) {
        ESP_LOGW(TAG, "path rejected: slot=%d path=%u", slot, path);
        return false;
    }
    item->blocking = path;
    ESP_LOGI(TAG, "slot %d blocking=%u", slot, item->blocking);
    return true;
}

static bool same_band(uint8_t a, uint8_t b)
{
    return (a == DECOR_WALK_BEHIND) == (b == DECOR_WALK_BEHIND);
}

int decor_layer_count(uint8_t path, int exclude_slot)
{
    int count = 0;
    for (int i = 0; i < decor_slot_count(); i++) {
        const decor_item_t *item = decor_at(i);
        if (item && i != exclude_slot && same_band(item->blocking, path)) count++;
    }
    return count;
}

int decor_layer_index(int slot)
{
    const decor_item_t *item = decor_at(slot);
    if (!item) return -1;
    int index = 0;
    for (int i = 0; i < slot; i++) {
        const decor_item_t *other = decor_at(i);
        if (other && same_band(item->blocking, other->blocking)) index++;
    }
    return index;
}

int decor_set_layer(int slot, int index)
{
    const decor_item_t *item = decor_at(slot);
    if (!item || index < 0 || index >= decor_layer_count(item->blocking, -1)) {
        ESP_LOGW(TAG, "layer rejected: slot=%d index=%d", slot, index);
        return -1;
    }
    int current = decor_layer_index(slot);
    while (current != index) {
        int step = current < index ? 1 : -1;
        int next = slot + step;
        while (next >= 0 && next < decor_slot_count()) {
            const decor_item_t *other = decor_at(next);
            if (other && same_band(item->blocking, other->blocking)) break;
            next += step;
        }
        decor_item_t swap = *slot_at(next);
        *slot_at(next) = *slot_at(slot);
        *slot_at(slot) = swap;
        slot = next;
        item = decor_at(slot);
        current += step;
    }
    return slot;
}

int decor_hit_test(int16_t x, int16_t y)
{
    for (int band = 1; band >= 0; band--)
    for (int i = decor_slot_count() - 1; i >= 0; i--) {
        const decor_item_t *item = decor_at(i);
        if (!item || (item->blocking == DECOR_WALK_BEHIND) != band) continue;
        const decor_def_t *def = decor_def(item->kind);
        if (!def) continue;
        float dx = (float)(x - item->x);
        float dy = (float)(y - item->y);
        float hit = def->radius < 28 ? 28 : def->radius;
        if (dx * dx + dy * dy <= hit * hit) return i;
    }
    return -1;
}

bool decor_blocks_point(int16_t x, int16_t y)
{
    for (int i = 0; i < decor_slot_count(); i++) {
        const decor_item_t *item = decor_at(i);
        if (!item || item->blocking != DECOR_SOLID) continue;
        const decor_def_t *def = decor_def(item->kind);
        if (!def) continue;
        float dx = (float)(x - item->x);
        float dy = (float)(y - item->y);
        if (dx * dx + dy * dy <= (float)def->radius * def->radius) return true;
    }
    return false;
}

bool decor_nearest_blocking(int16_t x, int16_t y, float max_distance,
                            int16_t *out_x, int16_t *out_y, float *out_distance)
{
    float best = max_distance * max_distance;
    int found = -1;
    for (int i = 0; i < decor_slot_count(); i++) {
        const decor_item_t *item = decor_at(i);
        if (!item || item->blocking != DECOR_SOLID) continue;
        float dx = (float)(item->x - x);
        float dy = (float)(item->y - y);
        float d2 = dx * dx + dy * dy;
        if (d2 < best) { best = d2; found = i; }
    }
    if (found < 0) return false;
    if (out_x) *out_x = decor_at(found)->x;
    if (out_y) *out_y = decor_at(found)->y;
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
    heap_caps_free(s_extra);
    s_extra = NULL;
    g.decor_extra_count = 0;
}

bool decor_test(void)
{
    uint32_t extra_count = g.decor_extra_count;
    decor_item_t *extra = decor_detach_extra();
    uint32_t silk = g.silk, owned = g.decor_owned, level = g.level;
    uint32_t owned_extra = g.decor_owned_extra;
    uint8_t high_owned = g.catalogue_owned_high;
    decor_item_t saved[DECOR_SLOTS];
    memcpy(saved, g.decor, sizeof saved);
    bool ok = true;

    decor_clear();
    g.decor_owned = 0;
    g.decor_owned_extra = 0;
    g.catalogue_owned_high &= 0x0F;
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
    ok = ok && decor_hit_test(127, 100) == slot;
    ok = ok && decor_hit_test(129, 100) < 0;

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

    decor_clear();
    int a = decor_place(DECOR_ROCK_SMALL, 200, 200);
    int b = decor_place(DECOR_CRYSTAL, 200, 200);
    int c = decor_place(DECOR_ROCK_SMALL, 200, 200);
    ok = ok && decor_set_path(b, DECOR_WALK_BEHIND);
    ok = ok && !decor_blocks_point(200, 200) &&
         !decor_nearest_blocking(200, 200, 100, NULL, NULL, NULL);
    ok = ok && decor_hit_test(200, 200) == b && decor_layer_count(0, -1) == 2;
    ok = ok && decor_layer_count(2, -1) == 1 && decor_layer_index(c) == 1;
    int reordered = decor_set_layer(c, 0);
    ok = ok && reordered == a && decor_layer_index(reordered) == 0;
    ok = ok && decor_at(b)->blocking == DECOR_WALK_BEHIND && decor_hit_test(200, 200) == b;
    ok = ok && !decor_set_path(a, 3) && decor_set_layer(a, -1) < 0;
    ok = ok && decor_set_layer(a, 2) < 0 && decor_layer_index(a) == 0;
    ok = ok && decor_set_path(b, DECOR_SOLID) && decor_blocks_point(200, 200);
    ok = ok && decor_set_layer(b, 2) >= 0;

    g.level = 40;
    g.silk = 100000;
    for (int i = 32; i < DECOR_COUNT; i++)
        ok = decor_buy(i) && decor_owned(i) && ok;
    g.catalogue_owned_high = high_owned;
    g.silk = silk; g.decor_owned = owned; g.level = level;
    g.decor_owned_extra = owned_extra; g.level = level;
    memcpy(g.decor, saved, sizeof saved);
    decor_restore_extra(extra);
    g.decor_extra_count = extra_count;
    return ok;
}
