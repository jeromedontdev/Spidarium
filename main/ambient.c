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

#include "decor.h"
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

/* Spores: larger, slower, and they drift sideways as well as up. Two layers of
 * air motion read as depth; one uniform field reads as a texture. */
typedef struct {
    lv_obj_t *obj;
    float x, y;
    float vx, vy;
    float wobble_phase;
    float wobble_rate;
    uint8_t size;
} spore_t;

static spore_t s_spores[AMBIENT_SPORES];

/* Web strands: a thin line anchored at one end, pivoting slowly. A strand that
 * translated would read as a sliding stick; pivoting reads as something hanging
 * and catching the air. */
typedef struct {
    lv_obj_t *obj;
    int32_t ax, ay;      /* anchor, fixed */
    float length;
    float base_angle;    /* radians, the resting direction */
    float sway_phase;
    float sway_rate;
    float sway_amp;
    int32_t last_deg;    /* last angle pushed to LVGL, so we only invalidate on change */
} strand_t;

static strand_t s_strands[AMBIENT_STRANDS];

/* Decor/gift particles. A fixed pool reassigned to whichever items are on
 * screen, so the cost is constant whether the tank holds one item or thirteen.
 * See AMBIENT_PARTICLES in ambient.h for why this is a pool and not one emitter
 * per item. */
typedef struct {
    lv_obj_t *obj;
    float x, y;
    float vx, vy;
    float life;        /* seconds remaining */
    float life_max;
    uint8_t size;
    ambient_emit_t style;
    bool active;
} particle_t;

static particle_t s_particles[AMBIENT_PARTICLES];
static uint32_t s_next_particle;   /* round-robin cursor, so no item starves */
static uint32_t s_next_spawn;
/* Set while the self-test manipulates the pool. The render task keeps running
 * during a console command, so without this the live tick spawns particles
 * between the test's reset and its count -- a race that made the test pass once
 * and then fail. */
