/*
 * Spidarium — terrarium background implementation.
 */
#include "terrarium.h"

#include "game_config.h"
#include "theme.h"

#include <math.h>
#include <stdlib.h>

#define PI_F 3.14159265f

/* Colours now come from the active theme (theme.c), so the tank can be
 * customised. These macros keep the call sites readable. */
#define COL_SUBSTRATE_TOP   (theme_current()->substrate_top)
#define COL_SUBSTRATE_BOT   (theme_current()->substrate_bot)
#define COL_GLASS           (theme_current()->glass)
#define COL_WEB             (theme_current()->web)
#define COL_DUST            (theme_current()->dust)
#define COL_POOP            0x6b7a3a

#define DUST_COUNT          14

/* Web geometry. The old web was 10 spokes x 4 rings anchored a quarter of the
 * way into the playfield, so it covered a small patch and read as a target.
 * This is a corner web: spokes radiate from a point just off the top-left, and
 * the rings sag between them the way real silk does. */
#define WEB_SPOKES          11
#define WEB_RINGS           7
#define WEB_RING_STEPS      14
#define WEB_SAG             0.16f   /* ring sag between spokes, fraction of radius */
#define WEB_JITTER          0.05f   /* per-spoke angle wobble, radians */

/* Gift kinds: 0 pebble, 1 leaf, 2 twig, 3 flower, 4 crystal.
 * GIFT_KINDS lives in game.h so the gift picker can cycle it. */
static const uint32_t k_gift_colors[GIFT_KINDS] = {
    0x58d68d, 0x7c9cff, 0xf5b342, 0xff7eb6, 0x4fd1c5,
};

static lv_obj_t *s_bg_layer;      /* substrate + web, rebuilt on theme change */
static lv_obj_t *s_gift_layer;
static lv_obj_t *s_poop_layer;
static lv_obj_t *s_preview_ring;        /* persistent, toggled hidden */
static uint32_t  s_poop_at[MAX_POOP];   /* creation time, for decomposition */
static lv_obj_t *s_dust[DUST_COUNT];
static float     s_dust_x[DUST_COUNT];
static float     s_dust_y[DUST_COUNT];
static float     s_dust_vx[DUST_COUNT];
static float     s_dust_vy[DUST_COUNT];

/* Static point storage for web lines (lv_line stores the pointer).
 *
 * Exact line budget: add_web_line() silently drops anything past this, so an
 * undersized value truncates the web with no error. Each spoke is 2 segments,
 * and each ring is 2 segments per gap between adjacent spokes. */
#define WEB_MAX_LINES (WEB_SPOKES * 2 + WEB_RINGS * (WEB_SPOKES - 1) * 2)
static lv_point_precise_t s_web_pts[WEB_MAX_LINES][2];
static int                s_web_n;

static lv_obj_t *make_rect(lv_obj_t *parent, int32_t x, int32_t y,
                           int32_t w, int32_t h, uint32_t col, lv_opa_t opa)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, lv_color_hex(col), 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
    return o;
}

static lv_obj_t *make_dot(lv_obj_t *parent, int32_t x, int32_t y,
                          int32_t r, uint32_t col, lv_opa_t opa)
{
    lv_obj_t *o = make_rect(parent, x - r, y - r, r * 2, r * 2, col, opa);
    lv_obj_set_style_radius(o, r, 0);
    return o;
}

/* ------------------------------------------------------------------ */
/* Substrate + vignette                                                */
/* ------------------------------------------------------------------ */
static void build_substrate(lv_obj_t *parent)
{
    /* Vertical gradient approximated with bands: LVGL 9 has no gradient fill
     * for plain objects without a style gradient descriptor, and banding is
     * invisible at this contrast. */
    const int bands = 24;
    int32_t h = ui.H / bands;
    for (int i = 0; i < bands; i++) {
        float t = (float)i / (float)(bands - 1);
        uint32_t c = COL_SUBSTRATE_TOP;
        /* Lerp toward the darker bottom colour. */
        int r0 = (c >> 16) & 0xff, g0 = (c >> 8) & 0xff, b0 = c & 0xff;
        int r1 = (COL_SUBSTRATE_BOT >> 16) & 0xff;
        int g1 = (COL_SUBSTRATE_BOT >> 8) & 0xff;
        int b1 = COL_SUBSTRATE_BOT & 0xff;
        int rr = r0 + (int)((r1 - r0) * t);
        int gg = g0 + (int)((g1 - g0) * t);
        int bb = b0 + (int)((b1 - b0) * t);
        uint32_t col = ((uint32_t)rr << 16) | ((uint32_t)gg << 8) | (uint32_t)bb;
        make_rect(parent, 0, i * h, ui.W, h + 1, col, LV_OPA_COVER);
    }

    /* Glass vignette: a soft inner border that sells the tank walls. */
    const int32_t t = 3;
    make_rect(parent, 0, 0, ui.W, t, COL_GLASS, LV_OPA_40);
    make_rect(parent, 0, ui.H - t, ui.W, t, COL_GLASS, LV_OPA_40);
    make_rect(parent, 0, 0, t, ui.H, COL_GLASS, LV_OPA_40);
    make_rect(parent, ui.W - t, 0, t, ui.H, COL_GLASS, LV_OPA_40);
}

