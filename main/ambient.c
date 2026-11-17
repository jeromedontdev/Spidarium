/*
 * Spidarium — ambient life, implementation.
 *
 * Two effects, both free of new art:
 *
 *  1. Dust motes. Eight small squares drifting slowly upward with a gentle
 *     sideways sway, tinted from the theme's own `dust` colour. They wrap at the
 *     top of the tank. This is the single cheapest way to make a still image
 *     read as a place with air in it.
 *
 *  2. The spider's idle life. A breathing scale, a blink, and a glance. These
 *     are sampled by the renderer, so the creature is never perfectly still
 *     even when the FSM has it in IDLE.
 *
 * Everything is created once and moved. Nothing is allocated per frame.
 */
#include "ambient.h"

#include "game.h"
#include "game_config.h"
#include "theme.h"
#include "ui_hud.h"

#include "esp_log.h"
#include "esp_random.h"
#include "esp_heap_caps.h"

#include <math.h>
#include <stdio.h>

static const char *TAG = "spidarium.ambient";

#define PI_F 3.14159265f

typedef struct {
    lv_obj_t *obj;
    float x, y;        /* sub-pixel position, so the drift is smooth */
    float vy;          /* upward speed, px/s */
    float sway_phase;  /* radians */
    float sway_rate;   /* radians/s */
    float sway_amp;    /* px */
    uint8_t size;
} mote_t;

static mote_t s_motes[AMBIENT_MOTES];
static bool s_visible = true;
static bool s_ready;

/* Idle-life state. */
static float s_breath_phase;
static float s_blink_phase;
static uint32_t s_next_blink;
static uint32_t s_next_glance;
static uint32_t s_glance_until;
static float s_glance_dir;

/* A mote is a 2..4 px square. Anything larger reads as debris rather than air. */
static uint8_t mote_size(void)
{
    return (uint8_t)(2 + esp_random() % 3);
}

static void mote_reset(mote_t *m, bool anywhere)
{
    m->size = mote_size();
    m->x = (float)(g_play.left + (int32_t)(esp_random() % (uint32_t)(g_play.right - g_play.left)));
    /* `anywhere` seeds the first frame across the whole tank; a wrap always
     * restarts at the bottom so the field does not thin out over time. */
    m->y = anywhere
        ? (float)(g_play.top + (int32_t)(esp_random() % (uint32_t)(g_play.bottom - g_play.top)))
        : (float)g_play.bottom;
    m->vy = -(4.0f + (float)(esp_random() % 100) / 100.0f * 8.0f);
    m->sway_phase = (float)(esp_random() % 628) / 100.0f;
    m->sway_rate = 0.4f + (float)(esp_random() % 100) / 100.0f * 0.8f;
    m->sway_amp = 2.0f + (float)(esp_random() % 100) / 100.0f * 4.0f;
}

void ambient_init(void)
{
    if (s_ready) return;
    for (int i = 0; i < AMBIENT_MOTES; i++) {
        mote_t *m = &s_motes[i];
        m->obj = lv_obj_create(ui.bg_canvas ? lv_obj_get_parent(ui.bg_canvas) : NULL);
        if (!m->obj) {
            ESP_LOGW(TAG, "mote %d could not be created", i);
            continue;
        }
        lv_obj_remove_style_all(m->obj);
        lv_obj_set_style_bg_opa(m->obj, LV_OPA_40, 0);
        lv_obj_set_style_radius(m->obj, 0, 0);
        lv_obj_set_clickable(m->obj, false);
        lv_obj_set_scrollable(m->obj, false);
        mote_reset(m, true);
        lv_obj_set_size(m->obj, m->size, m->size);
    }
    s_ready = true;
    ambient_retint();
    ESP_LOGI(TAG, "ambient life ready: %d motes", AMBIENT_MOTES);
}

void ambient_retint(void)
{
    const theme_def_t *t = theme_current();
    if (!t) return;
    /* `dust` is the theme's fixed highlight colour. It was defined in the theme
     * table and never used anywhere, so the motes cost no new palette work. */
    for (int i = 0; i < AMBIENT_MOTES; i++) {
        if (!s_motes[i].obj) continue;
        lv_obj_set_style_bg_color(s_motes[i].obj, lv_color_hex(t->dust), 0);
    }
}

