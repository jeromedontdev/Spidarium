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

#ifndef PI_F
#define PI_F 3.14159265f
#endif

static const char *TAG = "spidarium.decor";

/* Shop order. Costs are tuned against the treat prices (12..80 Silk) so decor
 * is a long-term sink rather than something you buy out in one session. */
static const decor_def_t k_decor_kinds[DECOR_COUNT] = {
    { "Small Rock", "spider walks over it", 18, 1, 9, 0, "decor_rock_small", 6 },
    { "Large Rock", "spider walks around it", 34, 2, 16, 1, "decor_rock_large", 14 },
    { "Pebbles", "spider walks over it", 14, 1, 8, 0, "decor_pebbles", 3 },
    { "Log", "spider walks around it", 46, 4, 22, 1, "decor_log", 16 },
    { "Bark Slab", "spider walks over it", 22, 3, 14, 0, "decor_bark", 7 },
    { "Fern", "spider walks over it", 38, 5, 15, 0, "decor_fern", 0 },
    { "Moss Clump", "spider walks over it", 20, 2, 11, 0, "decor_moss_clump", 4 },
    { "Succulent", "spider walks around it", 52, 7, 14, 1, "decor_succulent", 0 },
    { "White Flower", "spider walks over it", 26, 3, 8, 0, "decor_flower_white", 0 },
    { "Pink Flower", "spider walks over it", 30, 6, 8, 0, "decor_flower_pink", 0 },
    { "Mushroom", "spider walks over it", 28, 4, 10, 0, "decor_mushroom", 5 },
    { "Shell", "spider walks over it", 24, 5, 9, 0, "decor_shell", 4 },
    { "Crystal", "spider walks around it", 68, 9, 12, 1, "decor_crystal", 12 },
    { "Leaf Pile", "spider walks over it", 16, 1, 13, 0, "decor_leaf_pile", 3 },
    { "Twig", "spider walks over it", 12, 1, 12, 0, "decor_twig", 3 },
    { "Bone", "spider walks around it", 58, 8, 13, 1, "decor_bone", 6 },
    { "Fern Planter", "spider walks around it", 48, 6, 14, 1, "decor_fern_pot", 0 },
    { "Acorn Hide", "spider shelters inside", 72, 10, 15, 1, "decor_acorn_hide", 10 },
    { "Driftwood Arch", "spider walks around it", 82, 12, 18, 1, "decor_driftwood_arch", 18 },
    { "Web Hammock", "spider walks over it", 64, 8, 15, 0, "decor_web_hammock", 8 },
    { "Dew Pool", "spider walks over it", 54, 7, 14, 0, "decor_dew_pool", 0 },
    { "Mushroom Pair", "spider walks over it", 62, 8, 13, 0, "decor_mushroom_pair", 6 },
    { "Flower Planter", "spider walks around it", 68, 9, 15, 1, "decor_flower_planter", 0 },
    { "Crystal Cluster", "spider walks around it", 98, 14, 15, 1, "decor_crystal_cluster", 16 },
    { "Shell Cove", "spider shelters inside", 74, 11, 16, 1, "decor_shell_cove", 8 },
    { "Twig Ladder", "spider walks over it", 46, 6, 13, 0, "decor_twig_ladder", 12 },
    { "Mossy Stone", "spider walks over it", 42, 5, 13, 0, "decor_moss_stone", 8 },
    { "Seed Pod", "spider walks over it", 38, 4, 10, 0, "decor_seed_pod", 4 },
    { "Silk Tent", "spider shelters inside", 112, 16, 19, 1, "decor_silk_tent", 14 },
    { "Leaf Umbrella", "spider walks around it", 88, 13, 15, 1, "decor_leaf_umbrella", 0 },
    { "Bone Arch", "spider walks around it", 96, 15, 16, 1, "decor_bone_arch", 16 },
    { "Fossil", "spider walks over it", 58, 8, 12, 0, "decor_fossil", 5 },
    { "Lantern Cap", "spider walks over it", 92, 14, 13, 0, "decor_lantern_cap", 9 },
    { "Stone Bridge", "spider walks around it", 120, 18, 20, 1, "decor_stone_bridge", 18 },
    { "Fairy Ring", "woodland fairy / walk over", 44, 4, 17, 0, "decor_fairy_ring", 0 },
    { "Foxglove Lantern", "woodland fairy / walk around", 58, 6, 12, 1, "decor_foxglove_lantern", 0 },
    { "Pixie Door", "woodland fairy / walk around", 70, 8, 14, 1, "decor_pixie_door", 0 },
    { "Gargoyle", "gothic / walk around", 78, 9, 15, 1, "decor_gargoyle", 14 },
    { "Iron Gate", "gothic / walk around", 84, 10, 19, 1, "decor_iron_gate", 18 },
    { "Rose Urn", "gothic / walk around", 54, 6, 12, 1, "decor_rose_urn", 10 },
    { "Book Stack", "academia / walk around", 40, 3, 12, 1, "decor_book_stack", 8 },
    { "Globe", "academia / walk around", 66, 7, 13, 1, "decor_globe", 0 },
    { "Quill Desk", "academia / walk around", 90, 11, 19, 1, "decor_quill_desk", 14 },
    { "Tea Cup", "household / walk around", 32, 2, 13, 1, "decor_tea_cup", 7 },
    { "Spool", "household / walk around", 26, 2, 10, 1, "decor_spool", 6 },
    { "Thimble", "household / walk around", 30, 3, 10, 1, "decor_thimble", 6 },
    { "Cauldron", "witchy / walk around", 74, 8, 17, 1, "decor_cauldron", 12 },
    { "Broom", "witchy / walk over", 38, 4, 15, 0, "decor_broom", 3 },
    { "Potion Rack", "witchy / walk around", 86, 10, 17, 1, "decor_potion_rack", 14 },
    { "Amber Fossil", "prehistoric / walk over", 58, 6, 12, 0, "decor_amber_fossil", 5 },
    { "Cycad", "prehistoric / walk around", 62, 7, 16, 1, "decor_cycad", 0 },
    { "Dinosaur Egg", "prehistoric / walk around", 72, 9, 13, 1, "decor_dinosaur_egg", 9 },
    { "Neon Sign", "cyberpunk / walk around", 80, 9, 14, 1, "decor_neon_sign", 0 },
    { "Holo Terminal", "cyberpunk / walk around", 96, 12, 15, 1, "decor_holo_terminal", 12 },
    { "Cable Coil", "cyberpunk / walk over", 42, 4, 12, 0, "decor_cable_coil", 4 },
    { "Moon Rover", "sci-fi / walk around", 104, 13, 18, 1, "decor_moon_rover", 12 },
    { "Solar Array", "sci-fi / walk around", 88, 10, 18, 1, "decor_solar_array", 10 },
    { "Oxygen Pod", "sci-fi / walk around", 92, 11, 14, 1, "decor_oxygen_pod", 11 },
    { "Wizard Tower", "fantasy / walk around", 112, 14, 17, 1, "decor_wizard_tower", 16 },
    { "Dragon Statue", "fantasy / walk around", 120, 16, 18, 1, "decor_dragon_statue", 16 },
    { "Treasure Chest", "fantasy / walk around", 98, 12, 15, 1, "decor_treasure_chest", 11 },
    { "Sun Obelisk", "lost civilization / walk around", 108, 13, 13, 1, "decor_sun_obelisk", 13 },
    { "Glyph Tablet", "lost civilization / walk over", 64, 7, 14, 0, "decor_glyph_tablet", 4 },
    { "Broken Column", "lost civilization / walk around", 76, 8, 15, 1, "decor_broken_column", 12 },
    { "Stone Lantern", "Japanese zen / walk around", 70, 6, 13, 1, "decor_stone_lantern", 11 },
    { "Bamboo Fountain", "Japanese zen / walk around", 84, 9, 16, 1, "decor_bamboo_fountain", 14 },
    { "Bonsai", "Japanese zen / walk around", 94, 11, 16, 1, "decor_bonsai", 0 },
    { "Fairy Bridge", "woodland fairy / walk around", 102, 12, 20, 1, "decor_fairy_bridge", 16 },
    /* Large climb structures. The radius is the footprint the spider must be
     * inside to be lifted, and the height is the lift at the centre. These are
     * authored at 192x192 and drawn 1:1, so a radius of 60 is a structure that
     * spans a quarter of the tank rather than a prop the spider hops onto. */
    { "Fallen Branch", "spider climbs the whole length", 140, 15, 60, 1, "decor_climb_branch", 46 },
    { "Vine Arch", "spider climbs the whole length", 160, 17, 64, 1, "decor_climb_vine", 54 },
    { "Root Bridge", "spider climbs the whole length", 150, 16, 62, 1, "decor_climb_root", 50 },
    { "Ivy Column", "spider climbs the whole length", 132, 14, 52, 1, "decor_climb_ivy", 58 },
    { "Mossy Boulder", "spider climbs the whole length", 124, 13, 56, 1, "decor_climb_boulder", 40 },
    { "Driftwood Ramp", "spider climbs the whole length", 170, 18, 66, 1, "decor_climb_driftwood", 62 },
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
    if (kind < DECOR_CLIMB_FIRST)
        return (g.catalogue_owned_high & (1u << (kind - 64 + 4))) != 0;
    return (g.climb_owned & (1u << (kind - DECOR_CLIMB_FIRST))) != 0;
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
    else if (kind < DECOR_CLIMB_FIRST) g.catalogue_owned_high |= 1u << (kind - 64 + 4);
    else g.climb_owned |= 1u << (kind - DECOR_CLIMB_FIRST);
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
    /* Cycle through the paths this kind actually supports, so a flat prop never
     * lands on Climb and a climbable one always reaches it. */
    uint8_t path = item->blocking;
    for (int i = 0; i < DECOR_PATH_COUNT; i++) {
        path = (uint8_t)((path + 1) % DECOR_PATH_COUNT);
        if (path != DECOR_CLIMB || decor_climbable(item->kind)) break;
    }
    return decor_set_path(slot, path);
}

