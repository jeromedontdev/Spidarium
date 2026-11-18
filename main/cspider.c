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

#include "ambient.h"
#include "behaviour.h"
#include "decor.h"
#include "game.h"
#include "game_config.h"
#include "spider.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "spidarium.cspider";

#define PI_F 3.14159265f

/* ------------------------------------------------------------------ */
/* Geometry                                                            */
/* ------------------------------------------------------------------ */
/* Bounded leg tips fit inside the 48px half-width at every heading.
 *
 * The canvas is TALLER than it is wide because the climb lift raises the body
 * inside the buffer while the canvas stays anchored at the floor. A 96x96
 * buffer clipped the body off the top once the lift passed ~30 px, which is
 * exactly the range the large climb structures use (up to 62 px). The extra
 * rows are transparent headroom; the body sits at CANVAS_CY and the contact
 * shadow follows it up, so both stay inside the buffer at full lift. */
#define CANVAS_W      96
#define CANVAS_H      160
#define CANVAS_CX     (CANVAS_W / 2)
#define CANVAS_CY     (CANVAS_H - CANVAS_W / 2)

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
#define BODY_R       16
#define LEG_WIDTH    3

/* How far below the body centre the contact shadow sits. It used to be drawn at
 * the leg tips (LEG_LEN, 22 px), which left a visible gap between the body and
 * its own shadow and read as the spider hovering a little above the ground.
 * Pulling it up to the body radius tucks it under the abdomen, where a contact
 * shadow belongs. */
#define SHADOW_DROP  (BODY_R - 2)

/* Forward-axis compression while perched on a small prop. 0.72 shortens the
 * body by roughly a quarter, which is what a lower viewing angle does to a
 * body pointing away from the viewer. Below about 0.6 the abdomen starts to
 * overlap the cephalothorax and the silhouette stops reading as a spider. */
#define PERCH_FORESHORTEN 0.72f

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */
static lv_obj_t   *s_canvas;
/* The climb contact shadow is drawn INSIDE the spider canvas, under the feet,
 * before the legs and body. It cannot be a separate widget: a widget needs its
 * own style and stacking, and the shadow has to sit under the body in the same
 * buffer to composite correctly. Drawing it first means the legs and body paint
 * over it, which is exactly the occlusion a real shadow has. */

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
#define CANVAS_BYTES (CANVAS_W * CANVAS_H * CANVAS_BPP)

static uint8_t *s_buf;

static cspider_emote_t s_emote;

/* Petting reaction: a timed overlay, not a state. See cspider_pet_react(). */
static bool     s_pet_reacting;
static uint32_t s_pet_react_until;

/* Trick overlay. Wave and Bow need a visible pose of their own; Spin is carried
 * by the heading rotation in behaviour.c. Without this, Wave and Bow both fell
 * through to the same idle crouch and the training menu looked broken. */
static bool     s_trick_reacting;
static uint32_t s_trick_react_until;
static uint8_t  s_trick_react_kind;

/* Gait, carried over from the widget renderer. */
typedef struct {
    float phase, x, y;
    bool valid;
    uint32_t moved_at;
} gait_t;
static gait_t s_gait;

#define STRIDE_PX 24.0f
typedef struct { float hf, hr, kf, kr, ff, fr; } leg_pose_t;

static bool walk_state(spider_state_t state)
{
    return state == SPIDER_WANDER || state == SPIDER_STALK || state == SPIDER_SICK;
}

static bool gait_update(gait_t *gait, float x, float y, uint32_t now,
                         spider_state_t state)
{
    float dx = x - gait->x, dy = y - gait->y;
    float distance = sqrtf(dx * dx + dy * dy);
    if (gait->valid && walk_state(state) && distance > 0 && distance <= 12) {
        gait->phase = fmodf(gait->phase + distance * (2 * PI_F / STRIDE_PX), 2 * PI_F);
        gait->moved_at = now;
    }
    gait->x = x;
    gait->y = y;
    gait->valid = true;
    return walk_state(state) && gait->moved_at && now - gait->moved_at < 100;
}

/* Original angular anatomy, with distance-driven timing and no inverted bends.
 *
 * The front pair (k == 0) leads the walk: it swings wider and bends further
 * than the trailing pairs, so the legs nearest the eyes visibly reach and
 * plant instead of trailing the rear legs. The extra amplitude is applied
 * before the 0.15..1.25 radian clamp, so it can never invert a knee. */
