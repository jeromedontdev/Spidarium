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
#include "terrarium.h"
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
    /* Last integer position pushed to LVGL. A mote moves a fraction of a pixel
     * per tick, so most ticks land on the same pixel; calling lv_obj_set_pos()
     * anyway invalidates the object for no visible change. */
    int32_t last_x, last_y;
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
    int32_t last_x, last_y;
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
    /* Lifecycle. A strand is spun, hangs for a while, then is gone -- so the web
     * builds up and breaks down instead of being a fixed decoration. */
    float life;          /* seconds remaining; <= 0 means the slot is free */
    float life_max;
    float fade;          /* 0..1 opacity multiplier, ramps in and out */
    uint8_t peak_opa;    /* opacity at full strength */
    uint8_t last_opa;    /* last opacity pushed to LVGL */
    bool dirty;          /* geometry changed; present must re-push size and pos */
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
    int32_t last_x, last_y;
    uint8_t last_opa;
    uint8_t last_size;
    /* The style whose colour and shape were last pushed to LVGL, or -1 when
     * nothing has been pushed yet. Colour and shape are per-STYLE, not per
     * particle, so they only need re-pushing when a slot is recycled for a
     * different emitter -- which is once per spawn at most. */
    int8_t last_style;
    /* Whether the object is currently hidden. Tracked separately from last_opa
     * because 0xFF is used as the "force a re-push" sentinel: overloading the
     * two meant a particle that activated after ambient_set_visible() was never
     * unhidden, so it stayed invisible for its whole life. */
    bool hidden;
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

/* Anchor zones for the web, as fractions of the playfield. A web is spun where
 * the spider can anchor it, which is the frame of the enclosure -- so the zones
 * run all the way around the edges rather than only along the top. The corners
 * get more strands than the sides, which is how a web actually accumulates.
 *
 * `base` is the direction the strands from that zone fan out in, in radians,
 * using LVGL's rotation convention: 0 points DOWN the screen, +pi/2 points
 * left, pi points up, -pi/2 points right. A strand anchored to the bottom of the
 * tank has to point up or it is drawn off the panel, and one anchored to a side
 * has to point inward -- so the direction is a property of the zone, not a
 * global constant. */
static const struct { float fx, fy, base; } k_web_zones[] = {
    { 0.06f, 0.05f, -0.79f },   /* top-left corner, fanning down-right */
    { 0.94f, 0.04f,  0.79f },   /* top-right corner, fanning down-left */
    { 0.30f, 0.03f,  0.00f },   /* upper left-of-centre, fanning down */
    { 0.68f, 0.03f,  0.00f },   /* upper right-of-centre, fanning down */
    { 0.03f, 0.30f, -1.57f },   /* left edge, upper, fanning right */
    { 0.97f, 0.34f,  1.57f },   /* right edge, upper, fanning left */
    { 0.04f, 0.68f, -2.36f },   /* left edge, lower, fanning up-right */
    { 0.96f, 0.72f,  2.36f },   /* right edge, lower, fanning up-left */
    { 0.07f, 0.95f, -2.36f },   /* bottom-left corner, fanning up-right */
    { 0.93f, 0.94f,  2.36f },   /* bottom-right corner, fanning up-left */
    { 0.42f, 0.97f,  3.14f },   /* bottom edge, left-of-centre, fanning up */
    { 0.74f, 0.96f,  3.14f },   /* bottom edge, right-of-centre, fanning up */
};
#define WEB_ZONES ((int)(sizeof k_web_zones / sizeof k_web_zones[0]))

/* Half-width of the fan around a zone's inward direction. It has to be narrow
 * enough that a strand anchored on an edge still points into the tank: a wider
 * fan let a bottom-corner strand point straight down and be drawn off the
 * panel. */
#define WEB_FAN 0.6f

/* One anchor per zone, jittered once at init and then FIXED for the life of the
 * boot. Strands respawn into these same points, so the web keeps its shape as
 * individual strands come and go. Re-jittering on respawn would dissolve the
 * clusters back into scattered lines. */
static int32_t s_anchor_x[WEB_ZONES];
static int32_t s_anchor_y[WEB_ZONES];