bool decor_set_path(int slot, uint8_t path)
{
    decor_item_t *item = slot_at(slot);
    if (!item || item->kind == 0xFF || path >= DECOR_PATH_COUNT) {
        ESP_LOGW(TAG, "path rejected: slot=%d path=%u", slot, path);
        return false;
    }
    /* Climbing needs something to climb. Refuse the path on a flat prop rather
     * than silently drawing the spider at zero lift and confusing the player. */
    if (path == DECOR_CLIMB && !decor_climbable(item->kind)) {
        ESP_LOGW(TAG, "path rejected: slot=%d kind=%u is not climbable", slot, item->kind);
        return false;
    }
    item->blocking = path;
    ESP_LOGI(TAG, "slot %d blocking=%u", slot, item->blocking);
    return true;
}

/* --- climbing (2.5D) --------------------------------------------------- */

bool decor_is_climb_structure(uint8_t kind)
{
    return kind >= DECOR_CLIMB_FIRST && kind < DECOR_COUNT;
}

bool decor_climbable(uint8_t kind)
{
    const decor_def_t *def = decor_def(kind);
    return def && def->climb_height > 0;
}

uint8_t decor_climb_height(int slot)
{
    const decor_item_t *item = decor_at(slot);
    if (!item || item->blocking != DECOR_CLIMB) return 0;
    const decor_def_t *def = decor_def(item->kind);
    return def ? def->climb_height : 0;
}