static leg_pose_t leg_pose(int k, int side, float phase, bool walking,
                           cspider_emote_t emote, uint32_t now)
{
    float base = side * (PI_F * 0.5f + (k - 1.5f) * 0.42f);
    float gait = phase + k * (PI_F * 0.5f) + (side < 0 ? 0 : PI_F);
    float lead = k == 0 ? 1.55f : k == 1 ? 1.15f : 1.0f;
    float swing = walking ? 0.42f * lead * sinf(gait) : 0;
    float bend = 0.55f + (walking ? 0.30f * lead * sinf(gait + PI_F * 0.5f) : 0);
    float wave = sinf((now % 1200u) * (2 * PI_F / 1200));
    switch (emote) {
    case CSP_EMOTE_HAPPY:
        if (k == 0) {
            swing += 1.10f * wave;
            bend -= 0.55f + 0.35f * wave;
        }
        break;
    case CSP_EMOTE_SAD:
        bend += 0.60f;
        swing *= 0.35f;
        break;
    case CSP_EMOTE_ALERT:
        bend -= 0.45f + 0.10f * wave;
        swing *= 0.25f;
        break;
    case CSP_EMOTE_SLEEPY:
        bend -= 0.30f + 0.05f * wave;
        swing *= 0.15f;
        break;
    default:
        break;
    }
    /* Original happy/alert modulation could cross zero and reverse the knee. */
    float angle = fmaxf(0.15f, fminf(1.25f, swing + bend));
    float dir1 = base + side * angle, dir2 = base - side * angle;
    leg_pose_t pose = {BODY_R * cosf(base), BODY_R * sinf(base), 0, 0, 0, 0};
    pose.kf = pose.hf + LEG_LEN * LEG_SEG1 * cosf(dir1);
    pose.kr = pose.hr + LEG_LEN * LEG_SEG1 * sinf(dir1);
    pose.ff = pose.kf + LEG_LEN * LEG_SEG2 * cosf(dir2);
    pose.fr = pose.kr + LEG_LEN * LEG_SEG2 * sinf(dir2);
    return pose;
}

bool cspider_gait_test(void)
{
    static const leg_pose_t original[] = {
        {9.426316f, 12.928440f, 10.446028f, 25.647630f, 25.091787f, 31.681479f},
        {3.335358f, 15.648495f, -0.919937f, 27.678044f, 9.992579f, 39.159447f},
        {-3.335358f, 15.648495f, -12.125996f, 24.897402f, -6.843560f, 39.830636f},
        {-9.426316f, 12.928440f, -21.224289f, 17.789041f, -22.490138f, 33.578380f},
    };
    float phases[3];
    const int frames[] = {20, 30, 60};
    bool ok = true;
    for (int k = 0; k < 4; k++)
        for (int side = -1; side <= 1; side += 2) {
            leg_pose_t p = leg_pose(k, side, 0, false, CSP_EMOTE_NONE, 0);
            ok = ok && fabsf(p.hf - original[k].hf) < 0.001f &&
                fabsf(p.hr - side * original[k].hr) < 0.001f &&
                fabsf(p.kf - original[k].kf) < 0.001f &&
                fabsf(p.kr - side * original[k].kr) < 0.001f &&
                fabsf(p.ff - original[k].ff) < 0.001f &&
                fabsf(p.fr - side * original[k].fr) < 0.001f;
        }
    for (int rate = 0; rate < 3; rate++) {
        gait_t gait = {0};
        gait_update(&gait, 0, 0, 1000, SPIDER_WANDER);
        for (int i = 1; i <= frames[rate]; i++)
            gait_update(&gait, 18.0f * i / frames[rate], 0,
                         1000 + 1000 * i / frames[rate], SPIDER_WANDER);
        phases[rate] = gait.phase;
        for (int i = 0; i < 100; i++) gait_update(&gait, 18, 0, 2000, SPIDER_WANDER);
        ok = ok && fabsf(gait.phase - phases[rate]) < 0.0001f;
        gait_update(&gait, 100, 0, 2100, SPIDER_POUNCE);
        ok = ok && fabsf(gait.phase - phases[rate]) < 0.0001f;
        ok = ok && !gait_update(&gait, 100, 0, 2200, SPIDER_IDLE);
    }
    ok = ok && fabsf(phases[0] - phases[1]) < 0.001f &&
        fabsf(phases[0] - phases[2]) < 0.001f;
    for (int step = 0; step < 256; step++)
        for (int emote = CSP_EMOTE_NONE; emote <= CSP_EMOTE_SLEEPY; emote++)
            for (int k = 0; k < 4; k++)
                for (int side = -1; side <= 1; side += 2) {
                    leg_pose_t p = leg_pose(k, side, step * (2 * PI_F / 256), true,
                                            emote, step * 10);
                    float bend = (p.kf - p.hf) * (p.fr - p.kr) -
                                 (p.kr - p.hr) * (p.ff - p.kf);
                    float expected = -side;
                    float upper = hypotf(p.kf - p.hf, p.kr - p.hr);
                    float lower = hypotf(p.ff - p.kf, p.fr - p.kr);
                    ok = ok && bend * expected > 40 &&
                        fabsf(upper - LEG_LEN * LEG_SEG1) < 0.001f &&
                        fabsf(lower - LEG_LEN * LEG_SEG2) < 0.001f;
                    ok = ok && hypotf(p.ff, p.fr) <= BODY_R + LEG_LEN * (LEG_SEG1 + LEG_SEG2);
                    {
                        leg_pose_t r = leg_pose(k, side, 0, false, emote, step * 10);
                        float signed_bend = (r.kf - r.hf) * (r.fr - r.kr) -
                                            (r.kr - r.hr) * (r.ff - r.kf);
                        ok = ok && isfinite(r.kf) && isfinite(r.kr) &&
                            signed_bend * expected > 40 &&
                            fabsf(hypotf(r.kf - r.hf, r.kr - r.hr) - LEG_LEN * LEG_SEG1) < 0.001f &&
                            fabsf(hypotf(r.ff - r.kf, r.fr - r.kr) - LEG_LEN * LEG_SEG2) < 0.001f;
                    }
                }
    return ok;
}

