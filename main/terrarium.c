#include "terrarium.h"

#include "assets.h"
#include "decor.h"
#include "game_config.h"
#include "species.h"
#include "sprites.h"
#include "theme.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <stdio.h>

/* Backgrounds are authored at the panel's native 240x240 and enlarged 2x into
 * the 480x480 canvas, so each source pixel covers a 2x2 block. */
#define PIXEL_GRID 2

static lv_obj_t *s_background, *s_decor_layer, *s_canopy_layer, *s_poop_layer,
                *s_preview_ring, *s_preview_image;
/* The gift is a single sprite, so it is parented DIRECTLY into the decor band it
 * belongs to instead of into a container of its own.
 *
 * A separate gift layer made the layer control meaningless: lv_obj_move_to_index()
 * only reorders siblings, so a gift in its own layer could never be interleaved
 * with the decor it was supposed to sit behind or in front of. The index was
 * computed and then silently ignored, which is exactly the "layer settings arent
 * working on either item" report. */
static lv_obj_t *s_gift_obj;
static bool decor_canopy_now(const decor_item_t *item);
static int s_preview_slot = -1;
static bool s_preview_gift;
static int32_t s_preview_y;
static uint16_t *s_pixels;
static uint8_t *s_gift_pixels;
/* Grow only on explicit placement/restore; rendering reuses the high-water pool. */
static lv_obj_t **s_decor_obj;
static int s_decor_capacity;
static uint32_t s_poop_at[MAX_POOP];
/* Dirt points each dropping is worth, so Clean can pay for the mess it actually
 * removed instead of for the tap. */
static uint8_t s_poop_dirt[MAX_POOP];

static const lv_image_dsc_t *background_asset(int scene)
{
    const lv_image_dsc_t *image = assets_background(sprite_background_index(scene));
    if (!image || image->header.cf != LV_COLOR_FORMAT_RGB565 ||
        image->header.w != 240 || image->header.h != 240 ||
        image->data_size != 240 * 240 * 2) {
        ESP_LOGE("spidarium.tank", "background %d missing or incompatible", scene);
        return NULL;
    }
    return image;
}

void terrarium_rebuild(void)
{
    if (!s_pixels) return;
    int scene = (int)(theme_current() - k_themes);
    const lv_image_dsc_t *image = background_asset(scene);
    ESP_ERROR_CHECK(image ? ESP_OK : ESP_ERR_INVALID_STATE);
    const uint16_t *source = (const uint16_t *)image->data;
    for (int y = 0; y < ui.H; y++)
        for (int x = 0; x < ui.W; x++)
            s_pixels[y * ui.W + x] = source[(y / 2) * 240 + x / 2];
    if (g.border_id != 255) {
        const lv_image_dsc_t *border = assets_border(sprite_border_index(g.border_id));
        ESP_ERROR_CHECK(border ? ESP_OK : ESP_ERR_INVALID_STATE);
        const uint16_t *colors = (const uint16_t *)border->data;
        const uint8_t *alpha = border->data + 240 * 240 * 2;
        for (int y = 0; y < ui.H; y++)
            for (int x = 0; x < ui.W; x++) {
                int src = y / 2 * 240 + x / 2;
                if (alpha[src]) s_pixels[y * ui.W + x] = colors[src];
            }
    }
    lv_obj_invalidate(s_background);
}

