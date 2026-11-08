#include "terrarium.h"

#include "assets.h"
#include "game_config.h"
#include "species.h"
#include "sprites.h"
#include "theme.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#define PIXEL_GRID 4

static lv_obj_t *s_background, *s_gift_layer, *s_poop_layer, *s_preview_ring;
static uint16_t *s_pixels;
static uint8_t *s_gift_pixels;
static uint32_t s_poop_at[MAX_POOP];

static const lv_image_dsc_t *background_asset(int scene)
{
    const lv_image_dsc_t *image = assets_image(sprite_background_index(scene));
    if (!image || image->header.cf != LV_COLOR_FORMAT_RGB565A8 ||
        image->header.w != 240 || image->header.h != 240 ||
        image->data_size != 240 * 240 * 3) {
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
        for (int i = 240 * 240 * 2; i < 240 * 240 * 3; i++)
            ok = ok && image->data[i] == 255;
        hashes[scene] = hash;
        for (int i = 0; i < scene; i++) ok = ok && hashes[i] != hash;
    }
    const lv_image_dsc_t *image = background_asset((int)(theme_current() - k_themes));
    if (!image) return false;
    const uint16_t *source = (const uint16_t *)image->data;
    for (int y = 0; ok && y < ui.H; y++)
        for (int x = 0; x < ui.W; x++)
            if (s_pixels[y * ui.W + x] !=
                s_pixels[(y / PIXEL_GRID * PIXEL_GRID) * ui.W + x / PIXEL_GRID * PIXEL_GRID] ||
                s_pixels[y * ui.W + x] != source[(y / 2) * 240 + x / 2])
                ok = false;
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

void terrarium_preview_show(int32_t x, int32_t y, int32_t radius)
{
    int r = radius > 0 ? radius : 12;
    lv_obj_set_size(s_preview_ring, r * 2, r * 2);
    lv_obj_set_pos(s_preview_ring, x - r, y - r);
    lv_obj_set_hidden(s_preview_ring, false);
}

void terrarium_preview_hide(void) { lv_obj_set_hidden(s_preview_ring, true); }

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
}

void terrarium_clear_poop(void)
{
    for (int i = 0; i < MAX_POOP; i++) {
        if (ui.poop[i]) lv_obj_delete(ui.poop[i]);
        ui.poop[i] = NULL;
        s_poop_at[i] = 0;
    }
}

int terrarium_poop_count(void)
{
    int n = 0;
    for (int i = 0; i < MAX_POOP; i++) if (ui.poop[i]) n++;
    return n;
}

void terrarium_tick(uint32_t now_ms)
{
    for (int i = 0; i < MAX_POOP; i++) {
        if (ui.poop[i] && now_ms - s_poop_at[i] >= POOP_LIFETIME_MS) {
            lv_obj_delete(ui.poop[i]);
            ui.poop[i] = NULL;
            s_poop_at[i] = 0;
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
    s_gift_layer = layer(parent);
    s_gift_pixels = heap_caps_malloc(64 * 64 * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(s_gift_pixels ? ESP_OK : ESP_ERR_NO_MEM);
    s_poop_layer = layer(parent);
    s_preview_ring = layer(parent);
    lv_obj_set_style_border_width(s_preview_ring, 2, 0);
    lv_obj_set_style_border_color(s_preview_ring, lv_color_hex(0xf5c86b), 0);
    lv_obj_set_hidden(s_preview_ring, true);
    terrarium_refresh_gift();
}