/* ------------------------------------------------------------------ */
/* Drawing helpers — DIRECT PIXEL WRITES                               */
/* ------------------------------------------------------------------ */
/* These write straight into the canvas buffer instead of going through
 * lv_draw_rect()/lv_draw_line()/lv_draw_triangle().
 *
 * This is a plain RGB565 software renderer with no LVGL draw layers.
 * The reason matters here. Going
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
    return s_buf + (CANVAS_W * CANVAS_H * 2);
}

/* Set one pixel, clipped. */
static inline void put_px(int32_t x, int32_t y, uint16_t c)
{
    if (x < 0 || y < 0 || x >= CANVAS_W || y >= CANVAS_H) return;
    int32_t i = y * CANVAS_W + x;
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
        if (y < 0 || y >= CANVAS_H) continue;

        /* Half-width of the circle at this row. */
        int32_t w = (int32_t)sqrtf((float)(r2 - dy * dy));
        int32_t x0 = cx - w;
        int32_t x1 = cx + w;

        if (x0 < 0) x0 = 0;
        if (x1 >= CANVAS_W) x1 = CANVAS_W - 1;

        uint16_t *row = px_color() + y * CANVAS_W;
        uint8_t  *alp = px_alpha() + y * CANVAS_W;
        for (int32_t x = x0; x <= x1; x++) {
            row[x] = c;
            alp[x] = 0xFF;
        }
    }
}

/* A soft elliptical contact shadow, drawn at the floor point BEFORE the body.
 *
 * The canvas is RGB565A8, so opacity lives in the alpha plane. The shadow is
 * written as black with a radial alpha falloff. Because it is drawn first, the
 * legs and body paint over its middle and only the feathered rim shows; that
 * rim is what reads as the spider being off the ground.
 *
 * A separate LVGL widget was tried first and never rendered: a widget needs its
 * own style and stacking, and the shadow has to composite under the body in the
 * same buffer anyway. Drawing it here keeps it in one invalidate and one blit. */