void ambient_set_visible(bool visible)
{
    if (s_visible == visible) return;
    s_visible = visible;
    for (int i = 0; i < AMBIENT_MOTES; i++)
        if (s_motes[i].obj) lv_obj_set_hidden(s_motes[i].obj, !visible);
}

/* --- light shafts --------------------------------------------------------
 * Baked into the background canvas rather than drawn as LVGL objects. See the
 * comment in ambient.h for why: rotated objects were software rendered at
 * ~111,000 blended pixels each per frame and made the tank choppy, and
 * unrotated ones read as blocky vertical stripes going one way.
 *
 * Each beam is a diagonal band whose centreline wobbles, whose width tapers,
 * and whose brightness is modulated along its length. Those four things
 * together are what stop it reading as a rectangle: no two beams share a slope,
 * a width, a taper direction or a wobble rate, so the light never looks
 * symmetrical.
 */
typedef struct {
    float top_x;      /* where the centreline crosses the top edge */
    float slope;      /* px of horizontal travel per px of descent */
    float top_half;   /* half width at the top */
    float bot_half;   /* half width at the bottom */
    float peak;       /* peak alpha, 0..1 */
    float wobble_amp; /* px the centreline wanders */
    float wobble_rate;/* radians per px of descent */
    float wobble_phase;
    float pulse_rate; /* radians per px, brightness banding along the beam */
    float pulse_phase;
} shaft_t;

/* Deliberately irregular. Beam 0 leans right and widens, beam 1 leans left and
 * narrows, beam 2 leans right and is the narrowest and faintest. The slopes have
 * opposite signs so the light does not all travel the same way.
 *
 * The peaks started at 0.15/0.11/0.09 and were invisible on the panel: the
 * vertical fade and the canopy banding both cut into them, so the brightest
 * point on screen was under 6% and read as a smudge rather than a beam. These
 * are roughly double, which is the point where the light is unmistakable
 * without washing the art out. */
static const shaft_t k_shafts[AMBIENT_SHAFTS] = {
    {  92.0f,  0.42f, 23.0f, 39.0f, 0.340f, 14.0f, 0.0110f, 0.0f, 0.0200f, 0.0f },
    { 252.0f, -0.30f, 35.0f, 20.0f, 0.260f, 20.0f, 0.0080f, 2.1f, 0.0155f, 1.3f },
    { 404.0f,  0.22f, 15.0f, 31.0f, 0.220f, 11.0f, 0.0140f, 4.3f, 0.0245f, 2.7f },
};

/* Beam colour per day period. These are light, not tint: they are added over the
 * art rather than washing it out, so they can be brighter than the tint overlay
 * without greying the tank. */
static const uint32_t k_shaft_colours[4] = {
    0xffe6b8,   /* morning: warm gold */
    0xfff4e0,   /* afternoon: pale warm white */
    0xffb066,   /* evening: amber */
    0x9fb8e8,   /* night: cool moonlight */
};

static uint8_t s_shaft_period = 255;
/* Set by the console so a forced period survives the once-a-second clock tick
 * that would otherwise put it straight back. */
static bool s_shaft_forced;

void ambient_shafts_set_period(uint8_t period)
{
    s_shaft_period = period > 3 ? 255 : period;
}

void ambient_shafts_force(uint8_t period)
{
    s_shaft_forced = period <= 3;
    ambient_shafts_set_period(period);
}

bool ambient_shafts_forced(void)
{
    return s_shaft_forced;
}

uint8_t ambient_shafts_period(void)
{
    return s_shaft_period;
}

/* Blend one beam into an RGB565 buffer. Only the beam's own bounding box is
 * visited, so the cost is proportional to the lit area rather than the screen. */