bool terrarium_test(void)
{
    uint32_t hashes[THEME_COUNT];
    bool ok = s_pixels != NULL;
    for (int scene = 0; scene < THEME_COUNT; scene++) {
        const lv_image_dsc_t *image = background_asset(scene);
        if (!image) return false;
        uint32_t hash = 2166136261u;
        for (int i = 0; i < 240 * 240 * 2; i++)
            hash = (hash ^ image->data[i]) * 16777619u;
        hashes[scene] = hash;
        for (int i = 0; i < scene; i++) ok = ok && hashes[i] != hash;
    }
    const lv_image_dsc_t *image = background_asset((int)(theme_current() - k_themes));
    if (!image) return false;
    const uint16_t *source = (const uint16_t *)image->data;
    for (int y = 0; ok && y < ui.H; y++)
        for (int x = 0; x < ui.W; x++) {
            if (g.border_id != 255 && (x < 32 || x >= 448 || y < 32 || y >= 448)) continue;
            if (s_pixels[y * ui.W + x] !=
                s_pixels[(y / PIXEL_GRID * PIXEL_GRID) * ui.W + x / PIXEL_GRID * PIXEL_GRID] ||
                s_pixels[y * ui.W + x] != source[(y / 2) * 240 + x / 2])
                ok = false;
        }
    for (int id = 0; id < THEME_COUNT; id++) {
        const lv_image_dsc_t *frame = assets_border(sprite_border_index(id));
        if (!frame) return false;
        const uint8_t *alpha = frame->data + 240 * 240 * 2;
        for (int y = 16; y < 224; y++)
            for (int x = 16; x < 224; x++)
                ok = ok && alpha[y * 240 + x] == 0;
        if (g.border_id == id) {
            const uint16_t *colors = (const uint16_t *)frame->data;
            for (int y = 0; y < ui.H; y++)
                for (int x = 0; x < ui.W; x++) {
                    int src = y / 2 * 240 + x / 2;
                    if (alpha[src]) ok = ok && s_pixels[y * ui.W + x] == colors[src];
                }
        }
    }
    if (g.gift.used) {
        const lv_image_dsc_t *gift = assets_image(sprite_gift_index(g.gift.kind));
        ok = ok && gift && s_gift_pixels && s_gift_obj &&
             !lv_obj_is_hidden(s_gift_obj);
        if (gift && s_gift_pixels) {
            const uint16_t *colors = (const uint16_t *)gift->data;
            const uint8_t *alpha = gift->data + 2048;
            for (int y = 0; y < 64; y++)
                for (int x = 0; x < 64; x++) {
                    int src = y / 2 * 32 + x / 2, dst = y * 64 + x;
                    ok = ok && ((uint16_t *)s_gift_pixels)[dst] == colors[src] &&
                         s_gift_pixels[8192 + dst] == alpha[src];
                }
        }
    }
    return terrarium_decor_test() && ok;
}

bool terrarium_decor_test(void)
{
    bool ok = true;
    /* Every placed item must have a visible image object at its position, and
     * every empty slot must be hidden. lv_obj_get_x() reads the cached coords,
     * which are only refreshed by a layout pass, so force one first. */
    lv_obj_update_layout(s_decor_layer);
    lv_obj_update_layout(s_canopy_layer);
    for (int i = 0; i < s_decor_capacity; i++) {
        const decor_item_t *item = decor_at(i);
        lv_obj_t *obj = s_decor_obj[i];
        if (!obj) { ok = false; continue; }
        if (!item) {
            ok = ok && lv_obj_is_hidden(obj);
            continue;
        }
        const decor_def_t *def = decor_def(item->kind);
        bool visible = def && !lv_obj_is_hidden(obj);
        bool placed = lv_obj_get_x(obj) == item->x - 32 &&
                      lv_obj_get_y(obj) == item->y - 32;
        bool sourced = lv_image_get_src(obj) ==
                       assets_image(sprite_decor_item_index(item->kind));
        if (!visible || !placed || !sourced)
            ESP_LOGW("spidarium.tank",
                     "decor slot %d kind=%u visible=%d placed=%d(%d,%d vs %d,%d) sourced=%d",
                     i, item->kind, visible, placed, (int)lv_obj_get_x(obj),
                     (int)lv_obj_get_y(obj), item->x - 32, item->y - 32, sourced);
        bool canopy = decor_canopy_now(item);
        bool ordered = lv_obj_get_parent(obj) == (canopy ? s_canopy_layer : s_decor_layer);
        for (int j = i - 1; j >= 0; j--) {
            const decor_item_t *other = decor_at(j);
            if (!other || decor_canopy_now(other) != canopy) continue;
            ordered = ordered && lv_obj_get_index(s_decor_obj[j]) < lv_obj_get_index(obj);
            break;
        }
        ok = ok && visible && placed && sourced && ordered;
    }
    return ok;
}

static lv_obj_t *layer(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, ui.W, ui.H);
    lv_obj_set_clickable(obj, false);
    lv_obj_set_scrollable(obj, false);
    return obj;
}

static lv_obj_t *dot(lv_obj_t *parent, int x, int y, int radius, uint32_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, radius * 2, radius * 2);
    lv_obj_set_pos(obj, x - radius, y - radius);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 2, 0);
    lv_obj_set_clickable(obj, false);
    lv_obj_set_scrollable(obj, false);
    return obj;
}

