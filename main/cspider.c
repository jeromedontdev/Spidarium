/*
 * Spidarium — canvas spider renderer, implementation.
 *
 * One canvas, drawn from scratch each frame. The anatomy is the same as the
 * widget version: two body masses with a faked gradient, a fuzzy rim, four
 * secondary eyes, two big eyes with pupils and glints, two fangs, and eight
 * jointed legs. Only the output changed.
 *
 * Everything is computed in body-local (forward, right) space and rotated by
 * the heading, so the head leads the direction of travel exactly as before.
 */
#include "cspider.h"

#include "behaviour.h"
#include "game.h"
#include "game_config.h"
#include "spider.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "spidarium.cspider";

#define PI_F 3.14159265f

/* ------------------------------------------------------------------ */
/* Geometry                                                            */
/* ------------------------------------------------------------------ */
/* The canvas is square and centred on the spider. It must be large enough for
 * the legs at full stretch: BODY_R + LEG_LEN * (SEG1 + SEG2) = 16 + 22*1.30 =
 * 44.6 px from the centre, so 96 px gives 48 px of half-width and a little
 * margin. */
#define CANVAS_SIZE   96
#define CANVAS_C      (CANVAS_SIZE / 2)

/* Body masses, in body-local pixels. Same numbers as the widget renderer so the
 * creature looks identical. */
#define ABD_FWD      (-13.0f)
#define ABD_R        13
#define CEPH_FWD     8.0f
#define CEPH_R       11

#define EYE_FWD      6.0f
#define EYE_DX       5.5f
#define EYE_R        7
#define PUPIL_R      4
#define EYE_SMALL_R  2
#define EYE_MID_FWD  (-4.0f)
#define EYE_MID_DX   8.0f
#define EYE_REAR_FWD (-8.0f)
#define EYE_REAR_DX  4.0f

#define FANG_LEN     4.5f
#define FANG_HALF_W  3.0f
#define FANG_FWD     15.5f
#define FANG_DX      4.0f

#define FUZZ_COUNT   14
#define FUZZ_OUT     2.0f
#define FUZZ_R       3

#define LEG_LEN      22.0f
#define LEG_SEG1     0.58f
#define LEG_SEG2     0.72f
#define LEG_WIDTH    3
#define NUM_LEGS     8
#define BODY_R       16

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */
static lv_obj_t   *s_canvas;

/* The canvas is RGB565A8: an RGB565 colour plane followed by an 8-bit alpha
 * plane.
 *
 * Plain RGB565 has NO alpha channel, so clearing the buffer to zero produced
 * opaque black and the spider sat inside a black square. The format must carry
 * alpha for the cleared pixels to be transparent.
 *
 * RGB565A8 rather than ARGB8888 because it is 3 bytes per pixel instead of 4:
 * 96x96x3 = 27,648 bytes. The alpha plane is what makes the tank show through,
 * and a separate 8-bit plane gives exactly that for a quarter less memory.
 *
 * The pixels are written DIRECTLY, not through LVGL draw layers -- see the note
 * on the drawing helpers below.
 *
 * The buffer lives in PSRAM, NOT internal RAM. As a static array it came
 * straight out of internal SRAM and dropped the largest free DMA block from
 * 40,960 to 32,768 -- which is the pool the WiFi driver needs for its RX/TX
 * buffers. The canvas does not need to be DMA-capable: LVGL's software
 * renderer writes it with the CPU, and the blit to the LCD reads it with the
 * CPU. Only the LCD's own draw buffers need DMA.
 *
 * Allocated once at init and never freed, so there is no fragmentation cost. */
/* RGB565 colour plane (2 bytes/px) + A8 alpha plane (1 byte/px). */
#define CANVAS_BPP   3
#define CANVAS_BYTES (CANVAS_SIZE * CANVAS_SIZE * CANVAS_BPP)

static uint8_t *s_buf;

static cspider_emote_t s_emote;

/* Gait, carried over from the widget renderer. */
static float    s_phase;
static float    s_last_x, s_last_y;
static bool     s_last_valid;
static float    s_speed_smooth;

#define GAIT_RATE   0.30f
#define GAIT_SWING  0.42f
#define GAIT_LIFT   0.30f