/* ------------------------------------------------------------------ */
/* Web                                                                 */
/* ------------------------------------------------------------------ */
static void add_web_line(lv_obj_t *parent, int32_t x1, int32_t y1,
                         int32_t x2, int32_t y2)
{
    if (s_web_n >= WEB_MAX_LINES) return;

    lv_obj_t *l = lv_line_create(parent);
    s_web_pts[s_web_n][0].x = (lv_value_precise_t)x1;
    s_web_pts[s_web_n][0].y = (lv_value_precise_t)y1;
    s_web_pts[s_web_n][1].x = (lv_value_precise_t)x2;
    s_web_pts[s_web_n][1].y = (lv_value_precise_t)y2;
    lv_line_set_points(l, s_web_pts[s_web_n], 2);
    s_web_n++;

    lv_obj_set_style_line_width(l, 1, 0);
    lv_obj_set_style_line_color(l, lv_color_hex(COL_WEB), 0);
    lv_obj_set_style_line_opa(l, LV_OPA_30, 0);
    lv_obj_set_clickable(l, false);
    lv_obj_set_scrollable(l, false);
}

/* Deterministic pseudo-random in [0,1). A fixed seed keeps the web identical
 * across reboots, so it reads as a place rather than as noise that changes
 * every time the tank is powered on. */
static float web_rand(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return (float)((*state >> 8) & 0xffff) / 65536.0f;
}