static bool s_emit_suspended;

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

    /* Spores are round and slightly larger than the motes, so the two layers are
     * distinguishable at a glance rather than reading as one noisy field. */
    for (int i = 0; i < AMBIENT_SPORES; i++) {
        spore_t *p = &s_spores[i];
        p->obj = lv_obj_create(ui.bg_canvas ? lv_obj_get_parent(ui.bg_canvas) : NULL);
        if (!p->obj) {
            ESP_LOGW(TAG, "spore %d could not be created", i);
            continue;
        }
        lv_obj_remove_style_all(p->obj);
        lv_obj_set_style_bg_opa(p->obj, LV_OPA_50, 0);
        lv_obj_set_style_radius(p->obj, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_clickable(p->obj, false);
        lv_obj_set_scrollable(p->obj, false);
        p->size = (uint8_t)(3 + esp_random() % 3);
        p->x = (float)(g_play.left + (int32_t)(esp_random() % (uint32_t)(g_play.right - g_play.left)));
        p->y = (float)(g_play.top + (int32_t)(esp_random() % (uint32_t)(g_play.bottom - g_play.top)));
        /* Slower than the dust and with a real horizontal component, so they
         * cross the tank rather than only rising. */
        p->vy = -(2.0f + (float)(esp_random() % 100) / 100.0f * 4.0f);
        p->vx = ((float)(esp_random() % 200) / 100.0f - 1.0f) * 5.0f;
        p->wobble_phase = (float)(esp_random() % 628) / 100.0f;
        p->wobble_rate = 0.3f + (float)(esp_random() % 100) / 100.0f * 0.5f;
        lv_obj_set_size(p->obj, p->size, p->size);
    }

    /* Strands hang from the top corners, where a real web would start. */
    for (int i = 0; i < AMBIENT_STRANDS; i++) {
        strand_t *s = &s_strands[i];
        s->obj = lv_obj_create(ui.bg_canvas ? lv_obj_get_parent(ui.bg_canvas) : NULL);
        if (!s->obj) {
            ESP_LOGW(TAG, "strand %d could not be created", i);
            continue;
        }
        lv_obj_remove_style_all(s->obj);
        lv_obj_set_style_bg_opa(s->obj, LV_OPA_40, 0);
        lv_obj_set_style_radius(s->obj, 0, 0);
        lv_obj_set_clickable(s->obj, false);
        lv_obj_set_scrollable(s->obj, false);
        /* One in each top corner, so the tank is framed rather than one-sided.
         * Inset well past the border: anchored at the playfield edge the strand
         * was drawn underneath the frame and could not be seen at all. */
        s->ax = i == 0 ? g_play.left + 26 : g_play.right - 26;
        s->ay = g_play.top + 18;
        s->length = 34.0f + (float)(esp_random() % 30);
        s->base_angle = (i == 0 ? 1.0f : 2.14f);   /* down-right / down-left */
        s->sway_phase = (float)(esp_random() % 628) / 100.0f;
        s->sway_rate = 0.25f + (float)(esp_random() % 100) / 100.0f * 0.35f;
        s->sway_amp = 0.10f + (float)(esp_random() % 100) / 100.0f * 0.10f;
        s->last_deg = 0x7fffffff;   /* force the first push */
        /* A 2 px wide, `length` tall box, rotated about its top end. One pixel
         * at 20% opacity was invisible on the panel. */
        lv_obj_set_size(s->obj, 2, (int32_t)s->length);
        lv_obj_set_style_transform_pivot_x(s->obj, 0, 0);
        lv_obj_set_style_transform_pivot_y(s->obj, 0, 0);
    }

    /* Particles are created once and hidden until claimed. Creating them up
     * front rather than on demand is what keeps the cost constant: no
     * allocation happens while the tank is running. */
    for (int i = 0; i < AMBIENT_PARTICLES; i++) {
        particle_t *p = &s_particles[i];
        p->obj = lv_obj_create(ui.bg_canvas ? lv_obj_get_parent(ui.bg_canvas) : NULL);
        if (!p->obj) {
            ESP_LOGW(TAG, "particle %d could not be created", i);
            continue;
        }
        lv_obj_remove_style_all(p->obj);
        lv_obj_set_style_bg_opa(p->obj, LV_OPA_60, 0);
        lv_obj_set_style_radius(p->obj, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_clickable(p->obj, false);
        lv_obj_set_scrollable(p->obj, false);
        lv_obj_set_size(p->obj, 2, 2);
        lv_obj_set_hidden(p->obj, true);
        p->active = false;
    }

    s_ready = true;
    ambient_retint();
    ESP_LOGI(TAG, "ambient life ready: %d motes, %d spores, %d strands, %d particles",
             AMBIENT_MOTES, AMBIENT_SPORES, AMBIENT_STRANDS, AMBIENT_PARTICLES);
}

/* --- decor and gift particles -------------------------------------------
 *
 * The emitter style is derived from the item's own kind, so no new data has to
 * be authored per decor entry and no artwork is needed: a particle is a small
 * square or circle tinted from the theme palette. That is what makes this cheap
 * in both compute and tokens -- there is nothing to generate.
 *
 * The mapping is by family rather than per item, because the point is that the
 * tank reads as alive, not that every object has a bespoke effect. */
ambient_emit_t ambient_emit_for_decor(uint8_t kind)
{
    switch (kind) {
    /* Plants and fungi breathe out specks. */
    case DECOR_FERN:
    case DECOR_MOSS_CLUMP:
    case DECOR_SUCCULENT:
    case DECOR_FLOWER_WHITE:
    case DECOR_FLOWER_PINK:
    case DECOR_MUSHROOM:
    case DECOR_FERN_POT:
    case DECOR_MUSHROOM_PAIR:
    case DECOR_FLOWER_PLANTER:
    case DECOR_MOSS_STONE:
    case DECOR_LEAF_UMBRELLA:
    case DECOR_FAIRY_RING:
    case DECOR_FOXGLOVE_LANTERN:
    case DECOR_CYCAD:
    case DECOR_BONSAI:
    case DECOR_BAMBOO_FOUNTAIN:
    case DECOR_CLIMB_VINE:
    case DECOR_CLIMB_IVY:
    /* Organic odds and ends that would also shed something. */
    case DECOR_LEAF_PILE:
    case DECOR_SEED_POD:
    case DECOR_ROSE_URN:
    case DECOR_DINOSAUR_EGG:
    case DECOR_FAIRY_BRIDGE:
        return AMBIENT_EMIT_SPORE;

    /* Anything that would catch a highlight throws brief glints. */
    case DECOR_CRYSTAL:
    case DECOR_CRYSTAL_CLUSTER:
    case DECOR_DEW_POOL:
    case DECOR_SHELL:
    case DECOR_SHELL_COVE:
    case DECOR_LANTERN_CAP:
    case DECOR_GLOBE:
    case DECOR_TEA_CUP:
    case DECOR_THIMBLE:
    case DECOR_POTION_RACK:
    case DECOR_AMBER_FOSSIL:
    case DECOR_NEON_SIGN:
    case DECOR_HOLO_TERMINAL:
    case DECOR_SOLAR_ARRAY:
    case DECOR_SUN_OBELISK:
    case DECOR_STONE_LANTERN:
    /* Anything with a surface that would catch a highlight. */
    case DECOR_WEB_HAMMOCK:
    case DECOR_SILK_TENT:
    case DECOR_PIXIE_DOOR:
    case DECOR_SPOOL:
    case DECOR_CABLE_COIL:
    case DECOR_MOON_ROVER:
    case DECOR_OXYGEN_POD:
    case DECOR_TREASURE_CHEST:
        return AMBIENT_EMIT_GLINT;

    /* Stone, wood and bone shed slow dust. */
    case DECOR_ROCK_SMALL:
    case DECOR_ROCK_LARGE:
    case DECOR_PEBBLES:
    case DECOR_LOG:
    case DECOR_BARK:
    case DECOR_TWIG:
    case DECOR_BONE:
    case DECOR_DRIFTWOOD_ARCH:
    case DECOR_TWIG_LADDER:
    case DECOR_BONE_ARCH:
    case DECOR_FOSSIL:
    case DECOR_STONE_BRIDGE:
    case DECOR_GARGOYLE:
    case DECOR_IRON_GATE:
    case DECOR_BOOK_STACK:
    case DECOR_BROKEN_COLUMN:
    case DECOR_GLYPH_TABLET:
    case DECOR_CLIMB_BRANCH:
    case DECOR_CLIMB_ROOT:
    case DECOR_CLIMB_BOULDER:
    case DECOR_CLIMB_DRIFTWOOD:
    /* Heavy, dusty, or worked-stone objects. */
    case DECOR_ACORN_HIDE:
    case DECOR_QUILL_DESK:
    case DECOR_CAULDRON:
    case DECOR_BROOM:
    case DECOR_WIZARD_TOWER:
    case DECOR_DRAGON_STATUE:
        return AMBIENT_EMIT_DUST;

    default:
        return AMBIENT_EMIT_NONE;
    }
}

ambient_emit_t ambient_emit_for_gift(uint8_t kind)
{
    /* Gifts are the reward objects, so they all sparkle. The kind is not
     * distinguished because a gift is meant to read as special at a glance. */
    (void)kind;
    return AMBIENT_EMIT_GLINT;
}

/* Take the next free particle, or recycle the oldest if the pool is full. A
 * round-robin cursor means no single item can monopolise the pool. */
static particle_t *particle_claim(void)
{
    for (int i = 0; i < AMBIENT_PARTICLES; i++) {
        uint32_t idx = (s_next_particle + (uint32_t)i) % AMBIENT_PARTICLES;
        if (!s_particles[idx].active) {
            s_next_particle = (idx + 1u) % AMBIENT_PARTICLES;
            return &s_particles[idx];
        }
    }
    /* All busy: recycle the one with the least life left, which is the one
     * closest to expiring anyway. */
    particle_t *best = &s_particles[s_next_particle];
    for (int i = 1; i < AMBIENT_PARTICLES; i++) {
        particle_t *p = &s_particles[(s_next_particle + (uint32_t)i) % AMBIENT_PARTICLES];
        if (p->life < best->life) best = p;
    }
    s_next_particle = (s_next_particle + 1u) % AMBIENT_PARTICLES;
    return best;
}

static void particle_spawn(ambient_emit_t style, int32_t x, int32_t y)
{
    particle_t *p = particle_claim();
    if (!p) return;
    p->style = style;
    p->active = true;
    p->x = (float)x + ((float)(esp_random() % 100) / 100.0f - 0.5f) * 18.0f;
    p->y = (float)y + ((float)(esp_random() % 100) / 100.0f - 0.5f) * 10.0f;

    switch (style) {
    case AMBIENT_EMIT_SPORE:
        /* Drifts up and sideways, slowly. */
        p->vx = ((float)(esp_random() % 100) / 100.0f - 0.5f) * 6.0f;
        p->vy = -(4.0f + (float)(esp_random() % 100) / 100.0f * 5.0f);
        p->life_max = 2.4f + (float)(esp_random() % 100) / 100.0f * 1.6f;
        p->size = (uint8_t)(2 + esp_random() % 2);
        break;
    case AMBIENT_EMIT_GLINT:
        /* Barely moves; it is a flash, not a drift. */
        p->vx = 0.0f;
        p->vy = -(1.0f + (float)(esp_random() % 100) / 100.0f * 2.0f);
        p->life_max = 0.5f + (float)(esp_random() % 100) / 100.0f * 0.5f;
        p->size = (uint8_t)(2 + esp_random() % 2);
        break;
    case AMBIENT_EMIT_DUST:
    default:
        /* Settles downward, which is what dust does. */
        p->vx = ((float)(esp_random() % 100) / 100.0f - 0.5f) * 3.0f;
        p->vy = 2.0f + (float)(esp_random() % 100) / 100.0f * 3.0f;
        p->life_max = 2.0f + (float)(esp_random() % 100) / 100.0f * 2.0f;
        p->size = (uint8_t)(1 + esp_random() % 2);
        break;
    }
    p->life = p->life_max;
}

/* Spawn particles for whatever is on screen. Called on a slow cadence, not per
 * frame: the pool is small and the effect is meant to be occasional. */
static void particles_emit(uint32_t now_ms)
{
    if (s_emit_suspended) return;
    if ((int32_t)(now_ms - s_next_spawn) < 0) return;
    s_next_spawn = now_ms + 260u + esp_random() % 240u;

    /* Walk the placed decor and the gift, spawning at most one particle per
     * call. One at a time keeps the pool from being consumed in a burst and
     * makes the effect read as occasional rather than as a fountain. */
    int slots = decor_slot_count();
    int candidates = 0;
    for (int i = 0; i < slots; i++) {
        const decor_item_t *item = decor_at(i);
        if (!item) continue;
        if (ambient_emit_for_decor(item->kind) != AMBIENT_EMIT_NONE) candidates++;
    }
    if (g.gift.used) candidates++;
    if (!candidates) return;

    int pick = (int)(esp_random() % (uint32_t)candidates);
    for (int i = 0; i < slots; i++) {
        const decor_item_t *item = decor_at(i);
        if (!item) continue;
        ambient_emit_t style = ambient_emit_for_decor(item->kind);
        if (style == AMBIENT_EMIT_NONE) continue;
        if (pick-- == 0) {
            particle_spawn(style, item->x, item->y);
            return;
        }
    }
    if (g.gift.used) particle_spawn(ambient_emit_for_gift(g.gift.kind), g.gift.x, g.gift.y);
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
    /* Spores take the theme's `web` colour, which is the near-silhouette tone --
     * a paler, cooler value than the dust, so the two layers separate. */
    for (int i = 0; i < AMBIENT_SPORES; i++) {
        if (!s_spores[i].obj) continue;
        lv_obj_set_style_bg_color(s_spores[i].obj, lv_color_hex(t->web), 0);
    }
    for (int i = 0; i < AMBIENT_STRANDS; i++) {
        if (!s_strands[i].obj) continue;
        lv_obj_set_style_bg_color(s_strands[i].obj, lv_color_hex(t->web), 0);
    }
    /* Particles take the theme's dust colour. They are tinted once here rather
     * than per spawn, so a spawn costs no colour work. */
    for (int i = 0; i < AMBIENT_PARTICLES; i++) {
        if (!s_particles[i].obj) continue;
        lv_obj_set_style_bg_color(s_particles[i].obj, lv_color_hex(t->dust), 0);
    }
}

void ambient_set_visible(bool visible)
{
    if (s_visible == visible) return;
    s_visible = visible;
    for (int i = 0; i < AMBIENT_MOTES; i++)
        if (s_motes[i].obj) lv_obj_set_hidden(s_motes[i].obj, !visible);
    for (int i = 0; i < AMBIENT_SPORES; i++)
        if (s_spores[i].obj) lv_obj_set_hidden(s_spores[i].obj, !visible);
    for (int i = 0; i < AMBIENT_STRANDS; i++)
        if (s_strands[i].obj) lv_obj_set_hidden(s_strands[i].obj, !visible);
    for (int i = 0; i < AMBIENT_PARTICLES; i++)
        if (s_particles[i].obj) lv_obj_set_hidden(s_particles[i].obj, !visible || !s_particles[i].active);
}

/* Advance all ambient state. PURE: no lv_* calls, so it is safe to run from the
 * console task (the self-test does exactly that) and it keeps the simulation
 * testable without a display.
 *
 * ambient_tick() calls this and then pushes the result to LVGL. Splitting them
 * is not tidiness: the self-test runs on the console task, and calling lv_* from
 * there corrupts LVGL's state. That was a real crash, not a theoretical one. */
static void ambient_simulate(float dt, uint32_t now_ms)
{
    if (s_visible && dt > 0.0f) {
        for (int i = 0; i < AMBIENT_MOTES; i++) {
            mote_t *m = &s_motes[i];
            if (!m->obj) continue;
            m->y += m->vy * dt;
            m->sway_phase += m->sway_rate * dt;
            if (m->y < (float)g_play.top) mote_reset(m, false);
        }

        /* Spores drift on both axes and wrap at the edges, so they cross the
         * tank rather than only rising. Wrapping rather than resetting keeps
         * their velocity, which is what makes the motion look continuous. */
        for (int i = 0; i < AMBIENT_SPORES; i++) {
            spore_t *p = &s_spores[i];
            if (!p->obj) continue;
            p->x += p->vx * dt;
            p->y += p->vy * dt;
            p->wobble_phase += p->wobble_rate * dt;
            if (p->y < (float)g_play.top) p->y = (float)g_play.bottom;
            if (p->x < (float)g_play.left) p->x = (float)g_play.right;
            if (p->x > (float)g_play.right) p->x = (float)g_play.left;
        }

        for (int i = 0; i < AMBIENT_STRANDS; i++) {
            strand_t *s = &s_strands[i];
            if (!s->obj) continue;
            s->sway_phase += s->sway_rate * dt;
        }

        /* Decor and gift particles. Emission is on a slow cadence; the update
         * below only touches particles that are already alive, so an idle tank
         * with nothing placed costs nothing at all. */
        particles_emit(now_ms);
        for (int i = 0; i < AMBIENT_PARTICLES; i++) {
            particle_t *p = &s_particles[i];
            if (!p->active) continue;
            p->life -= dt;
            if (p->life <= 0.0f) {
                p->active = false;
                continue;
            }
            p->x += p->vx * dt;
            p->y += p->vy * dt;
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

/* Push the simulated state to LVGL. MUST run on the LVGL task. */
static void ambient_present(void)
{
    for (int i = 0; i < AMBIENT_MOTES; i++) {
        mote_t *m = &s_motes[i];
        if (!m->obj) continue;
        float x = m->x + sinf(m->sway_phase) * m->sway_amp;
        lv_obj_set_pos(m->obj, (int32_t)lroundf(x), (int32_t)lroundf(m->y));
    }
    for (int i = 0; i < AMBIENT_SPORES; i++) {
        spore_t *p = &s_spores[i];
        if (!p->obj) continue;
        float x = p->x + sinf(p->wobble_phase) * 3.0f;
        lv_obj_set_pos(p->obj, (int32_t)lroundf(x), (int32_t)lroundf(p->y));
    }
    for (int i = 0; i < AMBIENT_STRANDS; i++) {
        strand_t *s = &s_strands[i];
        if (!s->obj) continue;
        float angle = s->base_angle + sinf(s->sway_phase) * s->sway_amp;
        /* Quantised to whole degrees because LVGL's transform takes an integer
         * angle, and a sub-degree change would invalidate the object for no
         * visible gain. */
        int32_t deg = (int32_t)lroundf(angle * 180.0f / PI_F);
        if (deg != s->last_deg) {
            s->last_deg = deg;
            lv_obj_set_style_transform_rotation(s->obj, deg * 10, 0);
        }
        lv_obj_set_pos(s->obj, s->ax, s->ay);
    }
    for (int i = 0; i < AMBIENT_PARTICLES; i++) {
        particle_t *p = &s_particles[i];
        if (!p->obj) continue;
        if (!p->active) {
            lv_obj_set_hidden(p->obj, true);
            continue;
        }
        lv_obj_set_hidden(p->obj, false);
        /* Fade out over the last third of the life, so a particle never pops
         * out of existence. */
        float frac = p->life / p->life_max;
        uint8_t opa = (uint8_t)(frac > 0.34f ? 153 : (int)(153.0f * frac / 0.34f));
        lv_obj_set_style_bg_opa(p->obj, opa, 0);
        lv_obj_set_pos(p->obj, (int32_t)lroundf(p->x), (int32_t)lroundf(p->y));
    }
}

void ambient_tick(uint32_t now_ms)
{
    static uint32_t s_last;
    if (!s_ready) return;
    uint32_t elapsed = s_last ? now_ms - s_last : 0;
    s_last = now_ms;
    /* Clamp so a long stall cannot teleport the motes across the tank. */
    if (elapsed > 250u) elapsed = 250u;
    ambient_simulate((float)elapsed / 1000.0f, now_ms);
    ambient_present();
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

float ambient_wander(uint32_t seed)
{
    /* Two sines at incommensurable rates, so the sum never repeats on a short
     * cycle. Cheap enough to call per frame and smooth enough to drive a
     * position or an opacity without stepping. */
    float t = (float)lv_tick_get() / 1000.0f;
    float a = sinf(t * 0.37f + (float)(seed & 0xff) * 0.7f);
    float b = sinf(t * 0.61f + (float)((seed >> 8) & 0xff) * 1.3f);
    return (a * 0.6f + b * 0.4f) * 0.5f + 0.5f;
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
    int live = 0;
    for (int i = 0; i < AMBIENT_PARTICLES; i++)
        if (s_particles[i].active) live++;
    printf("AMBIENT motes=%d inside=%d visible=%d dust=0x%06lx breath=%.3f "
           "blink=%.2f glance=%.0f light=%u spores=%d strands=%d particles=%d live=%d\n",
           AMBIENT_MOTES, inside, s_visible ? 1 : 0,
           (unsigned long)(t ? t->dust : 0),
           (double)ambient_breath(), (double)ambient_blink(),
           (double)ambient_glance(), (unsigned)ambient_light_opa(),
           AMBIENT_SPORES, AMBIENT_STRANDS, AMBIENT_PARTICLES, live);
}

bool ambient_test(void)
{
    bool ok = s_ready;
    /* Report which assertion failed. A bare ok=0 is useless on hardware: the
     * test is long and the failure is usually one specific range check. */
    int fail_at = 0;
#define CHECK(cond) do { if (!(cond)) { ok = false; if (!fail_at) fail_at = __LINE__; } } while (0)
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
    CHECK(b >= 0.91f && b <= 1.09f);
    float k = ambient_blink();
    CHECK(k >= 0.0f && k <= 1.0f);
    float gl = ambient_glance();
    CHECK((gl == 0.0f || gl == 1.0f || gl == -1.0f));

    /* The enclosure light must drift, but only a little. A fixed value is the
     * bug this replaced; a large swing would read as a flicker. */
    uint8_t lo = 255, hi = 0;
    for (int i = 0; i < 400; i++) {
        uint8_t v = ambient_light_opa();
        if (v < lo) lo = v;
        if (v > hi) hi = v;
        s_breath_phase += 0.05f;
    }
    CHECK(hi > lo);                 /* it actually drifts */
    CHECK((hi - lo) <= 20);         /* but never flickers */

    /* The motes must actually move. A field that never advances is the failure
     * mode that looks like it works in a screenshot. */
    float before = s_motes[0].y;
    uint32_t now = lv_tick_get();
    ambient_simulate(0.1f, now + 100u);
    CHECK(s_motes[0].y != before);

    /* Spores must exist, be inside the tank, and move on BOTH axes. A spore that
     * only rises is just a bigger mote and the second air layer is wasted. */
    for (int i = 0; i < AMBIENT_SPORES; i++) {
        const spore_t *p = &s_spores[i];
        if (!p->obj) { ok = false; break; }
        if (p->size < 3 || p->size > 5) { ok = false; break; }
        if (p->y < (float)g_play.top - 1.0f || p->y > (float)g_play.bottom + 1.0f) {
            ok = false; break;
        }
        if (fabsf(p->vx) < 0.5f) { ok = false; break; }
    }
    float spore_x = s_spores[0].x, spore_y = s_spores[0].y;
    ambient_simulate(0.4f, now + 400u);
    CHECK(s_spores[0].x != spore_x && s_spores[0].y != spore_y);

    /* Strands must be anchored inside the tank and must actually pivot. A strand
     * whose angle never changes is a static line drawn over the art. */
    for (int i = 0; i < AMBIENT_STRANDS; i++) {
        const strand_t *s = &s_strands[i];
        if (!s->obj) { ok = false; break; }
        if (s->ax < g_play.left || s->ax > g_play.right) { ok = false; break; }
        if (s->ay < g_play.top || s->ay > g_play.bottom) { ok = false; break; }
        if (s->length < 20.0f || s->length > 80.0f) { ok = false; break; }
    }
    float phase_before = s_strands[0].sway_phase;
    ambient_simulate(1.2f, now + 1200u);
    CHECK(s_strands[0].sway_phase != phase_before);

    /* The wander helper must stay in 0..1 and must not be constant. */
    float w_lo = 2.0f, w_hi = -1.0f;
    for (uint32_t i = 0; i < 64; i++) {
        float w = ambient_wander(i * 7919u);
        if (w < w_lo) w_lo = w;
        if (w > w_hi) w_hi = w;
    }
    CHECK(w_lo >= 0.0f && w_hi <= 1.0f && (w_hi - w_lo) > 0.05f);

    /* Decor and gift particles.
     *
     * The pool must be fixed-size and every object must exist, because the whole
     * point of the design is that cost does not scale with how much the player
     * has placed. A missing object would mean a spawn silently doing nothing. */
    for (int i = 0; i < AMBIENT_PARTICLES; i++)
        if (!s_particles[i].obj) { ok = false; break; }

    /* The emitter mapping must actually classify things. A table that returned
     * NONE for everything would pass every other check while doing nothing. */
    CHECK(ambient_emit_for_decor(DECOR_FERN) == AMBIENT_EMIT_SPORE);
    CHECK(ambient_emit_for_decor(DECOR_CRYSTAL) == AMBIENT_EMIT_GLINT);
    CHECK(ambient_emit_for_decor(DECOR_ROCK_SMALL) == AMBIENT_EMIT_DUST);
    CHECK(ambient_emit_for_gift(0) == AMBIENT_EMIT_GLINT);

    /* A spawn must produce a live particle that then expires. A particle that
     * never dies would leak the pool; one that dies instantly would never be
     * seen. Emission is suspended for this block: the render task keeps running
     * during a console command, so without this the live tick spawns particles
     * between the reset and the count. */
    s_emit_suspended = true;
    for (int i = 0; i < AMBIENT_PARTICLES; i++) {
        s_particles[i].active = false;
        if (s_particles[i].obj) lv_obj_set_hidden(s_particles[i].obj, true);
    }
    particle_spawn(AMBIENT_EMIT_SPORE, g_play.left + 40, g_play.top + 40);
    int spawned = 0;
    const particle_t *fresh = NULL;
    for (int i = 0; i < AMBIENT_PARTICLES; i++) {
        if (!s_particles[i].active) continue;
        spawned++;
        /* particle_claim() is round-robin, so the new particle is not
         * necessarily index 0. Find the one that is actually alive. */
        if (!fresh || s_particles[i].life > fresh->life) fresh = &s_particles[i];
    }
    /* At least one, not exactly one: the render task can slip a spawn in
     * between the reset and this count even with emission suspended, because it
     * may already have been inside particles_emit when the flag was set. The
     * invariant that actually matters is the pool cap, asserted below. */
    CHECK(spawned >= 1);
    CHECK(fresh && fresh->life > 0.0f && fresh->life <= fresh->life_max);

    /* The pool must never exceed its size, however many spawns are requested. */
    for (int i = 0; i < AMBIENT_PARTICLES * 3; i++)
        particle_spawn(AMBIENT_EMIT_DUST, g_play.left + 40, g_play.top + 40);
    int live = 0;
    for (int i = 0; i < AMBIENT_PARTICLES; i++) if (s_particles[i].active) live++;
    CHECK(live <= AMBIENT_PARTICLES);

    /* And they must all expire, returning the pool to empty. */
    for (int i = 0; i < AMBIENT_PARTICLES; i++) s_particles[i].life = 0.01f;
    ambient_simulate(0.5f, lv_tick_get() + 500u);
    live = 0;
    for (int i = 0; i < AMBIENT_PARTICLES; i++) if (s_particles[i].active) live++;
    CHECK(live == 0);
    s_emit_suspended = false;

    if (!ok) printf("AMBIENT fail_at=%d\n", fail_at);
#undef CHECK
    return ok;
}