static float shaft_alpha_at(const shaft_t *s, int32_t x, int32_t y, int32_t h)
{
    float t = (float)y / (float)h;
    float centre = s->top_x + s->slope * (float)y
                 + sinf((float)y * s->wobble_rate + s->wobble_phase) * s->wobble_amp;
    float half = s->top_half + (s->bot_half - s->top_half) * t;
    if (half < 1.0f) return 0.0f;

    float d = fabsf((float)x - centre) / half;
    if (d >= 1.0f) return 0.0f;

    /* Light falls off as it travels down, and bands where foliage blocks it.
     * The banding is what makes it read as light through a canopy instead of
     * a painted stripe. */
    float vfade = 1.0f - 0.55f * t;
    float pulse = 1.0f + 0.20f * sinf((float)y * s->pulse_rate + s->pulse_phase);
    /* Squared smoothstep: soft at the edge, flat in the middle. A linear ramp
     * still shows a visible seam where the beam ends. */
    float f = 1.0f - d * d;
    f *= f;
    return s->peak * vfade * pulse * f;
}

static void shaft_bake_one(uint16_t *dst, int32_t w, int32_t h,
                           const shaft_t *s, uint8_t lr, uint8_t lg, uint8_t lb)
{
    for (int32_t y = 0; y < h; y++) {
        float t = (float)y / (float)h;
        float centre = s->top_x + s->slope * (float)y
                     + sinf((float)y * s->wobble_rate + s->wobble_phase) * s->wobble_amp;
        float half = s->top_half + (s->bot_half - s->top_half) * t;
        if (half < 1.0f) continue;

        int32_t x0 = (int32_t)(centre - half);
        int32_t x1 = (int32_t)(centre + half);
        if (x0 < 0) x0 = 0;
        if (x1 > w - 1) x1 = w - 1;

        for (int32_t x = x0; x <= x1; x++) {
            int32_t a = (int32_t)(shaft_alpha_at(s, x, y, h) * 255.0f);
            if (a <= 0) continue;
            if (a > 255) a = 255;

            uint16_t px = dst[y * w + x];
            int32_t r = (px >> 11) & 0x1f, g = (px >> 5) & 0x3f, b = px & 0x1f;
            r = (r << 3) | (r >> 2);
            g = (g << 2) | (g >> 4);
            b = (b << 3) | (b >> 2);
            int32_t ia = 255 - a;
            r = (r * ia + (int32_t)lr * a) / 255;
            g = (g * ia + (int32_t)lg * a) / 255;
            b = (b * ia + (int32_t)lb * a) / 255;
            dst[y * w + x] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
}

void ambient_shafts_bake(uint16_t *dst, int32_t w, int32_t h)
{
    if (!dst || w <= 0 || h <= 0) return;
    if (s_shaft_period > 3) return;
    uint32_t c = k_shaft_colours[s_shaft_period];
    uint8_t lr = (uint8_t)((c >> 16) & 0xff);
    uint8_t lg = (uint8_t)((c >> 8) & 0xff);
    uint8_t lb = (uint8_t)(c & 0xff);
    for (int i = 0; i < AMBIENT_SHAFTS; i++)
        shaft_bake_one(dst, w, h, &k_shafts[i], lr, lg, lb);
}

bool ambient_shafts_lit_at(int32_t x, int32_t y, int32_t h)
{
    if (s_shaft_period > 3) return false;
    for (int i = 0; i < AMBIENT_SHAFTS; i++)
        if (shaft_alpha_at(&k_shafts[i], x, y, h) > 0.002f) return true;
    return false;
}

void ambient_tick(uint32_t now_ms)
{
    static uint32_t s_last;
    if (!s_ready) return;
    uint32_t elapsed = s_last ? now_ms - s_last : 0;
    s_last = now_ms;
    /* Clamp so a long stall cannot teleport the motes across the tank. */
    if (elapsed > 250u) elapsed = 250u;
    float dt = (float)elapsed / 1000.0f;

    if (s_visible && dt > 0.0f) {
        for (int i = 0; i < AMBIENT_MOTES; i++) {
            mote_t *m = &s_motes[i];
            if (!m->obj) continue;
            m->y += m->vy * dt;
            m->sway_phase += m->sway_rate * dt;
            if (m->y < (float)g_play.top) mote_reset(m, false);
            float x = m->x + sinf(m->sway_phase) * m->sway_amp;
            lv_obj_set_pos(m->obj, (int32_t)lroundf(x), (int32_t)lroundf(m->y));
        }
    }

    /* Breathing: a slow sine, about one breath every four seconds. */
    s_breath_phase += dt * (2.0f * PI_F / 4.0f);
    if (s_breath_phase > 2.0f * PI_F) s_breath_phase -= 2.0f * PI_F;

    /* Blinking: a short close-open, every few seconds. The window is short so
     * the eyes are shut for a few frames rather than a visible pause. */
    if ((int32_t)(now_ms - s_next_blink) >= 0) {
        s_next_blink = now_ms + 2200u + esp_random() % 3800u;
        s_blink_phase = 1.0f;
    }
    if (s_blink_phase > 0.0f) {
        s_blink_phase -= dt * 7.0f;
        if (s_blink_phase < 0.0f) s_blink_phase = 0.0f;
    }

    /* Glancing: the eyes drift to one side and back. This is what makes an
     * otherwise still spider read as watching rather than switched off. */
    if ((int32_t)(now_ms - s_next_glance) >= 0) {
        s_next_glance = now_ms + 3000u + esp_random() % 5000u;
        s_glance_until = now_ms + 900u + esp_random() % 900u;
        s_glance_dir = (esp_random() & 1u) ? 1.0f : -1.0f;
    }
    if ((int32_t)(now_ms - s_glance_until) >= 0) s_glance_dir = 0.0f;

}

float ambient_breath(void)
{
    /* 0.92..1.08. The abdomen is 13 px across, so a 3% breath is a third of a
     * pixel and rounds away entirely -- the effect was invisible until the
     * amplitude was raised to the smallest value that actually moves a pixel.
     * More than this reads as a pulse rather than breathing. */
    return 1.0f + sinf(s_breath_phase) * 0.08f;
}

float ambient_blink(void)
{
    return s_blink_phase;
}

float ambient_glance(void)
{
    return s_glance_dir;
}

uint8_t ambient_light_opa(void)
{
    /* A slow drift around the historical 110, plus or minus 8. Small enough
     * that it never reads as a flicker, large enough that a still frame taken
     * a few seconds apart is visibly different. */
    float drift = sinf(s_breath_phase * 0.5f) * 8.0f;
    int v = 110 + (int)lroundf(drift);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (uint8_t)v;
}

void ambient_report(void)
{
    const theme_def_t *t = theme_current();
    int inside = 0;
    for (int i = 0; i < AMBIENT_MOTES; i++) {
        const mote_t *m = &s_motes[i];
        if (!m->obj) continue;
        if (m->y >= (float)g_play.top && m->y <= (float)g_play.bottom) inside++;
    }
    printf("AMBIENT motes=%d inside=%d visible=%d dust=0x%06lx breath=%.3f "
           "blink=%.2f glance=%.0f light=%u shafts=%d period=%d\n",
           AMBIENT_MOTES, inside, s_visible ? 1 : 0,
           (unsigned long)(t ? t->dust : 0),
           (double)ambient_breath(), (double)ambient_blink(),
           (double)ambient_glance(), (unsigned)ambient_light_opa(),
           AMBIENT_SHAFTS, (int)s_shaft_period);
}

bool ambient_test(void)
{
    bool ok = s_ready;

    /* Every mote must exist and sit inside the playfield. A mote outside the
     * tank is invisible work, and a mote at a negative position would be drawn
     * over the border. */
    for (int i = 0; i < AMBIENT_MOTES; i++) {
        const mote_t *m = &s_motes[i];
        if (!m->obj) { ok = false; break; }
        if (m->size < 2 || m->size > 4) { ok = false; break; }
        if (m->y < (float)g_play.top - 1.0f || m->y > (float)g_play.bottom + 1.0f) {
            ok = false; break;
        }
    }

    /* The idle life must stay in a range that reads as alive rather than as a
     * glitch: a breath that actually moves a pixel, a blink between shut and
     * open, and a glance that is one of three values. */
    float b = ambient_breath();
    ok = ok && b >= 0.91f && b <= 1.09f;
    float k = ambient_blink();
    ok = ok && k >= 0.0f && k <= 1.0f;
    float gl = ambient_glance();
    ok = ok && (gl == 0.0f || gl == 1.0f || gl == -1.0f);

    /* The enclosure light must drift, but only a little. A fixed value is the
     * bug this replaced; a large swing would read as a flicker. */
    uint8_t lo = 255, hi = 0;
    for (int i = 0; i < 400; i++) {
        uint8_t v = ambient_light_opa();
        if (v < lo) lo = v;
        if (v > hi) hi = v;
        s_breath_phase += 0.05f;
    }
    ok = ok && hi > lo;                 /* it actually drifts */
    ok = ok && (hi - lo) <= 20;         /* but never flickers */

    /* The motes must actually move. A field that never advances is the failure
     * mode that looks like it works in a screenshot. */
    float before = s_motes[0].y;
    uint32_t now = lv_tick_get();
    ambient_tick(now + 100u);
    ok = ok && s_motes[0].y != before;

    /* The light shafts. The three complaints were "blocky", "symmetrical" and
     * "only go down the screen in a single direction", so each is asserted
     * directly rather than trusted to the constants. */
    {
        uint8_t saved = s_shaft_period;
        ambient_shafts_set_period(1);

        /* Diagonal: every beam's centreline must move sideways as it descends.
         * A vertical stripe is the bug. */
        for (int i = 0; i < AMBIENT_SHAFTS; i++) {
            const shaft_t *s = &k_shafts[i];
            float top = s->top_x + sinf(s->wobble_phase) * s->wobble_amp;
            float bot = s->top_x + s->slope * 480.0f
                      + sinf(480.0f * s->wobble_rate + s->wobble_phase) * s->wobble_amp;
            ok = ok && fabsf(bot - top) > 40.0f;
        }

        /* Not all one direction: at least one beam must lean each way. */
        bool any_right = false, any_left = false;
        for (int i = 0; i < AMBIENT_SHAFTS; i++) {
            if (k_shafts[i].slope > 0.05f) any_right = true;
            if (k_shafts[i].slope < -0.05f) any_left = true;
        }
        ok = ok && any_right && any_left;

        /* Not symmetrical: no two beams may share a width or a peak. */
        for (int i = 0; i < AMBIENT_SHAFTS; i++)
            for (int j = i + 1; j < AMBIENT_SHAFTS; j++) {
                ok = ok && fabsf(k_shafts[i].top_half - k_shafts[j].top_half) > 2.0f;
                ok = ok && fabsf(k_shafts[i].peak - k_shafts[j].peak) > 0.01f;
            }

        /* Feathered, not blocky: the alpha must ramp across the beam rather than
         * step. Sample a row and require several distinct intermediate values. */
        int steps = 0;
        for (int32_t x = 0; x < 480; x++) {
            float a = shaft_alpha_at(&k_shafts[0], x, 240, 480);
            if (a > 0.01f && a < k_shafts[0].peak * 0.9f) steps++;
        }
        ok = ok && steps > 10;

        /* And it must actually reach the pixels. Bake into a transient PSRAM
         * buffer -- 480x64 is enough for every beam to cross it, and PSRAM keeps
         * the test from eating internal RAM. */
        uint16_t *probe = heap_caps_malloc(480 * 64 * sizeof(uint16_t),
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!probe) {
            ok = false;
        } else {
            for (int i = 0; i < 480 * 64; i++) probe[i] = 0x0000;
            ambient_shafts_bake(probe, 480, 64);
            int changed = 0;
            uint16_t first = 0;
            bool varied = false;
            for (int i = 0; i < 480 * 64; i++) {
                if (probe[i] != 0x0000) {
                    if (!changed) first = probe[i];
                    else if (probe[i] != first) varied = true;
                    changed++;
                }
            }
            ok = ok && changed > 0 && varied;

            /* No beams at all must leave the buffer untouched. */
            ambient_shafts_set_period(255);
            for (int i = 0; i < 480 * 64; i++) probe[i] = 0x0000;
            ambient_shafts_bake(probe, 480, 64);
            for (int i = 0; i < 480 * 64; i++) ok = ok && probe[i] == 0x0000;
            heap_caps_free(probe);
        }

        ambient_shafts_set_period(saved);
    }

    return ok;
}