static void build_web(lv_obj_t *parent)
{
    /* A corner web anchored just OUTSIDE the top-left of the screen, so the
     * spokes fan across the whole tank instead of forming a small circle in
     * the middle. The anchor being off-screen is what makes it read as a web
     * built into the corner of a tank. */
    const int32_t cx = -30;
    const int32_t cy = -30;

    /* Long enough to reach the far corner from that anchor. */
    const int32_t R = (int32_t)(sqrtf((float)(ui.W * ui.W + ui.H * ui.H))) + 40;

    uint32_t rng = 0x5EED1234u;
    s_web_n = 0;

    /* Spoke angles: a quarter fan from the anchor, with a little wobble so no
     * two spokes are evenly spaced. */
    float spoke_a[WEB_SPOKES];
    for (int k = 0; k < WEB_SPOKES; k++) {
        float t = (float)k / (float)(WEB_SPOKES - 1);          /* 0..1 */
        float base = t * (PI_F * 0.5f);                        /* 0..90 deg */
        spoke_a[k] = base + (web_rand(&rng) - 0.5f) * WEB_JITTER * 2.0f;
    }

    /* Spokes. Each is drawn as two segments with a slight bend, because a
     * perfectly straight line reads as a diagram rather than as silk. */
    for (int k = 0; k < WEB_SPOKES; k++) {
        float a = spoke_a[k];
        float bend = (web_rand(&rng) - 0.5f) * 0.06f;

        int32_t mx = cx + (int32_t)(cosf(a + bend) * (float)R * 0.5f);
        int32_t my = cy + (int32_t)(sinf(a + bend) * (float)R * 0.5f);
        int32_t ex = cx + (int32_t)(cosf(a) * (float)R);
        int32_t ey = cy + (int32_t)(sinf(a) * (float)R);

        add_web_line(parent, cx, cy, mx, my);
        add_web_line(parent, mx, my, ex, ey);
    }

    /* Rings. Each ring is a chain of short segments between adjacent spokes,
     * sagging toward the anchor at its midpoint -- that sag is what makes a
     * web look organic instead of like a spider chart. */
    for (int r = 1; r <= WEB_RINGS; r++) {
        /* Rings are not evenly spaced: real webs are denser near the centre. */
        float t = (float)r / (float)WEB_RINGS;
        float rad = (float)R * t * t;

        for (int k = 0; k < WEB_SPOKES - 1; k++) {
            float a0 = spoke_a[k];
            float a1 = spoke_a[k + 1];

            int32_t x0 = cx + (int32_t)(cosf(a0) * rad);
            int32_t y0 = cy + (int32_t)(sinf(a0) * rad);
            int32_t x1 = cx + (int32_t)(cosf(a1) * rad);
            int32_t y1 = cy + (int32_t)(sinf(a1) * rad);

            /* Midpoint pulled toward the anchor by WEB_SAG. */
            int32_t mx = (x0 + x1) / 2;
            int32_t my = (y0 + y1) / 2;
            mx += (int32_t)((float)(cx - mx) * WEB_SAG);
            my += (int32_t)((float)(cy - my) * WEB_SAG);

            add_web_line(parent, x0, y0, mx, my);
            add_web_line(parent, mx, my, x1, y1);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Decor                                                               */
/* ------------------------------------------------------------------ */

/* Oval: a rect with a full radius reads as an ellipse at this size. Using a
 * numeric radius rather than LV_RADIUS_CIRCLE keeps it valid for non-square
 * shapes too. */
static lv_obj_t *make_oval(lv_obj_t *parent, int32_t cx, int32_t cy,
                           int32_t w, int32_t h, uint32_t col, lv_opa_t opa)
{
    lv_obj_t *o = make_rect(parent, cx - w / 2, cy - h / 2, w, h, col, opa);
    lv_obj_set_style_radius(o, (w < h ? w : h) / 2, 0);
    return o;
}

/* Every piece is drawn with the same light source: shadow down-right, highlight
 * up-left. Consistency is what makes a set of small shapes read as one art
 * style rather than as unrelated blobs. */
#define GIFT_SHADOW_OPA   LV_OPA_40
#define GIFT_BODY_OPA     LV_OPA_90
#define GIFT_HILITE_OPA   LV_OPA_40
#define GIFT_SHADOW_COL   0x000000

static void draw_gift_shape(lv_obj_t *parent, const gift_t *d)
{
    uint32_t col = k_gift_colors[d->kind % GIFT_KINDS];
    int32_t x = d->x, y = d->y;
    uint8_t v = d->variant % 3;   /* per-piece size/shape variation */

    switch (d->kind % GIFT_KINDS) {
    case 0: {  /* pebble: smooth stone, shadow + highlight */
        int32_t w = 22 + v * 3;          /* 22, 25, 28 */
        int32_t h = w * 2 / 3;
        make_oval(parent, x + 2, y + 3, w, h, GIFT_SHADOW_COL, GIFT_SHADOW_OPA);
        make_oval(parent, x, y, w, h, col, GIFT_BODY_OPA);
        make_oval(parent, x - w / 5, y - h / 4, w / 3, h / 3,
                  0xffffff, GIFT_HILITE_OPA);
        break;
    }

    case 1: {  /* leaf: pointed blade with a midrib and a stem */
        /* Stem first, so the blade sits on top of it. */
        make_rect(parent, x - 1, y + 4, 2, 10, col, LV_OPA_60);
        /* Blade: three ovals along a diagonal, shrinking toward the tip. */
        make_oval(parent, x - 3, y + 2, 16, 11, col, GIFT_BODY_OPA);
        make_oval(parent, x + 2, y - 2, 13,  9, col, GIFT_BODY_OPA);
        make_oval(parent, x + 6, y - 6,  9,  7, col, GIFT_BODY_OPA);
        /* Midrib: a light sliver along the blade axis. */
        make_oval(parent, x + 1, y - 2, 11, 2, 0xffffff, LV_OPA_30);
        break;
    }

    case 2: {  /* twig: main branch, two offshoots, buds at the tips */
        make_rect(parent, x - 13, y - 2, 26, 4, col, LV_OPA_80);
        make_rect(parent, x - 4, y - 9, 3, 9, col, LV_OPA_70);
        make_rect(parent, x + 5, y - 7, 3, 7, col, LV_OPA_70);
        make_dot(parent, x - 3, y - 10, 2, col, LV_OPA_90);
        make_dot(parent, x + 6, y -  8, 2, col, LV_OPA_90);
        break;
    }

    case 3: {  /* flower: six petals, bright centre, stem */
        make_rect(parent, x - 1, y + 4, 2, 9, 0x58d68d, LV_OPA_60);
        for (int i = 0; i < 6; i++) {
            float a = (float)i * PI_F / 3.0f;
            make_oval(parent, x + (int32_t)(cosf(a) * 7.0f),
                      y + (int32_t)(sinf(a) * 7.0f), 9, 7, col, LV_OPA_80);
        }
        make_dot(parent, x, y, 4, 0xfff3c4, LV_OPA_90);
        break;
    }

    default: {  /* crystal: polished gem with a top facet and a glint */
        make_oval(parent, x + 2, y + 3, 16, 16, GIFT_SHADOW_COL, GIFT_SHADOW_OPA);
        make_oval(parent, x, y, 16, 16, col, GIFT_BODY_OPA);
        make_oval(parent, x, y - 4, 10, 8, 0xffffff, LV_OPA_30);
        make_oval(parent, x - 3, y - 5, 4, 3, 0xffffff, LV_OPA_60);
        break;
    }
    }
}

void terrarium_refresh_gift(void)
{
    if (!s_gift_layer) return;

    lv_obj_clean(s_gift_layer);

    if (g.gift.used) draw_gift_shape(s_gift_layer, &g.gift);
}

/* ------------------------------------------------------------------ */
/* Placement preview                                                   */
/* ------------------------------------------------------------------ */
void terrarium_preview_show(int32_t x, int32_t y, int32_t radius)
{
    if (!s_preview_ring) return;

    /* A radius of 0 means the kind is global: show a small marker instead of
     * a misleadingly large circle. */
    int32_t r = (radius > 0) ? radius : 12;

    lv_obj_set_size(s_preview_ring, r * 2, r * 2);
    lv_obj_set_style_radius(s_preview_ring, r, 0);
    lv_obj_set_pos(s_preview_ring, x - r, y - r);
    lv_obj_set_hidden(s_preview_ring, false);
}

void terrarium_preview_hide(void)
{
    if (s_preview_ring) lv_obj_set_hidden(s_preview_ring, true);
}

/* ------------------------------------------------------------------ */
/* Poop                                                                */
/* ------------------------------------------------------------------ */
void terrarium_add_poop(int32_t x, int32_t y)
{
    if (!s_poop_layer) return;

    /* Reuse the first free slot; recycle the oldest when full. */
    int slot = -1;
    for (int i = 0; i < MAX_POOP; i++) {
        if (!ui.poop[i]) { slot = i; break; }
    }
    if (slot < 0) {
        lv_obj_del(ui.poop[0]);
        for (int i = 1; i < MAX_POOP; i++) {
            ui.poop[i - 1] = ui.poop[i];
            s_poop_at[i - 1] = s_poop_at[i];
        }
        ui.poop[MAX_POOP - 1] = NULL;
        s_poop_at[MAX_POOP - 1] = 0;
        slot = MAX_POOP - 1;
    }

    ui.poop[slot] = make_dot(s_poop_layer, x, y, 3 + (rand() % 2),
                             COL_POOP, LV_OPA_80);
    s_poop_at[slot] = lv_tick_get();
}

void terrarium_clear_poop(void)
{
    for (int i = 0; i < MAX_POOP; i++) {
        if (ui.poop[i]) {
            lv_obj_del(ui.poop[i]);
            ui.poop[i] = NULL;
        }
        s_poop_at[i] = 0;
    }
}

/* Poop decomposes so the tank can return to a clean state on its own. */
static void decompose_poop(uint32_t now_ms)
{
    for (int i = 0; i < MAX_POOP; i++) {
        if (!ui.poop[i]) continue;
        if (s_poop_at[i] && (now_ms - s_poop_at[i]) >= POOP_LIFETIME_MS) {
            lv_obj_del(ui.poop[i]);
            ui.poop[i] = NULL;
            s_poop_at[i] = 0;
        }
    }
}

int terrarium_poop_count(void)
{
    int n = 0;
    for (int i = 0; i < MAX_POOP; i++) {
        if (ui.poop[i]) n++;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* Dust motes                                                          */
/* ------------------------------------------------------------------ */
static void build_dust(lv_obj_t *parent)
{
    for (int i = 0; i < DUST_COUNT; i++) {
        s_dust_x[i] = (float)(g_play.left + rand() % (g_play.right - g_play.left));
        s_dust_y[i] = (float)(g_play.top + rand() % (g_play.bottom - g_play.top));
        s_dust_vx[i] = ((float)(rand() % 100) - 50.0f) / 900.0f;
        s_dust_vy[i] = -((float)(rand() % 40) + 10.0f) / 900.0f;
        s_dust[i] = make_dot(parent, (int32_t)s_dust_x[i], (int32_t)s_dust_y[i],
                             1, COL_DUST, LV_OPA_30);
    }
}

void terrarium_tick(uint32_t now_ms)
{
    decompose_poop(now_ms);

    for (int i = 0; i < DUST_COUNT; i++) {
        if (!s_dust[i]) continue;

        s_dust_x[i] += s_dust_vx[i];
        s_dust_y[i] += s_dust_vy[i];

        /* Wrap within the playfield. */
        if (s_dust_y[i] < (float)g_play.top) {
            s_dust_y[i] = (float)g_play.bottom;
            s_dust_x[i] = (float)(g_play.left + rand() % (g_play.right - g_play.left));
        }
        if (s_dust_x[i] < (float)g_play.left) s_dust_x[i] = (float)g_play.right;
        if (s_dust_x[i] > (float)g_play.right) s_dust_x[i] = (float)g_play.left;

        lv_obj_set_pos(s_dust[i], (int32_t)s_dust_x[i], (int32_t)s_dust_y[i]);
    }
}

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */
void terrarium_init(lv_obj_t *parent)
{
    /* Background lives on its own layer so a theme change can rebuild just
     * this without touching the gift, poop or dust layers. */
    s_bg_layer = lv_obj_create(parent);
    lv_obj_remove_style_all(s_bg_layer);
    lv_obj_set_size(s_bg_layer, ui.W, ui.H);
    lv_obj_set_pos(s_bg_layer, 0, 0);
    lv_obj_set_style_bg_opa(s_bg_layer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_bg_layer, 0, 0);
    lv_obj_set_scrollable(s_bg_layer, false);
    lv_obj_set_clickable(s_bg_layer, false);

    build_substrate(s_bg_layer);
    build_web(s_bg_layer);

    s_gift_layer = lv_obj_create(parent);
    lv_obj_remove_style_all(s_gift_layer);
    lv_obj_set_size(s_gift_layer, ui.W, ui.H);
    lv_obj_set_pos(s_gift_layer, 0, 0);
    lv_obj_set_style_bg_opa(s_gift_layer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_gift_layer, 0, 0);
    lv_obj_set_scrollable(s_gift_layer, false);
    lv_obj_set_clickable(s_gift_layer, false);

    s_poop_layer = lv_obj_create(parent);
    lv_obj_remove_style_all(s_poop_layer);
    lv_obj_set_size(s_poop_layer, ui.W, ui.H);
    lv_obj_set_pos(s_poop_layer, 0, 0);
    lv_obj_set_style_bg_opa(s_poop_layer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_poop_layer, 0, 0);
    lv_obj_set_scrollable(s_poop_layer, false);
    lv_obj_set_clickable(s_poop_layer, false);

    for (int i = 0; i < MAX_POOP; i++) ui.poop[i] = NULL;

    /* Placement preview ring: built once here, shown/hidden on demand. */
    s_preview_ring = lv_obj_create(parent);
    lv_obj_remove_style_all(s_preview_ring);
    lv_obj_set_style_bg_opa(s_preview_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_preview_ring, 2, 0);
    lv_obj_set_style_border_color(s_preview_ring, lv_color_hex(0xf5b342), 0);
    lv_obj_set_style_border_opa(s_preview_ring, LV_OPA_40, 0);
    lv_obj_set_scrollable(s_preview_ring, false);
    lv_obj_set_clickable(s_preview_ring, false);
    lv_obj_set_hidden(s_preview_ring, true);

    build_dust(parent);
    terrarium_refresh_gift();
}

/* Rebuild the background after a theme change. The substrate and web are
 * static objects, so switching theme means deleting and recreating them --
 * which is safe here because this is NOT called from an event callback. */
void terrarium_rebuild(void)
{
    if (!s_bg_layer) return;

    lv_obj_clean(s_bg_layer);
    s_web_n = 0;
    build_substrate(s_bg_layer);
    build_web(s_bg_layer);
}