/* The lift at a point on one item, or 0 when the point is off its surface.
 *
 * A prop is a flat disc: inside the footprint you are on it, outside you are
 * not.
 *
 * A large climb structure is a SLOPE, and the slope runs UP THE SCREEN. The
 * spider enters at the bottom edge of the structure and rises as it walks
 * toward the top edge, which is what makes it read as climbing a branch rather
 * than floating over one. The first version ramped radially from the centre,
 * so the spider rose from whichever side it approached and the effect read as
 * being lifted onto a platform -- the "shadow as they walk over larger things"
 * report. Height is a function of vertical position only.
 *
 * The ramp is measured across the footprint's vertical extent, so the bottom
 * edge is ground level and the top edge is the full climb height. */
static uint8_t climb_lift_on(const decor_item_t *item, const decor_def_t *def,
                             int16_t x, int16_t y)
{
    float dx = (float)(x - item->x);
    float dy = (float)(y - item->y);
    float r = (float)def->radius;
    float d2 = dx * dx + dy * dy;
    if (d2 > r * r) return 0;
    if (!decor_is_climb_structure(item->kind)) return def->climb_height;

    /* 0 at the bottom edge of the footprint, 1 at the top edge. Screen y grows
     * downward, so the bottom edge is item->y + r. */
    float t = (dy + r) / (2.0f * r);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float lift = (float)def->climb_height * (1.0f - t);
    return (uint8_t)(lift + 0.5f);
}