/* ------------------------------------------------------------------ */
/* Drawing helpers — DIRECT PIXEL WRITES                               */
/* ------------------------------------------------------------------ */
/* These write straight into the canvas buffer instead of going through
 * lv_draw_rect()/lv_draw_line()/lv_draw_triangle().
 *
 * That is the Pocket Tank approach: render.c is a plain RGB565 software
 * renderer with no LVGL draw layers at all. The reason matters here. Going
 * through LVGL's draw layer made the software renderer allocate temporary
 * blending buffers for every primitive, every frame. Those allocations came out
 * of internal DMA-capable RAM -- the same pool the LCD SPI driver needs -- and
 * the display started failing with "Draw bitmap failed: ESP_ERR_NO_MEM" after
 * about thirty seconds of running.
 *
 * Writing pixels directly allocates nothing. The buffer is ours, the format is
 * known, and a circle is a scanline loop.
 *
 * The canvas is RGB565 with a separate 8-bit alpha plane (RGB565A8), so a pixel
 * is "transparent" when its alpha byte is 0. That gives a real alpha channel
 * without paying 4 bytes per pixel. */

/* Pack a 0xRRGGBB colour into RGB565. */
static inline uint16_t rgb565(uint32_t rgb)
{
    uint16_t r = (uint16_t)((rgb >> 19) & 0x1F);
    uint16_t g = (uint16_t)((rgb >> 10) & 0x3F);
    uint16_t b = (uint16_t)((rgb >> 3)  & 0x1F);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

/* The colour plane and the alpha plane. LVGL's RGB565A8 layout is all the
 * colour data first, then all the alpha data. */
static inline uint16_t *px_color(void)
{
    return (uint16_t *)s_buf;
}

static inline uint8_t *px_alpha(void)
{
    return s_buf + (CANVAS_SIZE * CANVAS_SIZE * 2);
}

/* Set one pixel, clipped. */
static inline void put_px(int32_t x, int32_t y, uint16_t c)
{
    if (x < 0 || y < 0 || x >= CANVAS_SIZE || y >= CANVAS_SIZE) return;
    int32_t i = y * CANVAS_SIZE + x;
    px_color()[i] = c;
    px_alpha()[i] = 0xFF;
}

/* A filled circle, by scanline. */
static void fill_circle(int32_t cx, int32_t cy, int32_t r, uint32_t rgb)
{
    if (r <= 0) return;

    uint16_t c = rgb565(rgb);
    int32_t r2 = r * r;

    for (int32_t dy = -r; dy <= r; dy++) {
        int32_t y = cy + dy;
        if (y < 0 || y >= CANVAS_SIZE) continue;

        /* Half-width of the circle at this row. */
        int32_t w = (int32_t)sqrtf((float)(r2 - dy * dy));
        int32_t x0 = cx - w;
        int32_t x1 = cx + w;

        if (x0 < 0) x0 = 0;
        if (x1 >= CANVAS_SIZE) x1 = CANVAS_SIZE - 1;

        uint16_t *row = px_color() + y * CANVAS_SIZE;
        uint8_t  *alp = px_alpha() + y * CANVAS_SIZE;
        for (int32_t x = x0; x <= x1; x++) {
            row[x] = c;
            alp[x] = 0xFF;
        }
    }
}

/* A thick line segment, by stepping along it and stamping a small disc. */
static void draw_line(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                      int32_t w, uint32_t rgb)
{
    uint16_t c = rgb565(rgb);

    int32_t dx = x2 - x1;
    int32_t dy = y2 - y1;
    int32_t steps = (abs(dx) > abs(dy)) ? abs(dx) : abs(dy);
    if (steps <= 0) { put_px(x1, y1, c); return; }

    int32_t half = w / 2;

    for (int32_t i = 0; i <= steps; i++) {
        int32_t x = x1 + dx * i / steps;
        int32_t y = y1 + dy * i / steps;

        /* Stamp a half-width disc so the line has thickness and rounded ends. */
        for (int32_t oy = -half; oy <= half; oy++) {
            for (int32_t ox = -half; ox <= half; ox++) {
                if (ox * ox + oy * oy <= half * half + half) {
                    put_px(x + ox, y + oy, c);
                }
            }
        }
    }
}

/* A filled triangle, by scanline. This is the thing LVGL has no widget for, and
 * the reason the old renderer needed a draw callback. Here it is a loop. */
static void fill_triangle(int32_t x0, int32_t y0,
                          int32_t x1, int32_t y1,
                          int32_t x2, int32_t y2,
                          uint32_t rgb)
{
    uint16_t c = rgb565(rgb);

    int32_t miny = y0, maxy = y0;
    if (y1 < miny) miny = y1;
    if (y2 < miny) miny = y2;
    if (y1 > maxy) maxy = y1;
    if (y2 > maxy) maxy = y2;

    if (miny < 0) miny = 0;
    if (maxy >= CANVAS_SIZE) maxy = CANVAS_SIZE - 1;

    for (int32_t y = miny; y <= maxy; y++) {
        /* Find the span of the triangle on this row by testing each edge. */
        int32_t lo = CANVAS_SIZE, hi = -1;

        int32_t ex[3] = { x0, x1, x2 };
        int32_t ey[3] = { y0, y1, y2 };

        for (int e = 0; e < 3; e++) {
            int32_t ax = ex[e],       ay = ey[e];
            int32_t bx = ex[(e + 1) % 3], by = ey[(e + 1) % 3];

            if (ay == by) {
                if (y == ay) {
                    if (ax < lo) lo = ax;
                    if (ax > hi) hi = ax;
                    if (bx < lo) lo = bx;
                    if (bx > hi) hi = bx;
                }
                continue;
            }

            if (y < ay && y < by) continue;
            if (y > ay && y > by) continue;

            int32_t x = ax + (bx - ax) * (y - ay) / (by - ay);
            if (x < lo) lo = x;
            if (x > hi) hi = x;
        }

        if (hi < lo) continue;
        if (lo < 0) lo = 0;
        if (hi >= CANVAS_SIZE) hi = CANVAS_SIZE - 1;

        uint16_t *row = px_color() + y * CANVAS_SIZE;
        uint8_t  *alp = px_alpha() + y * CANVAS_SIZE;
        for (int32_t x = lo; x <= hi; x++) {
            row[x] = c;
            alp[x] = 0xFF;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Body-local to canvas                                                */
/* ------------------------------------------------------------------ */
/* Rotate a body-local (forward, right) offset into canvas coordinates.
 *
 * This is the whole point of the rewrite: the old renderer had to register
 * every part and reposition 46 widgets each frame. Here it is one multiply. */
static void to_canvas(float fwd, float right, float heading,
                      float scale_f, float scale_r,
                      int32_t *out_x, int32_t *out_y)
{
    float fx = cosf(heading), fy = sinf(heading);
    float rx = -fy,           ry = fx;

    float f = fwd   * scale_f;
    float r = right * scale_r;

    *out_x = CANVAS_C + (int32_t)(fx * f + rx * r);
    *out_y = CANVAS_C + (int32_t)(fy * f + ry * r);
}

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */
void cspider_init(lv_obj_t *parent)
{
    /* PSRAM first, internal as a fallback. See the note on s_buf. */
    s_buf = heap_caps_malloc(CANVAS_BYTES,
                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_buf) {
        ESP_LOGW(TAG, "no PSRAM for canvas, falling back to internal");
        s_buf = heap_caps_malloc(CANVAS_BYTES,
                                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!s_buf) {
        ESP_LOGE(TAG, "canvas buffer allocation failed");
        return;
    }

    s_canvas = lv_canvas_create(parent);
    lv_obj_remove_style_all(s_canvas);
    lv_obj_set_size(s_canvas, CANVAS_SIZE, CANVAS_SIZE);
    lv_obj_set_style_bg_opa(s_canvas, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_canvas, 0, 0);
    lv_obj_set_scrollable(s_canvas, false);
    lv_obj_set_clickable(s_canvas, false);

    lv_canvas_set_buffer(s_canvas, s_buf, CANVAS_SIZE, CANVAS_SIZE,
                         LV_COLOR_FORMAT_RGB565A8);

    /* Start fully transparent so the tank shows through. Zero alpha is
     * transparent, which is exactly what is wanted here. */
    memset(s_buf, 0, CANVAS_BYTES);

    s_phase = 0.0f;
    s_last_valid = false;
    s_speed_smooth = 0.0f;

    ESP_LOGI(TAG, "canvas spider ready (%dx%d)", CANVAS_SIZE, CANVAS_SIZE);
}

lv_obj_t *cspider_obj(void)
{
    return s_canvas;
}

void cspider_set_emote(cspider_emote_t e)
{
    s_emote = e;
}

bool cspider_hit(int32_t x, int32_t y)
{
    /* The body is two overlapping circles. Test both, plus a little slack so a
     * tap on the edge still counts. */
    float dx = (float)(x - g.spider_x);
    float dy = (float)(y - g.spider_y);
    float d2 = dx * dx + dy * dy;

    float reach = (float)(ABD_R + 6);
    return d2 <= reach * reach;
}

/* ------------------------------------------------------------------ */
/* Render                                                              */
/* ------------------------------------------------------------------ */
void cspider_render(uint32_t now_ms)
{
    if (!s_canvas) return;

    /* --- gait -------------------------------------------------------
     * Same logic as the widget renderer: advance the phase on a steady time
     * base, but only while the spider is genuinely translating. */
    float bx = (float)g.spider_x;
    float by = (float)g.spider_y;

    if (s_last_valid) {
        float dx = bx - s_last_x;
        float dy = by - s_last_y;
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist < 40.0f) {
            s_speed_smooth += (dist - s_speed_smooth) * 0.25f;
        }
    }
    s_last_x = bx;
    s_last_y = by;
    s_last_valid = true;

    bool moving = s_speed_smooth > 0.15f;
    if (moving) {
        s_phase += GAIT_RATE;
        while (s_phase > 2.0f * PI_F) s_phase -= 2.0f * PI_F;
    }

    /* --- squash/stretch ---------------------------------------------
     * The old renderer applied this by moving every part. Here it is two
     * scale factors passed to to_canvas(). */
    float sx = 1.0f, sy = 1.0f;
    if (g.state == SPIDER_POUNCE)      { sx = 1.17f; sy = 0.82f; }
    else if (g.state == SPIDER_AIM)    { sx = 1.11f; sy = 0.84f; }
    else if (g.state == SPIDER_SLEEP)  { sx = 1.05f; sy = 0.95f; }

    float heading = g.spider_heading;

    /* --- palette ---------------------------------------------------- */
    uint32_t col_base = spider_colour_swatch(g.colour);
    uint32_t col_mid  = col_base;
    uint32_t col_hi   = col_base;
    uint32_t col_fuzz = col_base;
    uint32_t col_pup  = spider_accent_colour(g.pupil_colour);
    uint32_t col_leg  = spider_accent_colour(g.leg_colour);
    uint32_t col_fang = spider_accent_colour(g.fang_colour);

    /* Lighten toward the highlight by blending with white. Cheap and good
     * enough at this size. */
    {
        uint32_t r = (col_base >> 16) & 0xFF;
        uint32_t gg = (col_base >> 8) & 0xFF;
        uint32_t b = col_base & 0xFF;
        r  = r  + (255 - r)  / 4;
        gg = gg + (255 - gg) / 4;
        b  = b  + (255 - b)  / 4;
        col_mid = (r << 16) | (gg << 8) | b;

        r  = r  + (255 - r)  / 3;
        gg = gg + (255 - gg) / 3;
        b  = b  + (255 - b)  / 3;
        col_hi = (r << 16) | (gg << 8) | b;

        col_fuzz = col_mid;
    }

    bool asleep = (g.state == SPIDER_SLEEP);

    /* --- clear ------------------------------------------------------
     * Zero alpha is transparent, so the tank shows through everywhere the
     * spider is not drawn. The colour plane does not need clearing because
     * nothing reads it where alpha is zero. */
    memset(px_alpha(), 0, CANVAS_SIZE * CANVAS_SIZE);

    int32_t x, y;

    /* --- legs -------------------------------------------------------
     * Drawn first so the body covers the hips. */
    for (int i = 0; i < NUM_LEGS; i++) {
        int side = (i < 4) ? -1 : 1;
        int k    = i & 3;

        float off  = (float)(k - 1.5f) * 0.42f;
        float base = heading + (float)side * (PI_F * 0.5f + off);

        float gait = s_phase
                   + (float)k * (PI_F * 0.5f)
                   + (float)((side < 0) ? 0.0f : PI_F);

        float swing = moving ? GAIT_SWING * sinf(gait) : 0.0f;
        float bend  = 0.55f + (moving ? GAIT_LIFT * sinf(gait + PI_F * 0.5f) : 0.0f);

        /* Emotes modulate the gait, exactly as before. */
        if (s_emote != CSP_EMOTE_NONE) {
            float t = (float)(now_ms % 1200u) / 1200.0f;
            float wave = sinf(t * 2.0f * PI_F);

            switch (s_emote) {
            case CSP_EMOTE_HAPPY:
                if (k == 0) {
                    swing += 1.10f * wave;
                    bend  -= 0.55f + 0.35f * wave;
                }
                break;
            case CSP_EMOTE_SAD:
                bend  += 0.60f;
                swing *= 0.35f;
                break;
            case CSP_EMOTE_ALERT:
                bend  -= 0.45f + 0.10f * wave;
                swing *= 0.25f;
                break;
            case CSP_EMOTE_SLEEPY:
                bend  -= 0.30f + 0.05f * wave;
                swing *= 0.15f;
                break;
            default:
                break;
            }
        }

        float dir1 = base + (float)side * (swing + bend);
        float dir2 = base - (float)side * (swing + bend);

        float hx = (float)CANVAS_C + (float)BODY_R * cosf(base);
        float hy = (float)CANVAS_C + (float)BODY_R * sinf(base);
        float kx = hx + LEG_LEN * LEG_SEG1 * cosf(dir1);
        float ky = hy + LEG_LEN * LEG_SEG1 * sinf(dir1);
        float fx = kx + LEG_LEN * LEG_SEG2 * cosf(dir2);
        float fy = ky + LEG_LEN * LEG_SEG2 * sinf(dir2);

        draw_line((int32_t)hx, (int32_t)hy,
                  (int32_t)kx, (int32_t)ky, LEG_WIDTH, col_leg);
        draw_line((int32_t)kx, (int32_t)ky,
                  (int32_t)fx, (int32_t)fy, LEG_WIDTH, col_leg);
    }

    /* --- abdomen ---------------------------------------------------- */
    to_canvas(ABD_FWD, 0.0f, heading, sx, sy, &x, &y);
    fill_circle(x, y, ABD_R, col_base);

    to_canvas(ABD_FWD - 1.5f, -1.5f, heading, sx, sy, &x, &y);
    fill_circle(x, y, ABD_R - 3, col_mid);

    to_canvas(ABD_FWD - 3.0f, -3.0f, heading, sx, sy, &x, &y);
    fill_circle(x, y, ABD_R - 7, col_hi);

    /* Fuzzy rim. */
    for (int i = 0; i < FUZZ_COUNT; i++) {
        float a = (float)i * 2.0f * PI_F / (float)FUZZ_COUNT;
        int32_t fr = FUZZ_R - (i % 2);
        to_canvas(ABD_FWD + cosf(a) * (float)(ABD_R + FUZZ_OUT),
                  sinf(a) * (float)(ABD_R + FUZZ_OUT),
                  heading, sx, sy, &x, &y);
        fill_circle(x, y, fr, col_fuzz);
    }

    /* --- cephalothorax ---------------------------------------------- */
    to_canvas(CEPH_FWD, 0.0f, heading, sx, sy, &x, &y);
    fill_circle(x, y, CEPH_R, col_base);

    to_canvas(CEPH_FWD - 1.5f, -1.5f, heading, sx, sy, &x, &y);
    fill_circle(x, y, CEPH_R - 3, col_mid);

    to_canvas(CEPH_FWD - 3.0f, -3.0f, heading, sx, sy, &x, &y);
    fill_circle(x, y, CEPH_R - 6, col_hi);

    for (int i = 0; i < FUZZ_COUNT; i++) {
        float a = (float)i * 2.0f * PI_F / (float)FUZZ_COUNT;
        int32_t fr = FUZZ_R - (i % 2);
        to_canvas(CEPH_FWD + cosf(a) * (float)(CEPH_R + FUZZ_OUT),
                  sinf(a) * (float)(CEPH_R + FUZZ_OUT),
                  heading, sx, sy, &x, &y);
        fill_circle(x, y, fr, col_fuzz);
    }

    /* --- secondary eyes --------------------------------------------- */
    const uint32_t col_sec = 0x6b7280;
    to_canvas(CEPH_FWD + EYE_MID_FWD, -EYE_MID_DX, heading, sx, sy, &x, &y);
    fill_circle(x, y, EYE_SMALL_R, col_sec);
    to_canvas(CEPH_FWD + EYE_MID_FWD, +EYE_MID_DX, heading, sx, sy, &x, &y);
    fill_circle(x, y, EYE_SMALL_R, col_sec);
    to_canvas(CEPH_FWD + EYE_REAR_FWD, -EYE_REAR_DX, heading, sx, sy, &x, &y);
    fill_circle(x, y, EYE_SMALL_R, col_sec);
    to_canvas(CEPH_FWD + EYE_REAR_FWD, +EYE_REAR_DX, heading, sx, sy, &x, &y);
    fill_circle(x, y, EYE_SMALL_R, col_sec);

    /* --- primary eyes ----------------------------------------------- */
    /* Asleep: the eyes go black, so the spider reads as resting rather than
     * staring. Same rule as the widget renderer. */
    uint32_t col_eye = asleep ? 0x000000 : 0xf7f9ff;

    to_canvas(CEPH_FWD + EYE_FWD, -EYE_DX, heading, sx, sy, &x, &y);
    fill_circle(x, y, EYE_R, col_eye);
    to_canvas(CEPH_FWD + EYE_FWD, +EYE_DX, heading, sx, sy, &x, &y);
    fill_circle(x, y, EYE_R, col_eye);

    /* --- pupils and glints ------------------------------------------ */
    if (!asleep) {
        to_canvas(CEPH_FWD + EYE_FWD + 1.0f, -EYE_DX, heading, sx, sy, &x, &y);
        fill_circle(x, y, PUPIL_R, col_pup);
        to_canvas(CEPH_FWD + EYE_FWD + 1.0f, +EYE_DX, heading, sx, sy, &x, &y);
        fill_circle(x, y, PUPIL_R, col_pup);

        to_canvas(CEPH_FWD + EYE_FWD + 2.0f, -EYE_DX - 2.0f,
                  heading, sx, sy, &x, &y);
        fill_circle(x, y, 1, 0xffffff);
        to_canvas(CEPH_FWD + EYE_FWD + 2.0f, +EYE_DX - 2.0f,
                  heading, sx, sy, &x, &y);
        fill_circle(x, y, 1, 0xffffff);
    }

    /* --- fangs ------------------------------------------------------
     * Short fat triangles pointing forward. In the widget renderer these needed
     * a LV_EVENT_DRAW_MAIN callback; here they are three points each. */
    if (!asleep) {
        for (int s = 0; s < 2; s++) {
            float side = (s == 0) ? -1.0f : 1.0f;

            /* Base corners, then the apex ahead of them. */
            int32_t bx0, by0, bx1, by1, ax, ay;

            to_canvas(CEPH_FWD + FANG_FWD - FANG_LEN * 0.5f,
                      side * FANG_DX - FANG_HALF_W,
                      heading, sx, sy, &bx0, &by0);
            to_canvas(CEPH_FWD + FANG_FWD - FANG_LEN * 0.5f,
                      side * FANG_DX + FANG_HALF_W,
                      heading, sx, sy, &bx1, &by1);
            to_canvas(CEPH_FWD + FANG_FWD + FANG_LEN * 0.5f,
                      side * FANG_DX,
                      heading, sx, sy, &ax, &ay);

            fill_triangle(bx0, by0, bx1, by1, ax, ay, col_fang);
        }
    }

    /* Tell LVGL the buffer changed. There is no layer to finish: the pixels
     * were written directly, so this is just an invalidate. */
    lv_obj_invalidate(s_canvas);

    /* --- position ---------------------------------------------------
     * Centre the canvas on the spider. */
    lv_obj_set_pos(s_canvas, g.spider_x - CANVAS_C, g.spider_y - CANVAS_C);

    /* Sick spiders are visibly dulled. */
    lv_obj_set_style_opa(s_canvas, g.sick ? LV_OPA_60 : LV_OPA_COVER, 0);
}