/* The gift honours the same Walk Behind rule as decor: it may only cover the
 * spider with its top half, so it moves to the canopy band while the spider is
 * above its centre line and drops back to the floor band once the spider is
 * level with or below the base.
 *
 * Walk Behind also BLOCKS movement (see gift_blocks_movement() in behaviour.c),
 * so the spider can never stand on the gift's lower half. That is what makes
 * this rule read correctly: the only way to be above the centre line is to be
 * genuinely behind the artwork, never on top of it. */
static bool gift_canopy_now(void)
{
    return g.gift_path == DECOR_WALK_BEHIND && g.spider_y < g.gift.y;
}

static lv_obj_t *gift_band(void)
{
    return gift_canopy_now() ? s_canopy_layer : s_decor_layer;
}

void terrarium_refresh_gift(void)
{
    if (!s_decor_layer) return;
    if (s_gift_obj) {
        lv_obj_delete(s_gift_obj);
        s_gift_obj = NULL;
    }
    if (!g.gift.used) return;
    int index = sprite_gift_index(g.gift.kind);
    const lv_image_dsc_t *image = index >= 0 ? assets_image(index) : NULL;
    if (!image) {
        ESP_LOGW("spidarium.tank", "gift image unavailable");
        dot(gift_band(), g.gift.x, g.gift.y, 10, gift_def(g.gift.kind)->colour);
        return;
    }
    ESP_ERROR_CHECK(image->header.w == 32 && image->header.h == 32 &&
                    image->header.cf == LV_COLOR_FORMAT_RGB565A8 ? ESP_OK : ESP_FAIL);
    const uint16_t *color = (const uint16_t *)image->data;
    const uint8_t *alpha = image->data + 32 * 32 * 2;
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            int src = y / 2 * 32 + x / 2, dst = y * 64 + x;
            ((uint16_t *)s_gift_pixels)[dst] = color[src];
            s_gift_pixels[64 * 64 * 2 + dst] = alpha[src];
        }
    }
    s_gift_obj = lv_canvas_create(gift_band());
    lv_canvas_set_buffer(s_gift_obj, s_gift_pixels, 64, 64, LV_COLOR_FORMAT_RGB565A8);
    lv_obj_set_pos(s_gift_obj, g.gift.x - 32, g.gift.y - 32);
    lv_obj_set_clickable(s_gift_obj, false);
    lv_obj_set_scrollable(s_gift_obj, false);
    terrarium_gift_depth();
}

/* Report the gift's rendered state. The duplicate-gift bug was a preview image
 * left visible while the gift was hidden, so the useful check is how many
 * gift-like objects are actually on screen: exactly one gift object must exist
 * when a gift is placed, and the preview image must be hidden. */
void terrarium_gift_report(void)
{
    bool preview_hidden = s_preview_image ? lv_obj_is_hidden(s_preview_image) : true;
    bool gift_hidden = s_gift_obj ? lv_obj_is_hidden(s_gift_obj) : true;
    const char *band = "none";
    int gift_index = -1, decor_index = -1;
    if (s_gift_obj) {
        band = lv_obj_get_parent(s_gift_obj) == s_canopy_layer ? "canopy" : "floor";
        gift_index = (int)lv_obj_get_index(s_gift_obj);
    }
    /* The index of the first decor item in the same band, so the two numbers can
     * be compared directly: the gift is behind that item when its index is
     * lower. This is the check that the layer control actually reorders. */
    bool canopy = gift_canopy_now();
    for (int i = 0; i < s_decor_capacity; i++) {
        const decor_item_t *item = decor_at(i);
        if (!item || !s_decor_obj[i]) continue;
        if (decor_canopy_now(item) != canopy) continue;
        decor_index = (int)lv_obj_get_index(s_decor_obj[i]);
        break;
    }
    printf("GIFTREPORT used=%u kind=%u obj=%d gift_hidden=%d preview_hidden=%d "
           "band=%s layer=%d count=%d gift_index=%d decor_index=%d "
           "gift=(%d,%d) spider_y=%d canopy=%d\n",
           g.gift.used, g.gift.kind, s_gift_obj ? 1 : 0,
           gift_hidden ? 1 : 0, preview_hidden ? 1 : 0, band,
           decor_gift_layer_index(g.gift_path),
           decor_gift_layer_count(g.gift_path), gift_index, decor_index,
           g.gift.x, g.gift.y, g.spider_y, gift_canopy_now() ? 1 : 0);
}