uint8_t decor_climb_at(int16_t x, int16_t y, int *out_slot)
{
    if (out_slot) *out_slot = -1;
    uint8_t best = 0;
    for (int i = 0; i < decor_slot_count(); i++) {
        const decor_item_t *item = decor_at(i);
        if (!item || item->blocking != DECOR_CLIMB) continue;
        const decor_def_t *def = decor_def(item->kind);
        if (!def || def->climb_height == 0) continue;
        uint8_t lift = climb_lift_on(item, def, x, y);
        if (lift > best) {
            best = lift;
            if (out_slot) *out_slot = i;
        }
    }
    return best;
}

/* The surface slope under a point, as a signed -1..1 value.
 *
 * A large climb structure ramps linearly from ground level at its bottom edge
 * to full height at its top edge, so its surface has a constant gradient. The
 * gradient is the climb height spread over the footprint's vertical extent:
 *
 *     rise / run = climb_height / (2 * radius)
 *
 * A 62 px climb over a 66 px radius is therefore a slope of about 0.47, or
 * roughly 25 degrees. That is gentle enough to look like a ramp and steep
 * enough that a level spider would visibly float, which is exactly why the
 * renderer needs it.
 *
 * The sign is expressed in screen coordinates, where y grows downward: a
 * surface that rises toward the top of the screen has a NEGATIVE slope. The
 * renderer negates it again when it tilts the body, so the two conventions are
 * kept explicit rather than silently cancelling. */
float decor_climb_slope(int16_t x, int16_t y, int *out_slot)
{
    if (out_slot) *out_slot = -1;
    float best = 0.0f;
    uint8_t best_lift = 0;
    for (int i = 0; i < decor_slot_count(); i++) {
        const decor_item_t *item = decor_at(i);
        if (!item || item->blocking != DECOR_CLIMB) continue;
        const decor_def_t *def = decor_def(item->kind);
        if (!def || def->climb_height == 0) continue;
        uint8_t lift = climb_lift_on(item, def, x, y);
        if (lift == 0 || lift <= best_lift) continue;
        best_lift = lift;
        if (out_slot) *out_slot = i;
        /* A small prop is a flat disc, so its top is level. */
        if (!decor_is_climb_structure(item->kind)) { best = 0.0f; continue; }
        float r = (float)def->radius;
        if (r <= 0.0f) { best = 0.0f; continue; }
        best = -(float)def->climb_height / (2.0f * r);
    }
    return best;
}

/* --- perching on small props ------------------------------------------- */

/* Minimum perch footprint, in pixels.
 *
 * A prop's `radius` is its collision footprint, which for small items is
 * SMALLER than the spider's own body: the Book Stack is radius 12 while the
 * body spans about 26 px. Using the raw radius meant the spider's centre had to
 * land within 12 px of the item's centre to perch, a window it crossed in a
 * fraction of a tick while wandering, so it appeared to jump straight over the
 * books. The perch test is about "is the spider standing on this thing", not
 * "is its centre inside the collision disc", so it gets a floor that matches
 * the body it is standing on. */
#define PERCH_MIN_RADIUS 20

/* The climbable prop under a point, or NULL. Large structures are excluded:
 * those are walked across, not perched on. */
static const decor_item_t *perch_item_at(int16_t x, int16_t y)
{
    const decor_item_t *best = NULL;
    uint8_t best_lift = 0;
    for (int i = 0; i < decor_slot_count(); i++) {
        const decor_item_t *item = decor_at(i);
        if (!item || item->blocking != DECOR_CLIMB) continue;
        if (decor_is_climb_structure(item->kind)) continue;
        const decor_def_t *def = decor_def(item->kind);
        if (!def || def->climb_height == 0) continue;
        int32_t r = def->radius > PERCH_MIN_RADIUS ? def->radius : PERCH_MIN_RADIUS;
        float dx = (float)(x - item->x);
        float dy = (float)(y - item->y);
        if (dx * dx + dy * dy > (float)(r * r)) continue;
        if (def->climb_height > best_lift) {
            best_lift = def->climb_height;
            best = item;
        }
    }
    return best;
}