static void fill_shadow(int32_t cx, int32_t cy, int32_t rx, int32_t ry,
                        uint8_t max_alpha)
{
    if (rx <= 0 || ry <= 0) return;

    for (int32_t dy = -ry; dy <= ry; dy++) {
        int32_t y = cy + dy;
        if (y < 0 || y >= CANVAS_H) continue;

        float ny = (float)dy / (float)ry;
        float span = 1.0f - ny * ny;
        if (span <= 0.0f) continue;

        int32_t w = (int32_t)((float)rx * sqrtf(span));
        if (w <= 0) continue;

        int32_t x0 = cx - w;
        int32_t x1 = cx + w;
        if (x0 < 0) x0 = 0;
        if (x1 >= CANVAS_W) x1 = CANVAS_W - 1;

        uint16_t *row = px_color() + y * CANVAS_W;
        uint8_t  *alp = px_alpha() + y * CANVAS_W;

        for (int32_t x = x0; x <= x1; x++) {
            float nx = (float)(x - cx) / (float)rx;
            float d2 = nx * nx + ny * ny;
            if (d2 >= 1.0f) continue;

            /* Squared falloff: dense at the centre, feathered at the rim. */
            float f = 1.0f - d2;
            uint8_t a = (uint8_t)((float)max_alpha * f * f);
            if (a == 0) continue;

            row[x] = 0x0000;
            alp[x] = a;
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
    if (maxy >= CANVAS_H) maxy = CANVAS_H - 1;

    for (int32_t y = miny; y <= maxy; y++) {
        /* Find the span of the triangle on this row by testing each edge. */
        int32_t lo = CANVAS_H, hi = -1;

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
        if (hi >= CANVAS_H) hi = CANVAS_H - 1;

        uint16_t *row = px_color() + y * CANVAS_W;
        uint8_t  *alp = px_alpha() + y * CANVAS_W;
        for (int32_t x = lo; x <= hi; x++) {
            row[x] = c;
            alp[x] = 0xFF;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Body-local to canvas                                                */
/* Vertical offset applied to every body part, in canvas pixels. The climb
 * feature raises the spider by writing a negative value here rather than moving
 * the widget: the canvas stays anchored at the spider's floor position, so the
 * body and its contact shadow both rise inside the buffer and the widget
 * position stays a pure function of the logical floor point. */
static int32_t s_draw_dy;

/* Foreshortening of the body's forward axis, 1.0 = flat top-down.
 *
 * Perching on a small prop needs a different pose, not just a taller one: a
 * flat top-down spider sitting on a rock still reads as a flat top-down spider.
 * Compressing the forward axis is what a lower viewing angle does to a body
 * pointing away from the viewer, so the abdomen crowds toward the
 * cephalothorax and the silhouette shortens. Combined with the lift and the
 * outward heading, that is the 2.5D read. */
static float s_foreshorten = 1.0f;

/* There is deliberately NO perspective shift while climbing.
 *
 * Three attempts were made at one: a forward-axis compression, a body tilt
 * following the surface gradient, and finally a full dorsal projection that
 * turned the top-down sprite into a side view. Each was individually plausible
 * and each looked wrong on the panel, because a 96x96 pixel-art sprite drawn
 * from above cannot be re-projected into a convincing profile by moving its
 * parts around. The silhouette is authored top-down; skewing it produces a
 * distorted top-down spider, not a side view.
 *
 * What remains is the honest part of the effect: the spider is LIFTED up the
 * structure, with its contact shadow under its feet, so it visibly stands on
 * something with height. That reads correctly and costs nothing.
 *
 * Do not reintroduce a profile blend, a climb foreshortening or a surface tilt
 * without a physical-panel check. The constants and the dorsal projection were
 * removed rather than left dormant so they cannot be switched back on by
 * accident. */

/* ------------------------------------------------------------------ */
/* Rotate a body-local (forward, right) offset into canvas coordinates.
 *
 * This is the whole point of the rewrite: the old renderer had to register
 * every part and reposition 46 widgets each frame. Here it is one multiply.
 *
 * The spider is always drawn from directly above. There is no dorsal axis and
 * no body tilt: see the note above CLIMB_FULL_LIFT for why the climb
 * perspective was removed rather than tuned. */
static void to_canvas(float fwd, float right, float heading,
                      float scale_f, float scale_r,
                      int32_t *out_x, int32_t *out_y)
{
    float fx = cosf(heading), fy = sinf(heading);
    float rx = -fy,           ry = fx;

    float f = fwd   * scale_f * s_foreshorten;
    float r = right * scale_r;

    float ox = fx * f + rx * r;
    float oy = fy * f + ry * r;

    *out_x = CANVAS_CX + (int32_t)ox;
    *out_y = CANVAS_CY + (int32_t)oy + s_draw_dy;
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
    lv_obj_set_size(s_canvas, CANVAS_W, CANVAS_H);
    lv_obj_set_style_bg_opa(s_canvas, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_canvas, 0, 0);
    lv_obj_set_scrollable(s_canvas, false);
    lv_obj_set_clickable(s_canvas, false);

    lv_canvas_set_buffer(s_canvas, s_buf, CANVAS_W, CANVAS_H,
                         LV_COLOR_FORMAT_RGB565A8);

    /* Start fully transparent so the tank shows through. Zero alpha is
     * transparent, which is exactly what is wanted here. */
    memset(s_buf, 0, CANVAS_BYTES);

    memset(&s_gait, 0, sizeof s_gait);

    ESP_LOGI(TAG, "canvas spider ready (%dx%d)", CANVAS_W, CANVAS_H);
}

lv_obj_t *cspider_obj(void){
    return s_canvas;
}

void cspider_shadow_report(void)
{
    /* The shadow is the ONLY thing that writes partial alpha: the body and legs
     * write 0xFF and the cleared background is 0. Counting pixels with alpha in
     * 1..254 therefore proves whether the shadow actually reached the buffer,
     * independently of whether it is visible on screen.
     *
     * `rows` is the shadow's vertical extent in the canvas. It must TRACK the
     * lift: the shadow follows the body, so its rows move up as the spider
     * climbs. A shadow whose rows stay put while `lift` rises is the "floating
     * above the decor" bug -- it was pinned to the logical floor point. */
    int partial = 0, opaque = 0;
    int min_y = CANVAS_H, max_y = -1;
    if (s_buf) {
        const uint8_t *alp = px_alpha();
        for (int32_t y = 0; y < CANVAS_H; y++) {
            for (int32_t x = 0; x < CANVAS_W; x++) {
                uint8_t a = alp[y * CANVAS_W + x];
                if (a == 0xFF) { opaque++; }
                else if (a != 0) {
                    partial++;
                    if (y < min_y) min_y = y;
                    if (y > max_y) max_y = y;
                }
            }
        }
    }
    /* `fs` is the perch foreshortening only; climbing no longer changes the
     * pose, so there is no tilt or profile to report. */
    printf("SHADOW lift=%ld fs=%.3f canvas=(%ld,%ld) "
           "partial=%d opaque=%d rows=%d..%d\n",
           (long)decor_climb_lift(g.spider_x, g.spider_y, game_now()),
           (double)s_foreshorten,
           (long)lv_obj_get_x(s_canvas), (long)lv_obj_get_y(s_canvas),
           partial, opaque, min_y, max_y);
}
void cspider_set_emote(cspider_emote_t e)
{
    s_emote = e;
}

/* A short bounce plus a happy wave, played when the player pets the spider.
 *
 * This is a timed overlay rather than a state change: ui_hud_tick() recomputes
 * the emote from the game state on every frame, so an emote set from the pet
 * handler would be overwritten before it was ever drawn. Holding the reaction
 * here also means a pet never interrupts a hunt or a sleep. */
void cspider_pet_react(void)
{
    s_pet_reacting = true;
    s_pet_react_until = lv_tick_get() + PET_REACT_MS;
}

/* Trick overlay. kind 0 = Wave, 1 = Spin, 2 = Bow.
 *
 * Spin is already visible: behaviour.c rotates the heading through a full turn.
 * Wave and Bow had no visual at all, so training them looked like nothing
 * happened. Wave lifts the front pair and bounces; Bow dips the body forward. */
void cspider_trick_react(unsigned kind)
{
    if (kind > 2) return;
    s_trick_reacting = true;
    s_trick_react_kind = (uint8_t)kind;
    s_trick_react_until = lv_tick_get() + TRICK_REACT_MS;
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
/* Life-stage size, in 1/256 units for LVGL's transform scale. A spiderling is
 * visibly smaller than an adult, so the molt reads as growth rather than a
 * label change. */
static int32_t stage_scale_256(void)
{
    switch (g.stage) {
    case STAGE_SPIDERLING: return 192;  /* 75%  */
    case STAGE_JUVENILE:   return 224;  /* 87.5% */
    case STAGE_ADULT:      return 256;  /* 100% */
    default:               return 256;
    }
}

/* Sickness is shown by dulling the palette, never by making the canvas
 * translucent.
 *
 * The canvas used to get lv_obj_set_style_opa(LV_OPA_60) while sick. Against
 * the pale ground textures that shipped in v1.1.13 the floor showed straight
 * through the body, so a sick spider read as "the spider has gone transparent"
 * rather than "the spider is unwell". Blending 45% toward the pixel's own luma
 * and darkening 12% keeps every pixel fully opaque while still reading as dull
 * and listless.
 *
 * The grey target is the pixel's own luma rather than a fixed grey: a fixed
 * blue-grey increased the channel spread of near-white pixels (the eye whites
 * and glints) instead of reducing it. */
static inline uint32_t sick_aware(uint32_t rgb)
{
    if (!g.sick) return rgb;

    uint32_t r = (rgb >> 16) & 0xFF;
    uint32_t gg = (rgb >> 8) & 0xFF;
    uint32_t b = rgb & 0xFF;

    uint32_t luma = (r * 30 + gg * 59 + b * 11) / 100;

    r  = ((r  * 55 + luma * 45) / 100) * 88 / 100;
    gg = ((gg * 55 + luma * 45) / 100) * 88 / 100;
    b  = ((b  * 55 + luma * 45) / 100) * 88 / 100;

    return (r << 16) | (gg << 8) | b;
}

void cspider_render(uint32_t now_ms)
{
    if (!s_canvas) return;

    float position_x, position_y;
    behaviour_pos(&position_x, &position_y);
    bool moving = gait_update(&s_gait, position_x, position_y, now_ms,
                              (spider_state_t)g.state);

    /* --- squash/stretch ---------------------------------------------
     * The old renderer applied this by moving every part. Here it is two
     * scale factors passed to to_canvas(). */
    float sx = 1.0f, sy = 1.0f;
    if (g.state == SPIDER_POUNCE)      { sx = 1.17f; sy = 0.82f; }
    else if (g.state == SPIDER_AIM)    { sx = 1.11f; sy = 0.84f; }
    else if (g.state == SPIDER_SLEEP)  { sx = 1.05f; sy = 0.95f; }

    /* --- climb lift ---------------------------------------------------
     * Resolved before the pose blocks because the contact shadow depends on it.
     *
     * The lift is the ONLY climb cue. There is no foreshortening, no body tilt
     * and no side-view blend: see the note above CLIMB_FULL_LIFT. */
    int32_t climb = decor_climb_lift(g.spider_x, g.spider_y, now_ms);

    /* --- perch foreshortening ---------------------------------------
     * Sitting on a small prop is drawn from a lower angle, so the body's
     * forward axis compresses. This is the perspective skew that makes the
     * pose read as height; the lift alone just looks like a floating sprite.
     * Eased in and out so entering and leaving the perch is not a pop.
     *
     * This is the PERCH pose only. Climbing deliberately does not use it. */
    {
        static uint32_t s_fs_last_ms;
        float dt = (s_fs_last_ms && now_ms > s_fs_last_ms)
                 ? (float)(now_ms - s_fs_last_ms) / 1000.0f : 0.0f;
        s_fs_last_ms = now_ms;
        if (dt > 0.25f) dt = 0.25f;

        float want = (g.state == SPIDER_PERCH) ? PERCH_FORESHORTEN : 1.0f;

        float k = 1.0f - expf(-dt * 6.0f);
        s_foreshorten += (want - s_foreshorten) * k;
        if (fabsf(s_foreshorten - want) < 0.004f) s_foreshorten = want;
    }

    /* --- petting reaction -------------------------------------------
     * A gentle bounce with a happy wave, drawn on top of whatever the spider
     * is already doing. The emote is forced here rather than set from the pet
     * handler because ui_hud_tick() recomputes the emote from the game state
     * every frame and would overwrite it before it was ever drawn. */
    cspider_emote_t emote = s_emote;
    int32_t lift = 0;
    if (s_pet_reacting) {
        uint32_t elapsed = now_ms - (s_pet_react_until - PET_REACT_MS);
        if (elapsed >= PET_REACT_MS) {
            s_pet_reacting = false;
        } else {
            /* One smooth rise and fall: sin over half a period, so the spider
             * starts and ends on the ground. */
            float t = (float)elapsed / (float)PET_REACT_MS;
            lift = -(int32_t)((float)PET_REACT_PX * sinf(t * PI_F));
            emote = CSP_EMOTE_HAPPY;
        }
    }

    /* --- trick overlay ----------------------------------------------
     * Wave lifts the front pair and bobs; Bow dips the body forward. Spin needs
     * nothing here because behaviour.c turns the heading through a full circle,
     * which the renderer already follows. */
    if (s_trick_reacting) {
        uint32_t elapsed = now_ms - (s_trick_react_until - TRICK_REACT_MS);
        if (elapsed >= TRICK_REACT_MS) {
            s_trick_reacting = false;
        } else {
            float t = (float)elapsed / (float)TRICK_REACT_MS;
            if (s_trick_react_kind == 0) {
                /* Wave: two bobs, front legs raised by the happy emote. */
                lift -= (int32_t)((float)PET_REACT_PX * 0.6f * fabsf(sinf(t * 2.0f * PI_F)));
                emote = CSP_EMOTE_HAPPY;
            } else if (s_trick_react_kind == 2) {
                /* Bow: a forward dip that eases in and out. */
                sy *= 1.0f - 0.18f * sinf(t * PI_F);
                sx *= 1.0f + 0.10f * sinf(t * PI_F);
                emote = CSP_EMOTE_HAPPY;
            }
        }
    }

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

    /* Dull the whole palette while sick. Applied after the ramp is built so the
     * gradient stays intact and only its hue and brightness shift. */
    col_base = sick_aware(col_base);
    col_mid  = sick_aware(col_mid);
    col_hi   = sick_aware(col_hi);
    col_fuzz = sick_aware(col_fuzz);
    col_pup  = sick_aware(col_pup);
    col_leg  = sick_aware(col_leg);
    col_fang = sick_aware(col_fang);

    bool asleep = (g.state == SPIDER_SLEEP);

    /* Breathing. A real spider's abdomen pumps; the cephalothorax does not.
     * Scaling only the abdomen keeps the legs and eyes anchored, so the body
     * does not appear to slide. Frozen while asleep -- a sleeping spider is
     * still, and a breathing sleeper reads as a rendering bug. */
    float breath = asleep ? 1.0f : ambient_breath();
    int32_t abd_r = (int32_t)lroundf((float)ABD_R * breath);
    if (abd_r < ABD_R - 1) abd_r = ABD_R - 1;
    if (abd_r > ABD_R + 1) abd_r = ABD_R + 1;

    /* --- clear ------------------------------------------------------
     * Zero alpha is transparent, so the tank shows through everywhere the
     * spider is not drawn. The colour plane does not need clearing because
     * nothing reads it where alpha is zero. */
    memset(px_alpha(), 0, CANVAS_W * CANVAS_H);

    /* Raise every body part inside the canvas. The canvas itself stays anchored
     * at the spider's floor position, so the shadow below can be drawn at the
     * true ground point and the body visibly rises away from it. */
    s_draw_dy = -climb;

    int32_t x, y;

    /* --- contact shadow ---------------------------------------------
     * Drawn FIRST, at the feet, so the legs and body paint over its middle and
     * only the feathered rim shows. That rim is what grounds the spider.
     *
     * The shadow FOLLOWS the body while climbing. It used to stay pinned to the
     * logical floor point, which put it up to 84 px below the spider at full
     * lift -- nearly a whole sprite height -- and read as the spider floating
     * above the structure rather than standing on it. A contact shadow belongs
     * where the object touches its surface, and while climbing that surface is
     * the top of the structure, directly under the feet. The height cue comes
     * from the spider's screen position over the structure artwork, not from a
     * gap to a shadow left behind on the floor.
     *
     * The ellipse is WIDER than the leg span so the feet do not cover it
     * completely. */
    fill_shadow(CANVAS_CX, CANVAS_CY + SHADOW_DROP + s_draw_dy, 34, 12, 150);

    /* --- legs -------------------------------------------------------
     * Drawn first so the body covers the hips. The far side is drawn first so
     * the body paints over it. */
    for (int pass = 0; pass < 2; pass++) {
        int side = (pass == 0) ? -1 : 1;
        for (int k = 0; k < 4; k++) {
            leg_pose_t pose = leg_pose(k, side, s_gait.phase, moving, emote,
                                       now_ms);
            int32_t hx, hy, kx, ky, fx, fy;
            to_canvas(pose.hf, pose.hr, heading, 1, 1, &hx, &hy);
            to_canvas(pose.kf, pose.kr, heading, 1, 1, &kx, &ky);
            to_canvas(pose.ff, pose.fr, heading, 1, 1, &fx, &fy);
            draw_line(hx, hy, kx, ky, LEG_WIDTH, col_leg);
            draw_line(kx, ky, fx, fy, LEG_WIDTH, col_leg);
        }
    }

    /* --- abdomen ---------------------------------------------------- */
    to_canvas(ABD_FWD, 0.0f, heading, sx, sy, &x, &y);
    fill_circle(x, y, abd_r, col_base);

    to_canvas(ABD_FWD - 1.5f, -1.5f, heading, sx, sy, &x, &y);
    fill_circle(x, y, abd_r - 3, col_mid);

    to_canvas(ABD_FWD - 3.0f, -3.0f, heading, sx, sy, &x, &y);
    fill_circle(x, y, abd_r - 7, col_hi);

    /* Fuzzy rim. */
    for (int i = 0; i < FUZZ_COUNT; i++) {
        float a = (float)i * 2.0f * PI_F / (float)FUZZ_COUNT;
        int32_t fr = FUZZ_R - (i % 2);
        to_canvas(ABD_FWD + cosf(a) * (float)(abd_r + FUZZ_OUT),
                  sinf(a) * (float)(abd_r + FUZZ_OUT),
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
    const uint32_t col_sec = sick_aware(0x6b7280);
    to_canvas(CEPH_FWD + EYE_MID_FWD, -EYE_MID_DX, heading, sx, sy, &x, &y);
    fill_circle(x, y, EYE_SMALL_R, col_sec);
    to_canvas(CEPH_FWD + EYE_MID_FWD, +EYE_MID_DX, heading, sx, sy, &x, &y);
    fill_circle(x, y, EYE_SMALL_R, col_sec);
    to_canvas(CEPH_FWD + EYE_REAR_FWD, -EYE_REAR_DX, heading, sx, sy, &x, &y);
    fill_circle(x, y, EYE_SMALL_R, col_sec);
    to_canvas(CEPH_FWD + EYE_REAR_FWD, +EYE_REAR_DX, heading, sx, sy, &x, &y);
    fill_circle(x, y, EYE_SMALL_R, col_sec);

    /* --- primary eyes -----------------------------------------------
     * Asleep: the eyes go black, so the spider reads as resting rather than
     * staring. Same rule as the widget renderer.
     *
     * Awake: the eyes blink and glance. Both come from ambient.c, which runs on
     * this same task, so there is no extra state to keep in sync. A blink is
     * drawn as a closed lid rather than a shrunken circle -- a shrinking circle
     * reads as the eye receding, a lid reads as a blink. */
    uint32_t col_eye = asleep ? 0x000000 : sick_aware(0xf7f9ff);
    float blink = asleep ? 0.0f : ambient_blink();
    bool lids_shut = blink > 0.55f;
    float glance = asleep ? 0.0f : ambient_glance();

    if (lids_shut) {
        /* Closed lid: a short dark bar where the eye was. */
        for (int s = 0; s < 2; s++) {
            float side = (s == 0) ? -1.0f : 1.0f;
            int32_t lx0, ly0, lx1, ly1;
            to_canvas(CEPH_FWD + EYE_FWD, side * EYE_DX - EYE_R * 0.8f,
                      heading, sx, sy, &lx0, &ly0);
            to_canvas(CEPH_FWD + EYE_FWD, side * EYE_DX + EYE_R * 0.8f,
                      heading, sx, sy, &lx1, &ly1);
            draw_line(lx0, ly0, lx1, ly1, 2, sick_aware(0x2a2233));
        }
    } else {
        to_canvas(CEPH_FWD + EYE_FWD, -EYE_DX, heading, sx, sy, &x, &y);
        fill_circle(x, y, EYE_R, col_eye);
        to_canvas(CEPH_FWD + EYE_FWD, +EYE_DX, heading, sx, sy, &x, &y);
        fill_circle(x, y, EYE_R, col_eye);
    }

    /* --- pupils and glints ------------------------------------------ */
    if (!asleep && !lids_shut) {
        /* The glance shifts the pupils across the eye, not the whole head. */
        float gx = glance * 2.0f;
        to_canvas(CEPH_FWD + EYE_FWD + 1.0f, -EYE_DX + gx, heading, sx, sy, &x, &y);
        fill_circle(x, y, PUPIL_R, col_pup);
        to_canvas(CEPH_FWD + EYE_FWD + 1.0f, +EYE_DX + gx, heading, sx, sy, &x, &y);
        fill_circle(x, y, PUPIL_R, col_pup);

        to_canvas(CEPH_FWD + EYE_FWD + 2.0f, -EYE_DX - 2.0f + gx, heading, sx, sy, &x, &y);
        fill_circle(x, y, 1, sick_aware(0xffffff));
        to_canvas(CEPH_FWD + EYE_FWD + 2.0f, +EYE_DX - 2.0f + gx, heading, sx, sy, &x, &y);
        fill_circle(x, y, 1, sick_aware(0xffffff));
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
                      side * FANG_DX - FANG_HALF_W, heading, sx, sy, &bx0, &by0);
            to_canvas(CEPH_FWD + FANG_FWD - FANG_LEN * 0.5f,
                      side * FANG_DX + FANG_HALF_W, heading, sx, sy, &bx1, &by1);
            to_canvas(CEPH_FWD + FANG_FWD + FANG_LEN * 0.5f,
                      side * FANG_DX, heading, sx, sy, &ax, &ay);

            fill_triangle(bx0, by0, bx1, by1, ax, ay, col_fang);
        }
    }

    /* --- climbing (2.5D) ---------------------------------------------
     * A climbable item raises the spider's DRAW position only. Its logical
     * position stays on the floor, so steering, hunting and hit-testing are
     * untouched; the lift is what makes the enclosure read as having height.
     *
     * The body was already offset upward by `climb` via s_draw_dy, so the
     * canvas itself stays anchored at the floor. That is what lets the contact
     * shadow sit at the true ground point with the body visibly above it.
     *
     * The ramp lives in decor.c so stepping on and off is a short climb rather
     * than a teleport. */
    /* --- position ---------------------------------------------------
     * Centre the canvas on the spider, then scale it by life stage so a
     * spiderling reads as small and an adult as full size. The canvas is a
     * fixed 96x96 buffer; scaling the widget keeps the pixel art crisp and
     * costs nothing per frame.
     *
     * Every setter below invalidates the object, and the canvas is 96x96 -- so
     * a redundant call is not free, it is a full re-blend of the sprite. The
     * pivot, the scale and the opacity are constant for a given life stage, so
     * they are only pushed when they actually change. That was five wasted
     * invalidations per frame. */
    static int32_t s_last_x = 0x7fffffff, s_last_y = 0x7fffffff;
    static int32_t s_last_scale = -1;
    static bool s_last_opa_cover;
    int32_t px = g.spider_x - CANVAS_CX;
    int32_t py = g.spider_y - CANVAS_CY + lift;
    if (px != s_last_x || py != s_last_y) {
        s_last_x = px;
        s_last_y = py;
        lv_obj_set_pos(s_canvas, px, py);
    }
    int32_t scale = stage_scale_256();
    if (scale != s_last_scale) {
        s_last_scale = scale;
        if (scale == 256) {
            /* Identity scale: REMOVE the transform rather than setting it to
             * 256. LVGL takes a different, much slower path for any object with
             * a transform set -- it cannot use the plain blit -- and an adult
             * spider is the common case. Clearing the transform lets the canvas
             * blit directly, which is the difference between a full-screen
             * software transform every frame and a 96x96 copy. */
            lv_obj_remove_local_style_prop(s_canvas, LV_STYLE_TRANSFORM_SCALE_X, 0);
            lv_obj_remove_local_style_prop(s_canvas, LV_STYLE_TRANSFORM_SCALE_Y, 0);
            lv_obj_remove_local_style_prop(s_canvas, LV_STYLE_TRANSFORM_PIVOT_X, 0);
            lv_obj_remove_local_style_prop(s_canvas, LV_STYLE_TRANSFORM_PIVOT_Y, 0);
        } else {
            lv_obj_set_style_transform_pivot_x(s_canvas, CANVAS_CX, 0);
            lv_obj_set_style_transform_pivot_y(s_canvas, CANVAS_CY, 0);
            lv_obj_set_style_transform_scale_x(s_canvas, scale, 0);
            lv_obj_set_style_transform_scale_y(s_canvas, scale, 0);
        }
    }
    /* The canvas is always fully opaque. Sickness is expressed by the palette
     * (see sick_aware), not by widget opacity: a translucent spider let the
     * pale ground textures show through and read as a rendering bug. */
    if (!s_last_opa_cover) {
        s_last_opa_cover = true;
        lv_obj_set_style_opa(s_canvas, LV_OPA_COVER, 0);
    }

    /* Tell LVGL the buffer and position changed. There is no layer to finish:
     * the pixels were written directly, so this is just an invalidate. It runs
     * last so the new position is included in the redraw. */
    lv_obj_invalidate(s_canvas);
}