void terrarium_gift_depth(void)
{
    if (!s_gift_obj || !s_canopy_layer || !g.gift.used) return;

    /* The gift is parented into the decor band itself, so it is a SIBLING of the
     * decor it must interleave with. That is the whole fix: a gift in its own
     * container could never be ordered against decor, because
     * lv_obj_move_to_index() only reorders siblings. */
    lv_obj_t *want = gift_band();
    if (lv_obj_get_parent(s_gift_obj) != want) lv_obj_set_parent(s_gift_obj, want);

    /* "Layer" means "how far forward among the decor in this band": 0 is behind
     * every item, count is in front of every item. Walk the band in slot order,
     * which is also its z-order, and drop the gift immediately below the item at
     * the chosen position.
     *
     * The target index must account for where the gift already is. Moving the
     * gift shifts the decor's index, so naively moving to the decor's current
     * index oscillates: the gift lands after the item, the item's index drops,
     * and the next tick moves the gift back. Comparing the two indices first
     * makes the result a fixed point. */
    bool canopy = gift_canopy_now();
    int index = decor_gift_layer_index(g.gift_path);
    int seen = 0;
    for (int i = 0; i < s_decor_capacity; i++) {
        const decor_item_t *item = decor_at(i);
        lv_obj_t *obj = s_decor_obj[i];
        if (!item || !obj) continue;
        if (decor_canopy_now(item) != canopy) continue;
        if (seen++ == index) {
            int gift_at = (int)lv_obj_get_index(s_gift_obj);
            int decor_at_index = (int)lv_obj_get_index(obj);
            int target = gift_at < decor_at_index ? decor_at_index - 1 : decor_at_index;
            if (gift_at != target) lv_obj_move_to_index(s_gift_obj, target);
            return;
        }
    }
    lv_obj_move_foreground(s_gift_obj);
}

void terrarium_refresh_decor(void)
{
    if (!s_decor_layer) return;
    int count = decor_slot_count();
    if (count > s_decor_capacity) {
        lv_obj_t **grown = heap_caps_realloc(s_decor_obj, count * sizeof *grown,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!grown) { ESP_LOGE("spidarium.tank", "decor renderer: PSRAM exhausted"); return; }
        s_decor_obj = grown;
        while (s_decor_capacity < count) {
            lv_obj_t *obj = lv_image_create(s_decor_layer);
            lv_obj_set_clickable(obj, false);
            lv_obj_set_scrollable(obj, false);
            s_decor_obj[s_decor_capacity++] = obj;
        }
    }
    for (int i = 0; i < s_decor_capacity; i++) {
        const decor_item_t *item = decor_at(i);
        lv_obj_t *obj = s_decor_obj[i];
        if (!obj) continue;
        if (!item) {
            lv_obj_set_hidden(obj, true);
            continue;
        }
        int index = sprite_decor_item_index(item->kind);
        const lv_image_dsc_t *image = index >= 0 ? assets_image(index) : NULL;
        if (!image) {
            ESP_LOGW("spidarium.tank", "decor %u image unavailable", item->kind);
            lv_obj_set_hidden(obj, true);
            continue;
        }
        const decor_def_t *def = decor_def(item->kind);
        /* The source is 64x64 and LVGL's 1:1 scale is 256, so radius*16 draws
         * a footprint-sized prop: a 9 px pebble cluster lands at 36 px, a 22 px
         * log at 88 px. Scaling keeps the centre, so the position is the
         * unscaled top-left.
         *
         * The large climb structures are authored at 96x96 and drawn at scale
         * 512, which is 2x. That is the same 2 screen pixels per source pixel
         * the gifts use, so all the art shares one pixel grid. Drawing them
         * 1:1 from a 192x192 source gave them half the density of everything
         * else, which made their pixels look visibly smaller. */
        bool big = decor_is_climb_structure(item->kind);
        int32_t scale = big ? 512 : (def ? (int32_t)def->radius * 16 : 256);
        if (scale < 64) scale = 64;
        int32_t half = big ? DECOR_CLIMB_SPRITE / 2 : 32;
        lv_image_set_src(obj, image);
        lv_image_set_scale(obj, scale);
        lv_obj_set_pos(obj, item->x - half, item->y - half);
        lv_obj_set_hidden(obj, false);
        lv_obj_t *parent = decor_canopy_now(item) ? s_canopy_layer : s_decor_layer;
        if (lv_obj_get_parent(obj) != parent) lv_obj_set_parent(obj, parent);
        lv_obj_move_foreground(obj);
    }
}