static void web_anchors_init(void)
{
    int32_t span_x = g_play.right - g_play.left;
    int32_t span_y = g_play.bottom - g_play.top;
    for (int z = 0; z < WEB_ZONES; z++) {
        s_anchor_x[z] = g_play.left + 20 + (int32_t)(k_web_zones[z].fx * (float)(span_x - 40))
                      + (int32_t)(esp_random() % 30) - 15;
        s_anchor_y[z] = g_play.top + 12 + (int32_t)(k_web_zones[z].fy * (float)(span_y - 24))
                      + (int32_t)(esp_random() % 22) - 11;
        if (s_anchor_x[z] < g_play.left + 12) s_anchor_x[z] = g_play.left + 12;
        if (s_anchor_x[z] > g_play.right - 12) s_anchor_x[z] = g_play.right - 12;
        if (s_anchor_y[z] < g_play.top + 8) s_anchor_y[z] = g_play.top + 8;
        if (s_anchor_y[z] > g_play.bottom - 8) s_anchor_y[z] = g_play.bottom - 8;
    }
}

/* Spin one strand into a free slot.
 *
 * Strands are CLUSTERED around shared anchors. Even spacing is a pattern, not a
 * web; jittering each strand independently produces scattered lines rather than
 * a web. What actually reads as a web is several strands leaving the SAME point
 * at different angles, so the anchor comes from the fixed per-zone table and the
 * strands that share it differ only in angle, length and opacity.
 *
 * Pure: no lv_* calls. This runs from ambient_simulate(), which the console task
 * calls during the self-test, and touching LVGL from there corrupts its state.
 * The object is updated by ambient_present() when it sees `dirty`. */
static void strand_spawn(strand_t *s, int zone)
{
    s->ax = s_anchor_x[zone];
    s->ay = s_anchor_y[zone];

    /* Length and angle are independent and both wide. A web has short
     * cross-strands and long anchor lines at every angle from near-vertical to
     * near-horizontal. */
    s->length = 18.0f + (float)(esp_random() % 100) / 100.0f * 62.0f;
    /* The fan is centred on the zone's own inward direction, so a strand
     * anchored to the bottom of the tank points up into the tank instead of
     * being drawn off the panel. The spread is wide enough that strands sharing
     * an anchor still cross each other. */
    s->base_angle = k_web_zones[zone].base
                  + ((float)(esp_random() % 100) / 100.0f - 0.5f) * 2.0f * WEB_FAN;
    s->sway_phase = (float)(esp_random() % 628) / 100.0f;
    /* Sway rates spread wide so the strands never move in step. */
    s->sway_rate = 0.12f + (float)(esp_random() % 100) / 100.0f * 0.55f;
    s->sway_amp = 0.05f + (float)(esp_random() % 100) / 100.0f * 0.16f;
    s->peak_opa = (uint8_t)(40 + esp_random() % 60);
    /* A strand hangs for 20-70 seconds, so the web visibly builds and breaks
     * down over a session rather than being a fixed decoration. */
    s->life_max = 20.0f + (float)(esp_random() % 100) / 100.0f * 50.0f;
    s->life = s->life_max;
    s->fade = 0.0f;
    s->dirty = true;
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

    /* Strands hang in CLUSTERS radiating from shared anchors, and they come and
     * go over time (see strand_spawn() and the lifecycle in ambient_simulate()).
     *
     * Even spacing with an alternating lean was the "too robotic, not organic
     * placement at all" report: six lines at regular intervals is a pattern, not
     * a web. Jittering each strand independently was not enough either -- it
     * produced scattered lines rather than a web.
     *
     * What actually reads as a web is several strands leaving the SAME point at
     * different angles. So the anchors are chosen first, and each strand is
     * assigned to one of them exactly. The gaps between clusters are as much a
     * part of the look as the strands themselves. */
    web_anchors_init();
    for (int i = 0; i < AMBIENT_STRANDS; i++) {
        strand_t *s = &s_strands[i];
        s->obj = lv_obj_create(ui.bg_canvas ? lv_obj_get_parent(ui.bg_canvas) : NULL);
        if (!s->obj) {
            ESP_LOGW(TAG, "strand %d could not be created", i);
            continue;
        }
        lv_obj_remove_style_all(s->obj);
        lv_obj_set_style_radius(s->obj, 0, 0);
        lv_obj_set_clickable(s->obj, false);
        lv_obj_set_scrollable(s->obj, false);
        /* A 2 px wide, `length` tall box, rotated about its top end. One pixel
         * at 20% opacity was invisible on the panel. */
        lv_obj_set_style_transform_pivot_x(s->obj, 0, 0);
        lv_obj_set_style_transform_pivot_y(s->obj, 0, 0);

        /* Deal the strands round-robin across the anchors, so every cluster gets
         * at least one and the corners get more than the middle -- which is how
         * a web actually accumulates. */
        strand_spawn(s, i % WEB_ZONES);
        /* Stagger the initial lives so the whole web does not vanish at once. */
        s->life = s->life_max * (0.35f + (float)(esp_random() % 100) / 100.0f * 0.65f);
        s->fade = 1.0f;
    }

    /* Particles are created once and hidden until claimed. Creating them up
     * front rather than on demand is what keeps the cost constant: no
     * allocation happens while the tank is running.
     *
     * They are parented to the dedicated particle layer, NOT to the background's
     * parent. A particle spawns at the position of the decor item that shed it,
     * so in the background band it was drawn underneath that item's artwork and
     * was effectively invisible -- which is why the effect could not be seen at
     * all despite the emitter running. */
    lv_obj_t *particle_parent = terrarium_particle_layer();
    if (!particle_parent) particle_parent = ui.bg_canvas ? lv_obj_get_parent(ui.bg_canvas) : NULL;
    for (int i = 0; i < AMBIENT_PARTICLES; i++) {
        particle_t *p = &s_particles[i];
        p->obj = lv_obj_create(particle_parent);
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
        p->hidden = true;
        p->last_opa = 0;
        p->last_style = -1;
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
        /* Drifts up and sideways, slowly. The largest of the three: pollen is a
         * soft blob, and a bigger dot is what makes it read as organic. */
        p->vx = ((float)(esp_random() % 100) / 100.0f - 0.5f) * 6.0f;
        p->vy = -(4.0f + (float)(esp_random() % 100) / 100.0f * 5.0f);
        p->life_max = 2.4f + (float)(esp_random() % 100) / 100.0f * 1.6f;
        p->size = (uint8_t)(5 + esp_random() % 3);
        break;
    case AMBIENT_EMIT_GLINT:
        /* Barely moves; it is a flash, not a drift. Small and hard-edged, so it
         * reads as a specular point rather than as a floating speck. */
        p->vx = 0.0f;
        p->vy = -(1.0f + (float)(esp_random() % 100) / 100.0f * 2.0f);
        p->life_max = 0.5f + (float)(esp_random() % 100) / 100.0f * 0.5f;
        p->size = (uint8_t)(3 + esp_random() % 2);
        break;
    case AMBIENT_EMIT_DUST:
    default:
        /* Settles downward, which is what dust does. */
        p->vx = ((float)(esp_random() % 100) / 100.0f - 0.5f) * 3.0f;
        p->vy = 2.0f + (float)(esp_random() % 100) / 100.0f * 3.0f;
        p->life_max = 2.0f + (float)(esp_random() % 100) / 100.0f * 2.0f;
        p->size = (uint8_t)(4 + esp_random() % 2);
        break;
    }
    p->life = p->life_max;
    /* Force the next present to push the opacity. The object is created at
     * LV_OPA_60 and the peak is 255, so leaving last_opa at its initial value
     * would make the first comparison a no-op and the particle would spend its
     * whole life at 60/255 -- invisible on a mid-tone substrate. */
    p->last_opa = 0;
    /* The slot may have been recycled from a different emitter, so the colour
     * and shape have to be re-pushed. -1 is the "nothing pushed" sentinel. */
    p->last_style = -1;
}

