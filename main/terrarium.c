#include "terrarium.h"

#include "assets.h"
#include "decor.h"
#include "game_config.h"
#include "species.h"
#include "sprites.h"
#include "theme.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

/* Backgrounds are authored at the panel's native 240x240 and enlarged 2x into
 * the 480x480 canvas, so each source pixel covers a 2x2 block. */
#define PIXEL_GRID 2

static lv_obj_t *s_background, *s_decor_layer, *s_canopy_layer, *s_gift_layer, *s_poop_layer,
                *s_preview_ring, *s_preview_image;
static int s_preview_slot = -1;
static bool s_preview_gift;
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
        ok = ok && gift && s_gift_pixels && lv_obj_get_child_count(s_gift_layer) == 1;
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
        bool canopy = item->blocking == DECOR_WALK_BEHIND;
        bool ordered = lv_obj_get_parent(obj) == (canopy ? s_canopy_layer : s_decor_layer);
        for (int j = i - 1; j >= 0; j--) {
            const decor_item_t *other = decor_at(j);
            if (!other || (other->blocking == DECOR_WALK_BEHIND) != canopy) continue;
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

void terrarium_refresh_gift(void)
{
    if (!s_gift_layer) return;
    lv_obj_clean(s_gift_layer);
    if (!g.gift.used) return;
    int index = sprite_gift_index(g.gift.kind);
    const lv_image_dsc_t *image = index >= 0 ? assets_image(index) : NULL;
    if (!image) {
        ESP_LOGW("spidarium.tank", "gift image unavailable");
        dot(s_gift_layer, g.gift.x, g.gift.y, 10, gift_def(g.gift.kind)->colour);
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
    lv_obj_t *obj = lv_canvas_create(s_gift_layer);
    lv_canvas_set_buffer(obj, s_gift_pixels, 64, 64, LV_COLOR_FORMAT_RGB565A8);
    lv_obj_set_pos(obj, g.gift.x - 32, g.gift.y - 32);
    lv_obj_set_clickable(obj, false);
    lv_obj_set_scrollable(obj, false);
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
         * unscaled top-left. */
        int32_t scale = def ? (int32_t)def->radius * 16 : 256;
        if (scale < 64) scale = 64;
        lv_image_set_src(obj, image);
        lv_image_set_scale(obj, scale);
        lv_obj_set_pos(obj, item->x - 32, item->y - 32);
        lv_obj_set_hidden(obj, false);
        lv_obj_t *parent = item->blocking == DECOR_WALK_BEHIND ? s_canopy_layer : s_decor_layer;
        if (lv_obj_get_parent(obj) != parent) lv_obj_set_parent(obj, parent);
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
    lv_obj_t *parent = path == DECOR_WALK_BEHIND ? s_canopy_layer : s_decor_layer;
    if (lv_obj_is_hidden(s_preview_image) || lv_obj_get_parent(s_preview_image) != parent)
        return false;
    int before = 0;
    for (int i = 0; i < s_decor_capacity; i++) {
        const decor_item_t *item = decor_at(i);
        if (!item || i == s_preview_slot ||
            (item->blocking == DECOR_WALK_BEHIND) != (path == DECOR_WALK_BEHIND)) continue;
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
    if (gift) lv_obj_set_hidden(s_gift_layer, true);
    else if (slot >= 0 && slot < s_decor_capacity) lv_obj_set_hidden(s_decor_obj[slot], true);
    lv_image_set_src(s_preview_image, image);
    lv_image_set_scale(s_preview_image, gift ? 512 : decor_def(kind)->radius * 16);
    lv_image_set_antialias(s_preview_image, false);
    lv_obj_set_pos(s_preview_image, x - image->header.w / 2, y - image->header.h / 2);
    lv_obj_set_hidden(s_preview_image, false);
    lv_obj_t *parent = gift ? lv_obj_get_parent(s_gift_layer) :
        path == DECOR_WALK_BEHIND ? s_canopy_layer : s_decor_layer;
    if (lv_obj_get_parent(s_preview_image) != parent) lv_obj_set_parent(s_preview_image, parent);
    if (gift) {
        lv_obj_move_to_index(s_preview_image, lv_obj_get_index(s_preview_ring));
    } else {
        int index = 0;
        for (int i = 0; i < s_decor_capacity; i++) {
            const decor_item_t *item = decor_at(i);
            if (!item || i == slot ||
                (item->blocking == DECOR_WALK_BEHIND) != (path == DECOR_WALK_BEHIND)) continue;
            if (index++ == layer_index) lv_obj_move_foreground(s_preview_image);
            lv_obj_move_foreground(s_decor_obj[i]);
        }
        if (layer_index >= index) lv_obj_move_foreground(s_preview_image);
    }
    terrarium_preview_show(x, y, gift ? 34 : decor_def(kind)->radius * 2 + 2);
}

void terrarium_preview_hide(void)
{
    lv_obj_set_hidden(s_preview_ring, true);
    lv_obj_set_hidden(s_preview_image, true);
    if (s_preview_gift) lv_obj_set_hidden(s_gift_layer, false);
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
    /* Decor is part of the floor, so its layer is created before the gift and
     * the droppings and therefore draws beneath them. */
    s_decor_layer = layer(parent);
    s_canopy_layer = layer(parent);
    terrarium_refresh_decor();
    s_gift_layer = layer(parent);
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