void terrarium_decor_report(int slot)
{
    if (slot < 0 || slot >= s_decor_capacity) {
        printf("DECORREPORT slot=%d out of range (capacity=%d)\n", slot, s_decor_capacity);
        return;
    }
    const decor_item_t *item = decor_at(slot);
    lv_obj_t *obj = s_decor_obj[slot];
    if (!item || !obj) {
        printf("DECORREPORT slot=%d empty\n", slot);
        return;
    }
    int index = sprite_decor_item_index(item->kind);
    const lv_image_dsc_t *image = index >= 0 ? assets_image(index) : NULL;
    printf("DECORREPORT slot=%d kind=%u big=%d index=%d image=%p w=%u h=%u "
           "scale=%d pos=(%d,%d) hidden=%d parent=%s\n",
           slot, item->kind, decor_is_climb_structure(item->kind), index,
           (const void *)image, image ? (unsigned)image->header.w : 0u,
           image ? (unsigned)image->header.h : 0u,
           (int)lv_image_get_scale(obj), (int)lv_obj_get_x(obj), (int)lv_obj_get_y(obj),
           (int)lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN),
           lv_obj_get_parent(obj) == s_canopy_layer ? "canopy" : "floor");
}

/* A walk-behind item may only cover the spider with its TOP half. The sprite is * centred on the item, so its centre line is the halfway point: while the
 * spider is above that line it is genuinely behind the item, and once it is
 * level with or below it the spider is standing in front of the base and the
 * item must drop behind it. Parenting the whole sprite to the canopy layer made
 * the spider vanish over the item's footprint, which broke the perspective. */
static bool decor_canopy_now(const decor_item_t *item)
{
    return item->blocking == DECOR_WALK_BEHIND && g.spider_y < item->y;
}

void terrarium_decor_depth(void)
{
    if (!s_canopy_layer) return;
    for (int i = 0; i < s_decor_capacity; i++) {
        const decor_item_t *item = decor_at(i);
        lv_obj_t *obj = s_decor_obj[i];
        if (!item || !obj || lv_obj_is_hidden(obj)) continue;
        if (item->blocking != DECOR_WALK_BEHIND) continue;
        lv_obj_t *want = decor_canopy_now(item) ? s_canopy_layer : s_decor_layer;
        if (lv_obj_get_parent(obj) == want) continue;
        lv_obj_set_parent(obj, want);
        lv_obj_move_foreground(obj);
    }
}

void terrarium_raise_canopy(void)
{
    lv_obj_move_foreground(s_canopy_layer);
    lv_obj_move_foreground(s_preview_ring);
}

bool terrarium_preview_test(uint8_t path, int layer_index)
{
    /* The preview follows the same rule as a placed item: a walk-behind item is
     * only in the canopy layer while the spider is above its centre line. */
    bool canopy = path == DECOR_WALK_BEHIND && g.spider_y < s_preview_y;
    lv_obj_t *parent = canopy ? s_canopy_layer : s_decor_layer;
    if (lv_obj_is_hidden(s_preview_image) || lv_obj_get_parent(s_preview_image) != parent)
        return false;
    int before = 0;
    for (int i = 0; i < s_decor_capacity; i++) {
        const decor_item_t *item = decor_at(i);
        if (!item || i == s_preview_slot || decor_canopy_now(item) != canopy) continue;
        if (lv_obj_get_index(s_decor_obj[i]) < lv_obj_get_index(s_preview_image)) before++;
    }
    return before == layer_index &&
        lv_obj_get_index(s_canopy_layer) > lv_obj_get_index(ui.creature_layer);
}

void terrarium_preview_show(int32_t x, int32_t y, int32_t radius)
{
    int r = radius > 0 ? radius : 12;
    lv_obj_set_size(s_preview_ring, r * 2, r * 2);
    lv_obj_set_pos(s_preview_ring, x - r, y - r);
    lv_obj_set_hidden(s_preview_ring, false);
}