bool decor_perch_at(int16_t x, int16_t y)
{
    return perch_item_at(x, y) != NULL;
}

int16_t decor_perch_radius(int16_t x, int16_t y)
{
    const decor_item_t *item = perch_item_at(x, y);
    if (!item) return 0;
    const decor_def_t *def = decor_def(item->kind);
    if (!def) return 0;
    return def->radius > PERCH_MIN_RADIUS ? (int16_t)def->radius
                                          : (int16_t)PERCH_MIN_RADIUS;
}

bool decor_perch_center(int16_t x, int16_t y, int16_t *out_x, int16_t *out_y)
{
    const decor_item_t *item = perch_item_at(x, y);
    if (!item) return false;
    if (out_x) *out_x = item->x;
    if (out_y) *out_y = item->y;
    return true;
}

/* The heading that reads as "sitting on top, looking out".
 *
 * Facing the item's centre would point the spider at its own feet, which is the
 * opposite of the illusion. Facing straight out from the centre is the pose a
 * perched animal actually holds, and it is what makes the raised body read as
 * height rather than as a floating sprite. The heading is quantised to the
 * eight compass directions the artwork is drawn for, so the legs land on a
 * stable fan instead of sweeping through arbitrary angles. */
bool decor_perch_heading(int16_t x, int16_t y, float *out_heading)
{
    const decor_item_t *item = perch_item_at(x, y);
    if (!item) return false;

    float dx = (float)(x - item->x);
    float dy = (float)(y - item->y);
    float d = sqrtf(dx * dx + dy * dy);

    /* Dead centre has no outward direction. Face the viewer's "down the screen"
     * axis, which is the most readable default for a top-down enclosure. */
    float h;
    if (d < 1.5f) {
        h = PI_F * 0.5f;
    } else {
        h = atan2f(dy, dx);
    }

    /* Snap to 45-degree steps. */
    const float step = PI_F / 4.0f;
    h = roundf(h / step) * step;

    if (out_heading) *out_heading = h;
    return true;
}

/* The ramp is deliberately slow enough to read as a climb and fast enough that
 * the spider is never visibly floating. 90 px/s covers a small prop (<=18 px)
 * in a fifth of a second, but a large climb structure is 40-62 px, which would
 * take two thirds of a second and read as a slow float. The rate therefore
 * scales with the target so every climb takes a similar, readable time. */
#define CLIMB_RAMP_PX_PER_S 90.0f
#define CLIMB_RAMP_MIN_PX_PER_S 90.0f
#define CLIMB_RAMP_MAX_PX_PER_S 260.0f

static float s_climb_lift;      /* current drawn lift, pixels */
static uint32_t s_climb_last_ms;
static bool s_climb_init;
/* -1 = normal footprint-driven behaviour; >= 0 pins the lift. Console test hook
 * only, so the shadow can be inspected without the spider wandering off. */
static int32_t s_climb_forced = -1;

void decor_climb_reset(void)
{
    s_climb_lift = 0.0f;
    s_climb_init = false;
    s_climb_forced = -1;
}

void decor_climb_force(int32_t lift)
{
    s_climb_forced = (lift < 0) ? -1 : lift;
    if (s_climb_forced >= 0) {
        s_climb_lift = (float)s_climb_forced;
        s_climb_init = true;
    }
}