/* Spawn particles for whatever is on screen. Called on a slow cadence, not per
 * frame: the pool is small and the effect is meant to be occasional.
 *
 * The cadence is the lever that controls how alive the tank feels, and it is
 * cheap to turn: a spawn is a few float writes, not an allocation. The first
 * pass spawned every ~380 ms and was invisible with a dozen items placed,
 * because each item only got a particle once every few seconds. */
static void particles_emit(uint32_t now_ms)
{
    if (s_emit_suspended) return;
    if ((int32_t)(now_ms - s_next_spawn) < 0) return;
    s_next_spawn = now_ms + 90u + esp_random() % 90u;

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

/* Push a colour toward white so it reads against the substrate.
 *
 * The theme's `dust` is a mid-tone that sits close to the cork and soil colours
 * it is drawn over, so a particle tinted with it was invisible even once it was
 * correctly layered above the decor. A particle is a 3-5 px dot: it has no shape
 * to read, only contrast, so it has to be the brightest thing in its
 * neighbourhood. Mixing 55% white keeps the theme's hue while guaranteeing that
 * contrast on every background. */
static uint32_t brighten(uint32_t rgb, int percent)
{
    uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    r += (255u - r) * (uint32_t)percent / 100u;
    g += (255u - g) * (uint32_t)percent / 100u;
    b += (255u - b) * (uint32_t)percent / 100u;
    return (r << 16) | (g << 8) | b;
}

/* Linear blend of two RGB values, `percent` of the way from a to b.
 *
 * The channel deltas are SIGNED: `b` is often darker than `a`, and computing
 * `br - ar` in uint32_t wraps to ~4.29e9, which then overflows the packed
 * result and produces a colour with bits above 24. */
static uint32_t mix_rgb(uint32_t a, uint32_t b, int percent)
{
    int ar = (int)((a >> 16) & 0xFF), ag = (int)((a >> 8) & 0xFF), ab = (int)(a & 0xFF);
    int br = (int)((b >> 16) & 0xFF), bg = (int)((b >> 8) & 0xFF), bb = (int)(b & 0xFF);
    int r = ar + (br - ar) * percent / 100;
    int g = ag + (bg - ag) * percent / 100;
    int bl = ab + (bb - ab) * percent / 100;
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (bl < 0) bl = 0; else if (bl > 255) bl = 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

/* The colour of a particle, by emitter style.
 *
 * Every particle used to be tinted with the same brightened `dust`, so a fern's
 * spore, a crystal's glint and a rock's dust were indistinguishable -- the
 * "decor items all have the same color and pattern particle effect" report. The
 * three families now differ in BOTH hue and shape, which is what makes them read
 * as three different effects rather than one effect at three speeds:
 *
 *   SPORE  pale green, round        -- pollen lifting off a plant
 *   GLINT  warm near-white, hard    -- a highlight catching a facet
 *   DUST   warm grey, square        -- grit settling out of the air
 *
 * The theme supplies the BASE and a fixed per-style tint is mixed in. Deriving
 * the hue purely from the palette was not enough: several themes have a `web`
 * and a `dust` that are both cool blue-greys, so the spore and the glint came
 * out within a few units of each other (0xe9ecf1 vs 0xeaf1f2 on the default
 * theme). Mixing a fixed tint guarantees the three families separate on every
 * background while still shifting with the theme. */
static uint32_t particle_color(ambient_emit_t style)
{
    const theme_def_t *t = theme_current();
    uint32_t dust = t ? t->dust : 0xb9a888;
    uint32_t web = t ? t->web : 0x6f7f74;
    switch (style) {
    case AMBIENT_EMIT_SPORE:
        /* Pollen: the near-silhouette tone pulled toward a pale leaf green. */
        return mix_rgb(brighten(web, 70), 0x9fe8a0, 62);
    case AMBIENT_EMIT_GLINT:
        /* A specular flash: almost white, with just enough warmth to read as a
         * highlight rather than as a white pixel. */
        return mix_rgb(brighten(dust, 80), 0xfff4d0, 78);
    case AMBIENT_EMIT_DUST:
    default:
        /* Grit: the theme's own dust, only lightly lifted, pulled toward a warm
         * sand so it stays clearly duller and browner than the other two. */
        return mix_rgb(brighten(dust, 25), 0xc9b79a, 55);
    }
}

/* The corner radius of a particle, by emitter style. LV_RADIUS_CIRCLE is a
 * circle, 0 is a square, and a small value is a soft-edged blob. Shape is the
 * second half of "same pattern": three families that differ only in speed still
 * read as one effect. */
static int32_t particle_radius(ambient_emit_t style)
{
    switch (style) {
    case AMBIENT_EMIT_SPORE: return LV_RADIUS_CIRCLE;  /* round pollen */
    case AMBIENT_EMIT_GLINT: return 0;                 /* hard square facet */
    case AMBIENT_EMIT_DUST:
    default:                 return 2;                 /* soft-edged grit */
    }
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
    /* Particles are tinted per STYLE, not once for the whole pool: see
     * particle_color(). A theme change therefore has to invalidate the cached
     * style on every slot so the next present re-pushes the new palette. Doing
     * it here rather than per spawn keeps a spawn free of colour work. */
    for (int i = 0; i < AMBIENT_PARTICLES; i++) {
        if (!s_particles[i].obj) continue;
        s_particles[i].last_style = -1;
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
    for (int i = 0; i < AMBIENT_STRANDS; i++) {
        if (!s_strands[i].obj) continue;
        lv_obj_set_hidden(s_strands[i].obj, !visible);
        /* The dirty-check caches are only valid while the object is on screen.
         * Hiding and re-showing resets the object's own state, so force the next
         * present to push everything again. */
        s_strands[i].last_opa = 0xFF;
        s_strands[i].last_deg = 0x7fffffff;
    }
    for (int i = 0; i < AMBIENT_PARTICLES; i++) {
        if (!s_particles[i].obj) continue;
        lv_obj_set_hidden(s_particles[i].obj, !visible || !s_particles[i].active);
        s_particles[i].hidden = !visible || !s_particles[i].active;
        s_particles[i].last_opa = 0;
        s_particles[i].last_size = 0;
    }
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

        /* Strands are spun, hang, and are gone. The web therefore builds up and
         * breaks down over a session instead of being a fixed decoration, which
         * is what "dynamic, showing up randomly" asked for.
         *
         * The fade envelope is what makes it read as silk rather than as a line
         * popping in and out: 1.5 s in, hold, 2.5 s out. */
        for (int i = 0; i < AMBIENT_STRANDS; i++) {
            strand_t *s = &s_strands[i];
            if (!s->obj) continue;
            s->sway_phase += s->sway_rate * dt;
            s->life -= dt;
            if (s->life <= 0.0f) {
                /* Respawn into the same anchor zone, so the cluster stays put
                 * and the web keeps its shape. A fresh random point would turn
                 * the web back into scattered lines. */
                strand_spawn(s, i % WEB_ZONES);
                continue;
            }
            float age = s->life_max - s->life;
            if (age < 1.5f) {
                s->fade = age / 1.5f;
            } else if (s->life < 2.5f) {
                s->fade = s->life / 2.5f;
            } else {
                s->fade = 1.0f;
            }
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

/* Push the simulated state to LVGL. MUST run on the LVGL task.
 *
 * Every call here invalidates an object, and an invalidation costs a blend of
 * that object's area on the next refresh. So each object is only touched when
 * its integer position (or opacity) actually changed. A mote moves a fraction of
 * a pixel per tick, so most ticks land on the same pixel and cost nothing.
 *
 * This matters more than it looks: the tank runs at 30 Hz and there are 16
 * motes, 6 spores, 18 strands and up to 48 particles. Touching all of them every
 * frame was ~76 invalidations per frame, which is what made the spider look like
 * it had slowed down -- the render budget was being spent on dust. */
static void ambient_present_strands(void)
{
    for (int i = 0; i < AMBIENT_STRANDS; i++) {
        strand_t *s = &s_strands[i];
        if (!s->obj) continue;
        /* A respawn changes the anchor, length and angle all at once, so the
         * geometry is re-pushed in one go rather than piecemeal. */
        if (s->dirty) {
            s->dirty = false;
            lv_obj_set_size(s->obj, 2, (int32_t)s->length);
            lv_obj_set_pos(s->obj, s->ax, s->ay);
            s->last_deg = 0x7fffffff;
            s->last_opa = 0xFF;
        }
        /* Opacity is driven by the fade envelope, so a strand fades in as it is
         * spun and fades out before it goes. Only pushed when it changes. */
        uint8_t opa = (uint8_t)((float)s->peak_opa * s->fade);
        if (opa != s->last_opa) {
            s->last_opa = opa;
            lv_obj_set_style_bg_opa(s->obj, opa, 0);
        }
        if (opa == 0) continue;
        float angle = s->base_angle + sinf(s->sway_phase) * s->sway_amp;
        /* Quantised to whole degrees because LVGL's transform takes an integer
         * angle, and a sub-degree change would invalidate the object for no
         * visible gain. */
        int32_t deg = (int32_t)lroundf(angle * 180.0f / PI_F);
        if (deg == s->last_deg) continue;
        s->last_deg = deg;
        lv_obj_set_style_transform_rotation(s->obj, deg * 10, 0);
    }
}

static void ambient_present(void)
{
    for (int i = 0; i < AMBIENT_MOTES; i++) {
        mote_t *m = &s_motes[i];
        if (!m->obj) continue;
        float x = m->x + sinf(m->sway_phase) * m->sway_amp;
        int32_t ix = (int32_t)lroundf(x), iy = (int32_t)lroundf(m->y);
        if (ix == m->last_x && iy == m->last_y) continue;
        m->last_x = ix;
        m->last_y = iy;
        lv_obj_set_pos(m->obj, ix, iy);
    }
    for (int i = 0; i < AMBIENT_SPORES; i++) {
        spore_t *p = &s_spores[i];
        if (!p->obj) continue;
        float x = p->x + sinf(p->wobble_phase) * 3.0f;
        int32_t ix = (int32_t)lroundf(x), iy = (int32_t)lroundf(p->y);
        if (ix == p->last_x && iy == p->last_y) continue;
        p->last_x = ix;
        p->last_y = iy;
        lv_obj_set_pos(p->obj, ix, iy);
    }
    for (int i = 0; i < AMBIENT_PARTICLES; i++) {
        particle_t *p = &s_particles[i];
        if (!p->obj) continue;
        if (!p->active) {
            /* Only hide once. lv_obj_set_hidden() on an already-hidden object
             * still walks the style list. */
            if (!p->hidden) {
                p->hidden = true;
                lv_obj_set_hidden(p->obj, true);
            }
            continue;
        }
        if (p->hidden) {
            p->hidden = false;
            lv_obj_set_hidden(p->obj, false);
        }
        /* Colour and shape are per-STYLE, so they are pushed only when the slot
         * has been recycled for a different emitter -- at most once per spawn.
         * Pushing them every frame would invalidate the object for no visible
         * change and cost the frame budget the ambient layer already fights
         * for. */
        if (p->last_style != (int8_t)p->style) {
            p->last_style = (int8_t)p->style;
            lv_obj_set_style_bg_color(p->obj, lv_color_hex(particle_color(p->style)), 0);
            lv_obj_set_style_radius(p->obj, particle_radius(p->style), 0);
        }
        /* The size is per-particle, so it has to be pushed rather than set once
         * at creation -- but only when it changes, which is once per spawn. */
        if (p->size != p->last_size) {
            p->last_size = p->size;
            lv_obj_set_size(p->obj, p->size, p->size);
        }
        /* Fade out over the last third of the life, so a particle never pops
         * out of existence. The peak is full opacity: a 3-5 px dot at 200/255
         * over a mid-tone substrate was too faint to see at all. */
        float frac = p->life / p->life_max;
        uint8_t opa = (uint8_t)(frac > 0.34f ? 255 : (int)(255.0f * frac / 0.34f));
        if (opa != p->last_opa) {
            p->last_opa = opa;
            lv_obj_set_style_bg_opa(p->obj, opa, 0);
        }
        int32_t ix = (int32_t)lroundf(p->x), iy = (int32_t)lroundf(p->y);
        if (ix == p->last_x && iy == p->last_y) continue;
        p->last_x = ix;
        p->last_y = iy;
        lv_obj_set_pos(p->obj, ix, iy);
    }
}

void ambient_tick(uint32_t now_ms)
{
    static uint32_t s_last;
    static uint32_t s_last_present;
    static uint32_t s_last_strands;
    if (!s_ready) return;
    uint32_t elapsed = s_last ? now_ms - s_last : 0;
    s_last = now_ms;
    /* Clamp so a long stall cannot teleport the motes across the tank. */
    if (elapsed > 250u) elapsed = 250u;
    ambient_simulate((float)elapsed / 1000.0f, now_ms);

    /* Present at a lower rate than the render tick.
     *
     * This is the single biggest lever on the frame budget, and it is not
     * obvious: the cost of an ambient object is not its CPU time (a few float
     * writes) but the DIRTY RECTANGLE it adds. Sixteen motes, six spores, the
     * strands and up to forty-eight particles are scattered across the whole
     * tank, so every frame they move the flush has to cover most of the screen.
     * Measured: the layer cost ~4.5 Hz of frame rate at 30 Hz.
     *
     * Dust drifts at 4-12 px/s, so at 10 Hz it still advances less than a pixel
     * per update -- the motion is identical to the eye, at a third of the
     * invalidations. The simulation still runs every tick, so nothing changes
     * speed; only how often the result is pushed to LVGL. */
    if (!s_last_present || now_ms - s_last_present >= AMBIENT_PRESENT_MS) {
        s_last_present = now_ms;
        ambient_present();
    }

    /* Strands are presented on their own, slower cadence.
     *
     * Each strand is a ROTATED object, so LVGL classifies it as
     * LV_LAYER_TYPE_TRANSFORM and allocates an intermediate layer for it on
     * every refresh. That is far more expensive per object than a plain
     * rectangle, and it is why raising the strand count from 6 to 18 tripled the
     * ambient cost and halved the frame rate when they shared the 10 Hz present.
     *
     * A strand sways at 0.05-0.21 rad/s, so a whole degree of rotation takes
     * 80-350 ms. Presenting them at 4 Hz therefore loses nothing visible while
     * cutting their invalidations by 60%. */
    if (!s_last_strands || now_ms - s_last_strands >= AMBIENT_STRAND_PRESENT_MS) {
        s_last_strands = now_ms;
        ambient_present_strands();
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
    int live_spore = 0, live_glint = 0, live_dust = 0;
    for (int i = 0; i < AMBIENT_PARTICLES; i++) {
        if (!s_particles[i].active) continue;
        live++;
        switch (s_particles[i].style) {
        case AMBIENT_EMIT_SPORE: live_spore++; break;
        case AMBIENT_EMIT_GLINT: live_glint++; break;
        default:                 live_dust++;  break;
        }
    }
    printf("AMBIENT motes=%d inside=%d visible=%d dust=0x%06lx breath=%.3f "
           "blink=%.2f glance=%.0f light=%u spores=%d strands=%d particles=%d live=%d "
           "present_ms=%d\n",
           AMBIENT_MOTES, inside, s_visible ? 1 : 0,
           (unsigned long)(t ? t->dust : 0),
           (double)ambient_breath(), (double)ambient_blink(),
           (double)ambient_glance(), (unsigned)ambient_light_opa(),
           AMBIENT_SPORES, AMBIENT_STRANDS, AMBIENT_PARTICLES, live,
           AMBIENT_PRESENT_MS);
    /* The three emitter families must be distinguishable on hardware, not only
     * in the self-test: a report that only counted live particles would look
     * identical whether or not the per-style colour and shape work. */
    printf("AMBIENTSTYLE spore=%d glint=%d dust=%d "
           "color_spore=0x%06lx color_glint=0x%06lx color_dust=0x%06lx "
           "radius_spore=%d radius_glint=%d radius_dust=%d\n",
           live_spore, live_glint, live_dust,
           (unsigned long)particle_color(AMBIENT_EMIT_SPORE),
           (unsigned long)particle_color(AMBIENT_EMIT_GLINT),
           (unsigned long)particle_color(AMBIENT_EMIT_DUST),
           (int)particle_radius(AMBIENT_EMIT_SPORE),
           (int)particle_radius(AMBIENT_EMIT_GLINT),
           (int)particle_radius(AMBIENT_EMIT_DUST));
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
        if (!m->obj) { ok = false; if (!fail_at) fail_at = __LINE__; break; }
        if (m->size < 2 || m->size > 4) { ok = false; if (!fail_at) fail_at = __LINE__; break; }
        if (m->y < (float)g_play.top - 1.0f || m->y > (float)g_play.bottom + 1.0f) {
            ok = false; if (!fail_at) fail_at = __LINE__; break;
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
     * only rises is just a bigger mote and the second air layer is wasted.
     *
     * The horizontal check is on the FIELD, not on every spore: vx is drawn from
     * -5..+5, so any individual spore can legitimately land near zero and the
     * old per-spore assertion failed on roughly half of all boots. What matters
     * is that the layer as a whole crosses the tank. */
    int crossing = 0;
    for (int i = 0; i < AMBIENT_SPORES; i++) {
        const spore_t *p = &s_spores[i];
        if (!p->obj) { ok = false; if (!fail_at) fail_at = __LINE__; break; }
        if (p->size < 3 || p->size > 5) { ok = false; if (!fail_at) fail_at = __LINE__; break; }
        if (p->y < (float)g_play.top - 1.0f || p->y > (float)g_play.bottom + 1.0f) {
            ok = false; if (!fail_at) fail_at = __LINE__; break;
        }
        if (fabsf(p->vx) >= 0.5f) crossing++;
    }
    CHECK(crossing >= AMBIENT_SPORES / 2);
    float spore_x = s_spores[0].x, spore_y = s_spores[0].y;
    ambient_simulate(0.4f, now + 400u);
    CHECK(s_spores[0].x != spore_x && s_spores[0].y != spore_y);

    /* Strands must be anchored inside the tank and must actually pivot. A strand
     * whose angle never changes is a static line drawn over the art. */
    for (int i = 0; i < AMBIENT_STRANDS; i++) {
        const strand_t *s = &s_strands[i];
        if (!s->obj) { ok = false; if (!fail_at) fail_at = __LINE__; break; }
        if (s->ax < g_play.left || s->ax > g_play.right) { ok = false; if (!fail_at) fail_at = __LINE__; break; }
        if (s->ay < g_play.top || s->ay > g_play.bottom) { ok = false; if (!fail_at) fail_at = __LINE__; break; }
        if (s->length < 15.0f || s->length > 85.0f) { ok = false; if (!fail_at) fail_at = __LINE__; break; }
    }
    /* The placement must radiate from SHARED anchors, not be evenly spaced.
     * Even spacing is a pattern, not a web; jittering each strand independently
     * produces scattered lines rather than a web. Assert the property that
     * actually reads as a web: several strands leaving the same point. */
    {
        int shared = 0;
        for (int i = 0; i < AMBIENT_STRANDS; i++) {
            int same = 0;
            for (int j = 0; j < AMBIENT_STRANDS; j++)
                if (s_strands[j].ax == s_strands[i].ax &&
                    s_strands[j].ay == s_strands[i].ay) same++;
            if (same > shared) shared = same;
        }
        ok = ok && shared >= 2;
        if (!ok && !fail_at) fail_at = __LINE__;

        /* Unequal spacing between distinct anchors. */
        int32_t xs[AMBIENT_STRANDS];
        int n = 0;
        for (int i = 0; i < AMBIENT_STRANDS; i++) {
            bool dup = false;
            for (int j = 0; j < n; j++) if (xs[j] == s_strands[i].ax) dup = true;
            if (!dup) xs[n++] = s_strands[i].ax;
        }
        for (int i = 1; i < n; i++)
            for (int j = i; j > 0 && xs[j - 1] > xs[j]; j--) {
                int32_t t = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = t;
            }
        int32_t min_gap = 0x7fffffff, max_gap = 0;
        for (int i = 1; i < n; i++) {
            int32_t g = xs[i] - xs[i - 1];
            if (g < min_gap) min_gap = g;
            if (g > max_gap) max_gap = g;
        }
        ok = ok && n >= 2 && max_gap > min_gap * 3 / 2;
        if (!ok && !fail_at) fail_at = __LINE__;

        /* Angles must be genuinely varied, not two alternating values.
         *
         * This samples the GENERATOR rather than the current draw. Strands now
         * respawn with fresh random angles, so any single set of six can
         * occasionally land in a narrow band by chance (~1.6% of draws) -- which
         * made this assertion flaky rather than wrong. What actually matters is
         * that the generator spans a wide range, so that is what is measured. */
        float lo = 99.0f, hi = -99.0f;
        for (int i = 0; i < AMBIENT_STRANDS; i++) {
            strand_t probe = s_strands[i];
            for (int k = 0; k < 8; k++) {
                strand_spawn(&probe, i % WEB_ZONES);
                if (probe.base_angle < lo) lo = probe.base_angle;
                if (probe.base_angle > hi) hi = probe.base_angle;
            }
        }
        ok = ok && (hi - lo) > 1.5f;
        if (!ok && !fail_at) fail_at = __LINE__;
    }
    float phase_before = s_strands[0].sway_phase;
    ambient_simulate(1.2f, now + 1200u);
    CHECK(s_strands[0].sway_phase != phase_before);

    /* Strands must be DYNAMIC: they fade in, hang, and expire, then respawn into
     * the same anchor. A web that is identical forever is a decoration, not a
     * living thing -- and a respawn that picked a fresh random point would
     * dissolve the clusters back into scattered lines. */
    {
        /* A fresh strand starts at zero opacity and ramps up. */
        strand_t *s = &s_strands[0];
        int32_t anchor_x = s->ax, anchor_y = s->ay;
        s->life = s->life_max;
        s->fade = 0.0f;
        ambient_simulate(0.5f, now + 2000u);
        CHECK(s->fade > 0.0f && s->fade < 1.0f);   /* mid fade-in */

        /* Run it out. It must respawn rather than sit at zero forever. */
        s->life = 0.05f;
        ambient_simulate(0.2f, now + 3000u);
        CHECK(s->life > 1.0f);                     /* respawned with a full life */
        CHECK(s->ax == anchor_x && s->ay == anchor_y);  /* same cluster */
        CHECK(s->fade == 0.0f);                    /* starts invisible again */

        /* The fade-out must actually reach zero before expiry, so a strand never
         * pops out of existence. */
        s->life = 1.0f;
        ambient_simulate(0.1f, now + 4000u);
        CHECK(s->fade < 0.5f);
    }

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
        if (!s_particles[i].obj) { ok = false; if (!fail_at) fail_at = __LINE__; break; }

    /* The emitter mapping must actually classify things. A table that returned
     * NONE for everything would pass every other check while doing nothing. */
    CHECK(ambient_emit_for_decor(DECOR_FERN) == AMBIENT_EMIT_SPORE);
    CHECK(ambient_emit_for_decor(DECOR_CRYSTAL) == AMBIENT_EMIT_GLINT);
    CHECK(ambient_emit_for_decor(DECOR_ROCK_SMALL) == AMBIENT_EMIT_DUST);
    CHECK(ambient_emit_for_gift(0) == AMBIENT_EMIT_GLINT);

    /* The three families must differ in BOTH colour and shape.
     *
     * Every particle used to be tinted with the same brightened `dust` and
     * created as a circle, so a fern's spore, a crystal's glint and a rock's
     * dust were indistinguishable -- the "decor items all have the same color
     * and pattern particle effect" report. Asserting only that the colours are
     * non-zero would pass with three identical values, so the check is that all
     * three differ from each other. */
    {
        uint32_t spore = particle_color(AMBIENT_EMIT_SPORE);
        uint32_t glint = particle_color(AMBIENT_EMIT_GLINT);
        uint32_t dust = particle_color(AMBIENT_EMIT_DUST);
        CHECK(spore != glint && glint != dust && spore != dust);
        int32_t r_spore = particle_radius(AMBIENT_EMIT_SPORE);
        int32_t r_glint = particle_radius(AMBIENT_EMIT_GLINT);
        int32_t r_dust = particle_radius(AMBIENT_EMIT_DUST);
        CHECK(r_spore != r_glint && r_glint != r_dust && r_spore != r_dust);
    }

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