void terrarium_preview_item(bool gift, uint8_t kind, int slot, int32_t x, int32_t y,
                            uint8_t path, int layer_index)
{
    int index = gift ? sprite_gift_index(kind) : sprite_decor_item_index(kind);
    const lv_image_dsc_t *image = index >= 0 ? assets_image(index) : NULL;
    if (!image) {
        ESP_LOGE("spidarium.tank", "placement preview unavailable: gift=%d kind=%u", gift, kind);
        return;
    }
    s_preview_slot = slot;
    s_preview_gift = gift;
    s_preview_y = y;
    if (gift) {
        if (s_gift_obj) lv_obj_set_hidden(s_gift_obj, true);
    } else if (slot >= 0 && slot < s_decor_capacity) {
        lv_obj_set_hidden(s_decor_obj[slot], true);
    }
    lv_image_set_src(s_preview_image, image);
    bool big = !gift && decor_is_climb_structure(kind);
    lv_image_set_scale(s_preview_image,
                       gift ? 512 : big ? 512 : decor_def(kind)->radius * 16);
    lv_image_set_antialias(s_preview_image, false);
    lv_obj_set_pos(s_preview_image, x - image->header.w / 2, y - image->header.h / 2);
    lv_obj_set_hidden(s_preview_image, false);
    bool canopy = path == DECOR_WALK_BEHIND && g.spider_y < y;
    lv_obj_t *parent = canopy ? s_canopy_layer : s_decor_layer;
    if (lv_obj_get_parent(s_preview_image) != parent) lv_obj_set_parent(s_preview_image, parent);
    {
        int index = 0;
        for (int i = 0; i < s_decor_capacity; i++) {
            const decor_item_t *item = decor_at(i);
            if (!item || i == slot || decor_canopy_now(item) != canopy) continue;
            if (index++ == layer_index) lv_obj_move_foreground(s_preview_image);
            lv_obj_move_foreground(s_decor_obj[i]);
        }
        if (layer_index >= index) lv_obj_move_foreground(s_preview_image);
    }
    terrarium_preview_show(x, y, gift ? 34 : big ? decor_def(kind)->radius
                           : decor_def(kind)->radius * 2 + 2);
}

void terrarium_preview_hide(void)
{
    lv_obj_set_hidden(s_preview_ring, true);
    lv_obj_set_hidden(s_preview_image, true);
    /* Unconditionally restore the gift. This used to be guarded by
     * `s_preview_gift`, which left the gift hidden whenever the flag had
     * already been cleared -- and a hidden gift plus a still-visible preview
     * image is exactly the "two gifts, one stuck" report. The preview image is
     * a SIBLING of the gift, not a child, so hiding the preview and showing the
     * gift must always happen together. */
    if (s_gift_obj) lv_obj_set_hidden(s_gift_obj, false);
    if (s_preview_slot >= 0 && s_preview_slot < s_decor_capacity)
        lv_obj_set_hidden(s_decor_obj[s_preview_slot], decor_at(s_preview_slot) == NULL);
    s_preview_gift = false;
    s_preview_slot = -1;
    terrarium_refresh_decor();
}

/* A dropping is a small three-lobe mound, not a flat square: a wide base, a
 * narrower middle and a rounded tip, in warm brown with a darker underside.
 * Built from four reused child objects so nothing is allocated per frame. */
static lv_obj_t *poop_blob(lv_obj_t *parent, int x, int y, int w, int h, int radius,
                           uint32_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_pos(obj, x - w / 2, y - h / 2);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_clickable(obj, false);
    lv_obj_set_scrollable(obj, false);
    return obj;
}

void terrarium_add_poop(int32_t x, int32_t y)
{
    int slot = -1;
    for (int i = 0; i < MAX_POOP; i++) if (!ui.poop[i]) { slot = i; break; }
    if (slot < 0) {
        lv_obj_delete(ui.poop[0]);
        for (int i = 1; i < MAX_POOP; i++) {
            ui.poop[i - 1] = ui.poop[i];
            s_poop_at[i - 1] = s_poop_at[i];
            s_poop_dirt[i - 1] = s_poop_dirt[i];
        }
        slot = MAX_POOP - 1;
    }
    /* The slot handle is the base lobe; the other lobes are its children, so
     * deleting the slot removes the whole dropping. */
    lv_obj_t *base = poop_blob(s_poop_layer, x, y + 2, 12, 7, 3, 0x4a3520);
    poop_blob(base, 6, 3, 9, 6, 3, 0x6b4a2a);
    poop_blob(base, 6, 1, 6, 5, 3, 0x7d5a33);
    poop_blob(base, 6, 0, 3, 3, 2, 0x8f6a3d);
    ui.poop[slot] = base;
    s_poop_at[slot] = lv_tick_get();
    s_poop_dirt[slot] = POOP_DIRT_SPIDER;
}