int32_t decor_climb_lift(int16_t x, int16_t y, uint32_t now_ms)
{
    if (s_climb_forced >= 0) return s_climb_forced;

    uint8_t target = decor_climb_at(x, y, NULL);

    if (!s_climb_init) {
        s_climb_lift = (float)target;
        s_climb_last_ms = now_ms;
        s_climb_init = true;
        return (int32_t)(s_climb_lift + 0.5f);
    }

    uint32_t elapsed = now_ms - s_climb_last_ms;
    s_climb_last_ms = now_ms;
    /* A long gap means the render tick stalled or the app was suspended; snap
     * rather than animate a huge step. */
    if (elapsed > 250) {
        s_climb_lift = (float)target;
        return (int32_t)(s_climb_lift + 0.5f);
    }

    /* Scale the rate with the distance still to travel so a 60 px structure
     * does not take three times as long as a 20 px prop. */
    float remaining = fabsf((float)target - s_climb_lift);
    float rate = CLIMB_RAMP_PX_PER_S + remaining * 3.0f;
    if (rate < CLIMB_RAMP_MIN_PX_PER_S) rate = CLIMB_RAMP_MIN_PX_PER_S;
    if (rate > CLIMB_RAMP_MAX_PX_PER_S) rate = CLIMB_RAMP_MAX_PX_PER_S;
    float step = rate * (float)elapsed / 1000.0f;
    float delta = (float)target - s_climb_lift;
    if (fabsf(delta) <= step) s_climb_lift = (float)target;
    else s_climb_lift += (delta > 0.0f) ? step : -step;

    return (int32_t)(s_climb_lift + 0.5f);
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

/* --- the gift's layer ---------------------------------------------------
 *
 * The gift is a single item, so it has no siblings to order against. What the
 * player actually wants is to order it against the DECOR sharing its band: a
 * gift behind the log, or in front of it. The gift therefore occupies a
 * position in the same band sequence the decor uses, and the renderer inserts
 * it at that position.
 *
 * The count includes the gift itself, so "3 / 4" means the gift is third of
 * four things in that band. */
int decor_gift_layer_count(uint8_t path)
{
    return decor_layer_count(path, -1) + 1;
}

int decor_gift_layer_index(uint8_t path)
{
    int count = decor_layer_count(path, -1);
    int index = g.gift_layer;
    if (index > count) index = count;
    return index;
}

bool decor_gift_set_layer(uint8_t path, int index)
{
    int count = decor_layer_count(path, -1);
    if (index < 0 || index > count) {
        ESP_LOGW(TAG, "gift layer rejected: index=%d count=%d", index, count);
        return false;
    }
    g.gift_layer = (uint8_t)index;
    return true;
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
    uint8_t climb_owned = g.climb_owned;
    decor_item_t saved[DECOR_SLOTS];
    memcpy(saved, g.decor, sizeof saved);
    bool ok = true;

    decor_clear();
    g.decor_owned = 0;
    g.decor_owned_extra = 0;
    g.catalogue_owned_high &= 0x0F;
    g.climb_owned = 0;
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
    /* Path 3 is Climb. A Small Rock has height, so it is allowed; an
     * out-of-range path is not. */
    ok = ok && decor_set_path(a, DECOR_CLIMB) && decor_at(a)->blocking == DECOR_CLIMB;
    ok = ok && !decor_set_path(a, DECOR_PATH_COUNT);
    ok = ok && decor_set_path(a, DECOR_WALK_OVER);
    ok = ok && decor_set_layer(a, -1) < 0;
    ok = ok && decor_set_layer(a, 2) < 0 && decor_layer_index(a) == 0;
    ok = ok && decor_set_path(b, DECOR_SOLID) && decor_blocks_point(200, 200);
    ok = ok && decor_set_layer(b, 2) >= 0;

    /* --- climbing (2.5D) ------------------------------------------------
     * A climbable item lifts the spider only while it is over the footprint,
     * and only on the Climb path. A flat prop must refuse the path outright. */
    decor_clear();
    int log = decor_place(DECOR_LOG, 240, 240);
    ok = ok && log >= 0 && decor_climbable(DECOR_LOG);
    ok = ok && !decor_climbable(DECOR_FERN);
    ok = ok && decor_climb_height(log) == 0;   /* still Walk Over */
    ok = ok && decor_set_path(log, DECOR_CLIMB);
    ok = ok && decor_climb_height(log) == k_decor_kinds[DECOR_LOG].climb_height;
    int climb_slot = -1;
    ok = ok && decor_climb_at(240, 240, &climb_slot) == k_decor_kinds[DECOR_LOG].climb_height;
    ok = ok && climb_slot == log;
    ok = ok && decor_climb_at(240, 240 + k_decor_kinds[DECOR_LOG].radius + 6, NULL) == 0;
    /* The tallest overlapping item wins. */
    int bridge = decor_place(DECOR_STONE_BRIDGE, 240, 240);
    ok = ok && bridge >= 0 && decor_set_path(bridge, DECOR_CLIMB);
    ok = ok && decor_climb_at(240, 240, NULL) == k_decor_kinds[DECOR_STONE_BRIDGE].climb_height;
    /* A flat prop refuses Climb rather than silently drawing zero lift. */
    int fern = decor_place(DECOR_FERN, 120, 120);
    ok = ok && fern >= 0 && !decor_set_path(fern, DECOR_CLIMB);
    ok = ok && decor_at(fern)->blocking != DECOR_CLIMB;
    /* Cycling skips Climb for a flat prop and reaches it for a climbable one. */
    for (int i = 0; i < DECOR_PATH_COUNT; i++) decor_toggle_blocking(fern);
    ok = ok && decor_at(fern)->blocking != DECOR_CLIMB;
    bool reached = false;
    for (int i = 0; i < DECOR_PATH_COUNT; i++) {
        decor_toggle_blocking(log);
        if (decor_at(log)->blocking == DECOR_CLIMB) reached = true;
    }
    ok = ok && reached;
    /* The ramp settles on the target and resets cleanly. */
    decor_climb_reset();
    ok = ok && decor_climb_lift(240, 240, 1000) == k_decor_kinds[DECOR_STONE_BRIDGE].climb_height;
    ok = ok && decor_climb_lift(240, 240, 2000) == k_decor_kinds[DECOR_STONE_BRIDGE].climb_height;
    decor_climb_reset();
    ok = ok && decor_climb_lift(20, 20, 3000) == 0;
    decor_clear();

    /* --- large climb structures -----------------------------------------
     * These are a separate family: authored at 192x192, drawn 1:1, and their
     * lift RAMPS from nothing at the rim to full height at the centre. A prop
     * is a flat disc, so it must still snap to its full height. */
    ok = ok && decor_is_climb_structure(DECOR_CLIMB_BRANCH);
    ok = ok && decor_is_climb_structure(DECOR_CLIMB_DRIFTWOOD);
    ok = ok && !decor_is_climb_structure(DECOR_LOG);
    ok = ok && !decor_is_climb_structure(DECOR_FAIRY_BRIDGE);
    int big = decor_place(DECOR_CLIMB_DRIFTWOOD, 240, 240);
    ok = ok && big >= 0 && decor_set_path(big, DECOR_CLIMB);
    uint8_t full = k_decor_kinds[DECOR_CLIMB_DRIFTWOOD].climb_height;
    uint8_t radius = k_decor_kinds[DECOR_CLIMB_DRIFTWOOD].radius;
    ok = ok && full >= 40;                     /* genuinely tall, not a hop */
    ok = ok && decor_climb_at(240, 240, NULL) == full;
    /* Halfway out the lift is roughly half, and it falls to zero at the rim. */
    uint8_t half = decor_climb_at(240 + radius / 2, 240, NULL);
    ok = ok && half > 0 && half < full;
    ok = ok && decor_climb_at(240 + radius + 2, 240, NULL) == 0;
    /* A prop still snaps: no gradient across its footprint. */
    int flat = decor_place(DECOR_LOG, 120, 120);
    ok = ok && flat >= 0 && decor_set_path(flat, DECOR_CLIMB);
    uint8_t log_h = k_decor_kinds[DECOR_LOG].climb_height;
    ok = ok && decor_climb_at(120, 120, NULL) == log_h;
    ok = ok && decor_climb_at(120 + k_decor_kinds[DECOR_LOG].radius - 1, 120, NULL) == log_h;
    decor_clear();

    g.level = 40;
    g.silk = 100000;
    for (int i = 32; i < DECOR_COUNT; i++)
        ok = decor_buy(i) && decor_owned(i) && ok;
    g.catalogue_owned_high = high_owned;
    g.climb_owned = climb_owned;
    g.silk = silk; g.decor_owned = owned; g.level = level;
    g.decor_owned_extra = owned_extra; g.level = level;
    memcpy(g.decor, saved, sizeof saved);
    decor_restore_extra(extra);
    g.decor_extra_count = extra_count;
    return ok;
}
