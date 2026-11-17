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
    ambient_shafts_init();
    ESP_LOGI(TAG, "ambient life ready: %d motes, %d shafts", AMBIENT_MOTES, AMBIENT_SHAFTS);
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

/* --- light shafts -------------------------------------------------------
 *
 * Two wide, very transparent bands drifting diagonally across the tank. The eye
 * reads moving light as depth even when nothing else moves, which is what makes
 * a static background read as a lit space rather than a picture.
 *
 * Each shaft is a tall thin rectangle rotated by LVGL. Rotation is a transform,
 * so it costs no extra objects and no per-frame allocation -- the object is
 * created once and only its position changes. */
typedef struct {
    lv_obj_t *obj;
    float phase;      /* 0..1 position across the sweep */
    float speed;      /* sweeps per second */
    int32_t width;
} shaft_t;

static shaft_t s_shafts[AMBIENT_SHAFTS];
static uint8_t s_shaft_period = 255;
static bool s_shafts_ready;

/* Per-period tint and strength. Morning and evening are warm and visible;
 * afternoon is neutral and faint; night is cool and dim, because a lit window
 * at night should not look like noon. */
static const struct { uint32_t colour; uint8_t opa; } k_shaft_tints[4] = {
    { 0xffe0b0, 26 },   /* morning */
    { 0xfff4e0, 14 },   /* afternoon */
    { 0xffb070, 30 },   /* evening */
    { 0x8fa8e0, 18 },   /* night */
};

void ambient_set_visible(bool visible)
{
    if (s_visible == visible) return;
    s_visible = visible;
    for (int i = 0; i < AMBIENT_MOTES; i++)
        if (s_motes[i].obj) lv_obj_set_hidden(s_motes[i].obj, !visible);
    for (int i = 0; i < AMBIENT_SHAFTS; i++)
        if (s_shafts[i].obj) lv_obj_set_hidden(s_shafts[i].obj, !visible);
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

    /* Light shafts sweep slowly across the tank. The sweep is wide enough that
     * a shaft is off-screen for part of its cycle, which is what makes the
     * light feel like it is coming from somewhere rather than sliding back and
     * forth. */
    if (s_visible && dt > 0.0f) {
        for (int i = 0; i < AMBIENT_SHAFTS; i++) {
            shaft_t *s = &s_shafts[i];
            if (!s->obj) continue;
            s->phase += s->speed * dt;
            if (s->phase > 1.0f) s->phase -= 1.0f;
            /* Travel from fully off the left edge to fully off the right. */
            int32_t span = ui.W + s->width * 2;
            int32_t x = (int32_t)(s->phase * (float)span) - s->width;
            lv_obj_set_pos(s->obj, x, -ui.H / 2);
        }
    }
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

void ambient_shafts_init(void)
{
    if (s_shafts_ready) return;
    lv_obj_t *parent = ui.bg_canvas ? lv_obj_get_parent(ui.bg_canvas) : NULL;
    for (int i = 0; i < AMBIENT_SHAFTS; i++) {
        shaft_t *s = &s_shafts[i];
        s->obj = lv_obj_create(parent);
        if (!s->obj) {
            ESP_LOGW(TAG, "shaft %d could not be created", i);
            continue;
        }
        lv_obj_remove_style_all(s->obj);
        s->width = 70 + i * 46;
        lv_obj_set_size(s->obj, s->width, ui.H * 2);
        lv_obj_set_style_radius(s->obj, 0, 0);
        lv_obj_set_style_bg_opa(s->obj, LV_OPA_TRANSP, 0);
        lv_obj_set_clickable(s->obj, false);
        lv_obj_set_scrollable(s->obj, false);
        /* A slight lean, so the two shafts are not parallel. */
        lv_obj_set_style_transform_rotation(s->obj, i ? 140 : 200, 0);
        s->phase = i ? 0.55f : 0.15f;
        s->speed = i ? 0.011f : 0.017f;
    }
    s_shafts_ready = true;
    ambient_shafts_set_period(255);
}

void ambient_shafts_set_period(uint8_t period)
{
    if (period == s_shaft_period) return;
    s_shaft_period = period;
    /* No clock, or a period we do not know: use the neutral afternoon tint
     * rather than switching the effect off, so a tank without an RTC still has
     * light in it. */
    unsigned idx = period < 4 ? period : 1;
    for (int i = 0; i < AMBIENT_SHAFTS; i++) {
        if (!s_shafts[i].obj) continue;
        lv_obj_set_style_bg_color(s_shafts[i].obj, lv_color_hex(k_shaft_tints[idx].colour), 0);
        lv_obj_set_style_bg_opa(s_shafts[i].obj, k_shaft_tints[idx].opa, 0);
    }
}

void ambient_shafts_report(void)
{
    int live = 0;
    for (int i = 0; i < AMBIENT_SHAFTS; i++) if (s_shafts[i].obj) live++;
    printf("SHAFTS count=%d live=%d period=%u opa=%u\n",
           AMBIENT_SHAFTS, live, (unsigned)s_shaft_period,
           (unsigned)(s_shaft_period < 4 ? k_shaft_tints[s_shaft_period].opa
                                         : k_shaft_tints[1].opa));
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
           "blink=%.2f glance=%.0f light=%u\n",
           AMBIENT_MOTES, inside, s_visible ? 1 : 0,
           (unsigned long)(t ? t->dust : 0),
           (double)ambient_breath(), (double)ambient_blink(),
           (double)ambient_glance(), (unsigned)ambient_light_opa());
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

    return ok;
}