/* A bug left in the tank leaves a small pale speckle rather than a dropping:
 * it is frass, not a meal's worth of waste, so it is smaller, cooler in tone
 * and worth less dirt. Same slot pool and lifetime as a dropping. */
void terrarium_add_bug_poop(int32_t x, int32_t y)
{
    int slot = -1;
    for (int i = 0; i < MAX_POOP; i++) if (!ui.poop[i]) { slot = i; break; }
    if (slot < 0) {
        lv_obj_delete(ui.poop[0]);
        for (int i = 1; i < MAX_POOP; i++) {
            ui.poop[i - 1] = ui.poop[i];
            s_poop_at[i - 1] = s_poop_at[i];
            s_poop_dirt[i - 1] = s_poop_dirt[i];
        }
        slot = MAX_POOP - 1;
    }
    lv_obj_t *base = poop_blob(s_poop_layer, x, y + 1, 7, 4, 2, 0x8d8a7a);
    poop_blob(base, 3, 1, 5, 3, 2, 0xb5b2a2);
    poop_blob(base, 3, 0, 3, 2, 1, 0xd8d5c6);
    ui.poop[slot] = base;
    s_poop_at[slot] = lv_tick_get();
    s_poop_dirt[slot] = POOP_DIRT_BUG;
}

void terrarium_clear_poop(void)
{
    for (int i = 0; i < MAX_POOP; i++) {
        if (ui.poop[i]) lv_obj_delete(ui.poop[i]);
        ui.poop[i] = NULL;
        s_poop_at[i] = 0;
        s_poop_dirt[i] = 0;
    }
}

int terrarium_poop_count(void)
{
    int n = 0;
    for (int i = 0; i < MAX_POOP; i++) if (ui.poop[i]) n++;
    return n;
}

int terrarium_dirt(void)
{
    int dirt = 0;
    for (int i = 0; i < MAX_POOP; i++) if (ui.poop[i]) dirt += s_poop_dirt[i];
    return dirt;
}

void terrarium_tick(uint32_t now_ms)
{
    for (int i = 0; i < MAX_POOP; i++) {
        /* Signed difference: a dropping stamped later than now_ms (created
         * earlier in this same tick) must not read as long expired. */
        if (ui.poop[i] && (int32_t)(now_ms - s_poop_at[i]) >= (int32_t)POOP_LIFETIME_MS) {
            lv_obj_delete(ui.poop[i]);
            ui.poop[i] = NULL;
            s_poop_at[i] = 0;
            s_poop_dirt[i] = 0;
        }
    }
}

void terrarium_init(lv_obj_t *parent)
{
    s_pixels = heap_caps_malloc((size_t)ui.W * ui.H * sizeof(uint16_t),
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(s_pixels ? ESP_OK : ESP_ERR_NO_MEM);
    s_background = lv_canvas_create(parent);
    lv_canvas_set_buffer(s_background, s_pixels, ui.W, ui.H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_clickable(s_background, false);
    lv_obj_set_scrollable(s_background, false);
    ui.bg_canvas = s_background;
    terrarium_rebuild();
    /* Decor is part of the floor, so its layer is created before the droppings
     * and therefore draws beneath them. The gift is parented into the decor
     * band itself (see s_gift_obj), so it needs no layer of its own. */
    s_decor_layer = layer(parent);
    s_canopy_layer = layer(parent);
    terrarium_refresh_decor();
    s_gift_pixels = heap_caps_malloc(64 * 64 * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(s_gift_pixels ? ESP_OK : ESP_ERR_NO_MEM);
    s_poop_layer = layer(parent);
    s_preview_ring = layer(parent);
    lv_obj_set_style_border_width(s_preview_ring, 2, 0);
    lv_obj_set_style_border_color(s_preview_ring, lv_color_hex(0xf5c86b), 0);
    lv_obj_set_hidden(s_preview_ring, true);
    s_preview_image = lv_image_create(parent);
    lv_obj_set_clickable(s_preview_image, false);
    lv_obj_set_scrollable(s_preview_image, false);
    lv_obj_set_hidden(s_preview_image, true);
    terrarium_refresh_decor();
    terrarium_refresh_gift();
}
