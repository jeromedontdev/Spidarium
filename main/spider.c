/*
 * Spidarium — spider rendering.
 *
 * Procedural, jointed limbs: eight two-segment legs drawn as lv_line polylines
 * with a visible knee, driven by a tetrapod gait so neighbouring legs alternate.
 * This replaces the sprite-based renderer, which read as a static blob and lost
 * the articulated leg motion.
 *
 * Position and heading come from the behaviour module; this module only turns
 * them into pixels.
 */
#include "spider.h"

#include "behaviour.h"
#include "game_config.h"
#include "prey.h"

#include <math.h>

#define PI_F 3.14159265f

/* Body geometry.
 *
 * BODY_R is the leg hip radius and must not change: the leg motion is tuned
 * against it. BODY_BOX is the anatomy container, which is deliberately larger
 * so the abdomen is not clipped by the container bounds (it used to be: the
 * box was 32x32 while the abdomen reached x=36, y=38). */
#define BODY_R        16
#define BODY_BOX      68
#define BODY_C        (BODY_BOX / 2)
#define LEG_LEN       22.0f
#define LEG_SEG1      0.58f    /* femur fraction of LEG_LEN */
#define LEG_SEG2      0.72f    /* tibia fraction of LEG_LEN */
#define LEG_WIDTH     3

/* Anatomy, in body-local (forward, right) coordinates.
 *
 * These are NOT screen offsets. Every part is placed each frame by rotating
 * (forward, right) through the current heading, so the head always points the
 * way the spider is travelling. Fixed screen offsets were the bug: the legs
 * rotated with heading but the body did not, so the spider appeared to walk
 * sideways with its head permanently pointing up.
 *
 * The two masses must OVERLAP at the waist. A gap between them lets the
 * terrarium background show through the middle of the creature, which reads as
 * a detached head floating in front of the body -- the "head is on backwards"
 * illusion. Abdomen spans ABD_FWD +/- ABD_R = -26 .. 0; head spans
 * CEPH_FWD +/- CEPH_R = -3 .. 19. They therefore overlap by 3 px and form a
 * single silhouette.
 *
 * The eyes are deliberately oversized and cartoonish. At this physical size
 * realistic spider eyes are 2-3 px and vanish entirely; big amber eyes with
 * dark pupils are what make the creature legible and expressive. */
#define ABD_FWD       (-13.0f)  /* behind centre; overlaps the head at the waist */
#define ABD_R         13
#define CEPH_FWD      8.0f      /* ahead of centre */
#define CEPH_R        11

/* Cartoon restyle.
 *
 * The body was two flat grey circles. It now reads as a soft, fuzzy creature:
 * a warm body colour, a lighter fuzzy rim (a ring of small overlapping dots
 * around each mass), and big WHITE eyes with dark pupils. White eyes on the
 * near-black substrate are the strongest contrast available on this panel, and
 * contrast is what makes a face legible at 2.16 inches.
 *
 * The legs are deliberately NOT touched. Their geometry, colour and gait are
 * tuned and the user explicitly asked to leave them alone. */
/* Black-to-grey gradient. LVGL 9.6 has no radial gradient fill for plain
 * objects, so the gradient is built by stacking progressively smaller, lighter
 * circles offset toward the light source (up-left). Three steps per mass is
 * enough to read as a soft sphere at this size. */
/* Body colour palettes. Each entry is a base/mid/highlight triple plus a fuzzy
 * rim colour, so a palette is one row. The player picks one at first start.
 *
 * Index 0 is the default black-to-grey. */
typedef struct {
    const char *name;
    uint32_t    base;
    uint32_t    mid;
    uint32_t    hi;
    uint32_t    fuzz;
} body_palette_t;

#define BODY_PALETTE_COUNT 6

static const body_palette_t k_palettes[BODY_PALETTE_COUNT] = {
    { "charcoal", 0x14171c, 0x2b3138, 0x454d57, 0x3a414a },
    { "ash",      0x2a2a2e, 0x45454b, 0x64646c, 0x52525a },
    { "rust",     0x2e1a12, 0x4d2c1e, 0x6e422c, 0x5a3524 },
    { "moss",     0x16241a, 0x27402c, 0x3a5c40, 0x2f4a35 },
    { "indigo",   0x161a2e, 0x272e4d, 0x3a446e, 0x2f365a },
    { "plum",     0x241624, 0x402740, 0x5c3a5c, 0x4a2f4a },
};

/* The active palette. Falls back to charcoal if the stored index is unset. */
static const body_palette_t *palette(void)
{
    uint8_t i = g.colour;
    if (i >= BODY_PALETTE_COUNT) i = 0;
    return &k_palettes[i];
}

/* ------------------------------------------------------------------ */
/* Accent colours: pupils, legs, fangs                                 */
/* ------------------------------------------------------------------ */
/* Each is a simple named list, so the setup screen can offer the same swatch
 * UI for all three without special-casing any of them. */

typedef struct {
    const char *name;
    uint32_t    colour;
} accent_t;

#define ACCENT_COUNT 9

/* Index 0/1/2 are the defaults for pupils, fangs and legs respectively, so the
 * order here matters: a fresh spider must look like the old one. */
#define ACCENT_BLACK 0
#define ACCENT_WHITE 1
#define ACCENT_GREY  2

static const accent_t k_accents[ACCENT_COUNT] = {
    { "black",   0x000000 },
    { "white",   0xf7f9ff },
    { "grey",    0x8a94a6 },
    { "amber",   0xf5b342 },
    { "red",     0xe06c6c },
    { "green",   0x58d68d },
    { "blue",    0x7c9cff },
    { "teal",    0x4fd1c5 },
    { "violet",  0xb07cff },
};

static uint32_t accent_colour(uint8_t idx)
{
    return k_accents[idx % ACCENT_COUNT].colour;
}

const char *spider_accent_name(uint8_t idx)
{
    return k_accents[idx % ACCENT_COUNT].name;
}

uint32_t spider_accent_colour(uint8_t idx)
{
    return accent_colour(idx);
}

uint8_t spider_accent_count(void)
{
    return ACCENT_COUNT;
}

/* The three accent colours actually in use, with sane defaults. */
static uint32_t pupil_colour(void)
{
    return (g.pupil_colour < ACCENT_COUNT) ? accent_colour(g.pupil_colour)
                                           : 0x000000;
}

static uint32_t leg_colour(void)
{
    return (g.leg_colour < ACCENT_COUNT) ? accent_colour(g.leg_colour)
                                         : 0x8a94a6;
}

static uint32_t fang_colour(void)
{
    return (g.fang_colour < ACCENT_COUNT) ? accent_colour(g.fang_colour)
                                          : 0xf7f9ff;
}

const char *spider_colour_name(uint8_t idx)
{
    return k_palettes[idx % BODY_PALETTE_COUNT].name;
}

/* The palette's mid tone, which is the most representative single colour for a
 * swatch. The setup screen used a hardcoded 0x14171c for every swatch, which is
 * why all six circles looked identical. */
uint32_t spider_colour_swatch(uint8_t idx)
{
    return k_palettes[idx % BODY_PALETTE_COUNT].mid;
}

uint8_t spider_colour_count(void)
{
    return BODY_PALETTE_COUNT;
}
#define COL_EYE_WHITE     0xf7f9ff   /* big white eyes */
#define COL_PUPIL         0x000000   /* pure black pupil */

/* Fuzzy rim: how many dots ring each mass, and how far out they sit. */
#define FUZZ_COUNT        14
#define FUZZ_OUT          2.0f      /* px beyond the body radius */
#define FUZZ_R            3         /* dot radius */

/* Eyes, relative to the cephalothorax centre. Forward is the direction of
 * travel, so the big anterior eyes must sit AHEAD of the head centre (positive
 * forward) with the smaller eyes behind them.
 *
 * The secondary eyes are deliberately DARKER than the primary pair. At the same
 * bright amber they merged with the big eyes into one orange smear behind the
 * face, which read as a colour artefact rather than as eyes. They are also
 * drawn before the primary pair so the big eyes occlude them and only the
 * outer edges peek through. */
#define EYE_FWD       6.0f      /* big anterior eyes, ahead of head centre */
#define EYE_DX        5.5f      /* lateral offset from head centre */
#define EYE_R         7         /* bigger: cartoon eyes dominate the face */
#define PUPIL_R       4         /* bigger pupil keeps the eye from looking blank */

#define EYE_SMALL_R   2

/* Fangs: short, fat triangles pointing forward, drawn along the heading.
 *
 * They used to be round dots at forward 19 with radius 3. The eyes sit at
 * forward 14 with radius 7, so they span forward 7..21 -- which means the dots
 * were drawn ON TOP of the eyeballs. That is the overlap.
 *
 * The fix is to move them clear of the eye front edge (forward 21) and to draw
 * them as triangles rather than dots: a dot reads as a bead, a short fat wedge
 * reads as a fang. FANG_R is the object's half-size, which must be at least
 * FANG_LEN so the triangle is not clipped by the object's own draw area. */
#define FANG_R        6       /* object half-size; must be >= FANG_LEN */
#define FANG_FWD      15.5f   /* base lands just past the eye front edge (21) */
#define FANG_DX       4.0f    /* lateral offset; 4 + half-width 3 leaves a 2px gap */
#define FANG_LEN      4.5f    /* short */
#define FANG_HALF_W   3.0f    /* fat: 6px across the base */
/* Placed BEHIND the primary pair (negative forward) so they clear the big
 * eyes' silhouette and actually read as separate eyes. At positive forward
 * they sat entirely inside the primary eyes and were invisible. */
#define EYE_MID_FWD   (-4.0f)   /* second pair, at the sides of the head */
#define EYE_MID_DX    8.0f
#define EYE_REAR_FWD  (-8.0f)   /* third pair, at the back of the head */
#define EYE_REAR_DX   4.0f

#define COL_EYE_PRIMARY   COL_EYE_WHITE   /* the "face": big and white */
#define COL_EYE_SECONDARY 0x6b7280        /* dim grey, reads as secondary */

/* A body part positioned in body-local coordinates. */
typedef struct {
    lv_obj_t *obj;
    float     fwd;
    float     right;
    int32_t   r;
} part_t;

/* Exact part budget. add_part() silently drops anything past this limit, and
 * the previous value (2*FUZZ_COUNT + 12 = 40) was 4 short of the 44 parts the
 * body actually registers -- so the pupils and both eye glints were being
 * thrown away. That is why the eyes looked pupil-less and why a stray white
 * dot appeared at the body container's origin (0,0): the dropped glints were
 * never positioned, so they sat at the top-left corner.
 *
 *   2 masses + 2 gradient steps each  =  6
 *   2 fuzzy rims x FUZZ_COUNT         = 28
 *   4 secondary eyes                  =  4
 *   2 primary eyes + 2 pupils         =  4
 *   2 glints                          =  2
 *                                  total 44 */
/* 2 masses + 4 gradient steps + 2 fuzzy rims (2 x FUZZ_COUNT) + 4 secondary
 * eyes + 2 primary eyes + 2 pupils + 2 fangs + 2 glints = 2*FUZZ_COUNT + 18.
 * add_part() silently drops anything past this, so it must stay exact. */
#define MAX_PARTS (2 * FUZZ_COUNT + 18)

/* One complete spider anatomy: the body container, every part handle, the leg
 * lines, and the part registry that layout_parts() walks.
 *
 * This is a struct rather than a pile of file-scope statics so the setup screen
 * can build a SECOND, identical anatomy as a live preview. Both come out of
 * anatomy_build(), so the preview cannot drift away from the real spider. The
 * previous preview was a hand-placed approximation and looked nothing like it. */
typedef struct {
    lv_obj_t *body;

    lv_obj_t *abd, *abd_mid, *abd_hi;
    lv_obj_t *ceph, *ceph_mid, *ceph_hi;
    lv_obj_t *fuzz_abd[FUZZ_COUNT];
    lv_obj_t *fuzz_ceph[FUZZ_COUNT];

    lv_obj_t *eye_l, *eye_r;
    lv_obj_t *pup_l, *pup_r;
    lv_obj_t *fang_l, *fang_r;

    lv_obj_t *leg[NUM_LEGS];

    /* lv_line_set_points() stores the pointer, so these MUST outlive the call --
     * LVGL reads them during its own draw pass. */
    lv_point_precise_t leg_pts[NUM_LEGS][3];

    part_t parts[MAX_PARTS];
    int    part_n;

    /* Current heading, in radians. The fang triangles are drawn at draw time
     * and need to know which way to point; parts are positioned by rotating
     * their offset but are not themselves rotated, so a triangle would
     * otherwise always point the same way regardless of travel direction. */
    float  heading;
} anatomy_t;

/* The live spider. */
static anatomy_t s_anat;

/* Short aliases so the render code reads exactly as it did when these were
 * separate file-scope statics. */
#define s_body      (s_anat.body)
#define s_abd       (s_anat.abd)
#define s_abd_mid   (s_anat.abd_mid)
#define s_abd_hi    (s_anat.abd_hi)
#define s_ceph      (s_anat.ceph)
#define s_ceph_mid  (s_anat.ceph_mid)
#define s_ceph_hi   (s_anat.ceph_hi)
#define s_fuzz_abd  (s_anat.fuzz_abd)
#define s_fuzz_ceph (s_anat.fuzz_ceph)
#define s_eye_l     (s_anat.eye_l)
#define s_eye_r     (s_anat.eye_r)
#define s_pup_l     (s_anat.pup_l)
#define s_pup_r     (s_anat.pup_r)
#define s_fang_l    (s_anat.fang_l)
#define s_fang_r    (s_anat.fang_r)
#define s_leg       (s_anat.leg)
#define s_leg_pts   (s_anat.leg_pts)
#define s_parts     (s_anat.parts)
#define s_part_n    (s_anat.part_n)

/* Gait.
 *
 * The phase advances on a steady TIME base while the spider is genuinely
 * translating. Three earlier attempts are worth not repeating:
 *   - A pure time base kept the legs cycling when stationary (SPIDER_PLAY hops
 *     in place; WANDER can be pinned against a wall).
 *   - A state-based gate had the same problem, since state != movement.
 *   - Driving the phase directly from per-frame distance stuttered, because
 *     the position updates at the 20 Hz logic rate while this runs at 30 Hz.
 *
 * GAIT_RATE is the single knob for how fast the legs move.
 */
#define GAIT_RATE     0.30f    /* radians of phase per render frame (30 Hz) */
#define GAIT_SWING    0.42f    /* fore/aft swing amplitude, radians */
#define GAIT_LIFT     0.30f    /* knee-bend oscillation amplitude */

/* Focus ring: built once, toggled with lv_obj_set_hidden. Creating or deleting
 * LVGL objects per frame crashes this project. */
static lv_obj_t *s_focus_ring;

/* Golf-style aim visuals. All persistent, all toggled hidden.
 *   s_aim_arrow  a line from the spider showing the locked/sweeping direction
 *   s_power_bg   the power meter track
 *   s_power_fill the power meter fill
 *   s_power_ideal a marker showing the power that would land on the bug */
static lv_obj_t *s_aim_arrow;
static lv_point_precise_t s_aim_pts[2];
static lv_obj_t *s_power_bg;
static lv_obj_t *s_power_fill;
static lv_obj_t *s_power_ideal;

static bool     s_ready;
static float    s_phase;
static float    s_last_x, s_last_y;   /* previous frame, for distance */
static float    s_speed_smooth;       /* smoothed per-frame displacement */
static bool     s_last_valid;

/* Hop / pounce arc. */
static uint32_t s_arc_start;
static uint32_t s_arc_ms;
static int32_t  s_arc_height;
static bool     s_arc_active;

/* One-shot clip bookkeeping (kept for API compatibility with behaviour.c). */
static bool     s_once_active;
static uint32_t s_once_started;
static uint32_t s_once_duration;

/* Current emotional expression, shown through the legs. */
static spider_emote_t s_emote;

void spider_set_emote(spider_emote_t emote)
{
    s_emote = emote;
}

spider_emote_t spider_emote(void)
{
    return s_emote;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static lv_obj_t *make_dot(lv_obj_t *parent, int32_t x, int32_t y,
                          int32_t r, uint32_t col)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, r * 2, r * 2);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(col), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
    return o;
}

/* Register a body part at a body-local (forward, right) offset. */
static void add_part_to(anatomy_t *a, lv_obj_t *obj,
                        float fwd, float right, int32_t r)
{
    if (!obj || a->part_n >= MAX_PARTS) return;
    a->parts[a->part_n].obj   = obj;
    a->parts[a->part_n].fwd   = fwd;
    a->parts[a->part_n].right = right;
    a->parts[a->part_n].r     = r;
    a->part_n++;
}

/* A fang: a short fat triangle pointing along the body's forward axis.
 *
 * Drawn in the object's own DRAW event, because LVGL has no triangle widget.
 * The object is a transparent square of side 2*FANG_R and the triangle is
 * rotated by the anatomy's current heading here, in the callback. Rotating the
 * points directly avoids depending on LV_USE_TRANSFORM, which is off in this
 * build -- the same reason the body is squashed by moving parts rather than by
 * scaling objects. */
static void fang_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    anatomy_t *a = (anatomy_t *)lv_obj_get_user_data(obj);

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    float cx = (float)(coords.x1 + coords.x2) * 0.5f;
    float cy = (float)(coords.y1 + coords.y2) * 0.5f;

    float h  = a ? a->heading : 0.0f;
    float fx = cosf(h), fy = sinf(h);   /* forward */
    float rx = -fy,     ry = fx;        /* right   */

    /* Triangle in body-local (right, forward) space: a wide base behind, a
     * single apex ahead. Short and fat, so it reads as a fang rather than a
     * spike. */
    static const float lr[3] = { -FANG_HALF_W, +FANG_HALF_W, 0.0f };
    static const float lf[3] = { -FANG_LEN * 0.5f, -FANG_LEN * 0.5f,
                                 +FANG_LEN * 0.5f };

    lv_draw_triangle_dsc_t dsc;
    lv_draw_triangle_dsc_init(&dsc);
    dsc.color = lv_color_hex(fang_colour());
    dsc.opa   = LV_OPA_COVER;

    for (int i = 0; i < 3; i++) {
        dsc.p[i].x = cx + rx * lr[i] + fx * lf[i];
        dsc.p[i].y = cy + ry * lr[i] + fy * lf[i];
    }

    lv_draw_triangle(layer, &dsc);
}

static lv_obj_t *make_fang(lv_obj_t *parent, anatomy_t *a)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, FANG_R * 2, FANG_R * 2);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
    lv_obj_set_user_data(o, a);
    lv_obj_add_event_cb(o, fang_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    return o;
}

/* Place every registered part by rotating its body-local offset through the
 * current heading. This is what makes the head lead the direction of travel.
 *
 * `scale_f` / `scale_r` are 8.8 fixed-point (256 = 1.0) squash/stretch factors
 * along the forward and lateral axes. They are applied to the body-local
 * offsets here because LVGL 9.6 cannot scale an object without LV_USE_MATRIX
 * (which is off), so the anatomy has to be squashed by moving its parts. */
static void layout_parts(anatomy_t *a, float heading,
                         uint32_t scale_f, uint32_t scale_r)
{
    a->heading = heading;

    float fx = cosf(heading), fy = sinf(heading);   /* forward */
    float rx = -fy,           ry = fx;              /* right   */

    float kf = (float)scale_f / 256.0f;
    float kr = (float)scale_r / 256.0f;

    for (int i = 0; i < a->part_n; i++) {
        part_t *p = &a->parts[i];
        if (!p->obj) continue;

        float fwd   = p->fwd   * kf;
        float right = p->right * kr;

        float sx = fx * fwd + rx * right;
        float sy = fy * fwd + ry * right;

        /* Position is relative to the body container, whose centre is BODY_C. */
        lv_obj_set_pos(p->obj,
                       (int32_t)(BODY_C + sx) - p->r,
                       (int32_t)(BODY_C + sy) - p->r);
    }
}

/* ------------------------------------------------------------------ */
/* Anatomy                                                             */
/* ------------------------------------------------------------------ */
/* Build one complete spider into `a`. Used for the live spider and for the
 * setup screen's preview, so the two are guaranteed to be the same creature.
 *
 * `parent` is the object the body container is created in. `leg_parent` is where
 * the leg lines go: the live spider puts them on the screen (they are positioned
 * in screen coordinates each frame), while the preview puts them inside its own
 * box so they are clipped to it. */
static void anatomy_build(anatomy_t *a, lv_obj_t *parent, lv_obj_t *leg_parent,
                          bool pettable)
{
    a->part_n = 0;

    a->body = lv_obj_create(parent);
    lv_obj_remove_style_all(a->body);
    lv_obj_set_size(a->body, BODY_BOX, BODY_BOX);
    lv_obj_set_style_bg_opa(a->body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(a->body, 0, 0);
    lv_obj_set_scrollable(a->body, false);
    /* The body is NOT clickable. It used to carry a pet handler, which competed
     * with the slingshot for the same gesture: a press near the spider was
     * ambiguous between "pet" and "start a pull". The whole tank surface now
     * means one thing -- drag to hunt -- so the body must not swallow presses. */
    lv_obj_set_clickable(a->body, false);
    (void)pettable;

    /* Abdomen first so the cephalothorax overlaps it at the waist.
     *
     * The fuzzy rim is a ring of small dots placed just outside each mass.
     * They are registered as ordinary parts, so they rotate with the body and
     * cost nothing extra per frame -- no new objects are created after init. */
    /* Abdomen: base, then two lighter circles shrinking toward the light
     * source (up-left in body-local space, i.e. negative forward / negative
     * right). Stacking them is how the gradient is faked. */
    a->abd = make_dot(a->body, 0, 0, ABD_R, palette()->base);
    add_part_to(a, a->abd, ABD_FWD, 0.0f, ABD_R);
    a->abd_mid = make_dot(a->body, 0, 0, ABD_R - 3, palette()->mid);
    add_part_to(a, a->abd_mid, ABD_FWD - 1.5f, -1.5f, ABD_R - 3);
    a->abd_hi = make_dot(a->body, 0, 0, ABD_R - 7, palette()->hi);
    add_part_to(a, a->abd_hi, ABD_FWD - 3.0f, -3.0f, ABD_R - 7);

    for (int i = 0; i < FUZZ_COUNT; i++) {
        float ang = (float)i * 2.0f * PI_F / (float)FUZZ_COUNT;
        /* Alternate the dot radius so the rim reads as fur rather than as a
         * perfect ring of beads. */
        int32_t fr = FUZZ_R - (i % 2);
        a->fuzz_abd[i] = make_dot(a->body, 0, 0, fr, palette()->fuzz);
        add_part_to(a, a->fuzz_abd[i],
                    ABD_FWD + cosf(ang) * (float)(ABD_R + FUZZ_OUT),
                    sinf(ang) * (float)(ABD_R + FUZZ_OUT), fr);
    }

    /* Head: same three-step gradient, slightly lighter overall so the two
     * masses stay distinguishable where they overlap at the waist. */
    a->ceph = make_dot(a->body, 0, 0, CEPH_R, palette()->base);
    add_part_to(a, a->ceph, CEPH_FWD, 0.0f, CEPH_R);
    a->ceph_mid = make_dot(a->body, 0, 0, CEPH_R - 3, palette()->mid);
    add_part_to(a, a->ceph_mid, CEPH_FWD - 1.5f, -1.5f, CEPH_R - 3);
    a->ceph_hi = make_dot(a->body, 0, 0, CEPH_R - 6, palette()->hi);
    add_part_to(a, a->ceph_hi, CEPH_FWD - 3.0f, -3.0f, CEPH_R - 6);

    for (int i = 0; i < FUZZ_COUNT; i++) {
        float ang = (float)i * 2.0f * PI_F / (float)FUZZ_COUNT;
        int32_t fr = FUZZ_R - (i % 2);
        a->fuzz_ceph[i] = make_dot(a->body, 0, 0, fr, palette()->fuzz);
        add_part_to(a, a->fuzz_ceph[i],
                    CEPH_FWD + cosf(ang) * (float)(CEPH_R + FUZZ_OUT),
                    sinf(ang) * (float)(CEPH_R + FUZZ_OUT), fr);
    }

    /* Secondary eyes FIRST, so the big primary pair is drawn on top of them and
     * occludes their inner halves. Dim amber keeps them reading as secondary
     * rather than as a second bright face. */
    add_part_to(a, make_dot(a->body, 0, 0, EYE_SMALL_R, COL_EYE_SECONDARY),
                CEPH_FWD + EYE_MID_FWD, -EYE_MID_DX, EYE_SMALL_R);
    add_part_to(a, make_dot(a->body, 0, 0, EYE_SMALL_R, COL_EYE_SECONDARY),
                CEPH_FWD + EYE_MID_FWD, +EYE_MID_DX, EYE_SMALL_R);
    add_part_to(a, make_dot(a->body, 0, 0, EYE_SMALL_R, COL_EYE_SECONDARY),
                CEPH_FWD + EYE_REAR_FWD, -EYE_REAR_DX, EYE_SMALL_R);
    add_part_to(a, make_dot(a->body, 0, 0, EYE_SMALL_R, COL_EYE_SECONDARY),
                CEPH_FWD + EYE_REAR_FWD, +EYE_REAR_DX, EYE_SMALL_R);

    /* Oversized cartoon eyes. The big anterior pair sits AHEAD of the head
     * centre so the face leads. */
    a->eye_l = make_dot(a->body, 0, 0, EYE_R, COL_EYE_PRIMARY);
    a->eye_r = make_dot(a->body, 0, 0, EYE_R, COL_EYE_PRIMARY);
    add_part_to(a, a->eye_l, CEPH_FWD + EYE_FWD, -EYE_DX, EYE_R);
    add_part_to(a, a->eye_r, CEPH_FWD + EYE_FWD, +EYE_DX, EYE_R);

    /* Pupils sit slightly forward in each eye, which reads as a focused
     * forward gaze rather than a dead centre stare. */
    a->pup_l = make_dot(a->body, 0, 0, PUPIL_R, pupil_colour());
    a->pup_r = make_dot(a->body, 0, 0, PUPIL_R, pupil_colour());
    add_part_to(a, a->pup_l, CEPH_FWD + EYE_FWD + 1.0f, -EYE_DX, PUPIL_R);
    add_part_to(a, a->pup_r, CEPH_FWD + EYE_FWD + 1.0f, +EYE_DX, PUPIL_R);

    /* Fangs: short fat triangles, clear of the eye front edge. */
    a->fang_l = make_fang(a->body, a);
    a->fang_r = make_fang(a->body, a);
    add_part_to(a, a->fang_l, CEPH_FWD + FANG_FWD, -FANG_DX, FANG_R);
    add_part_to(a, a->fang_r, CEPH_FWD + FANG_FWD, +FANG_DX, FANG_R);

    /* Specular glints: the single cheapest trick that makes an eye look alive. */
    add_part_to(a, make_dot(a->body, 0, 0, 1, 0xffffff),
                CEPH_FWD + EYE_FWD + 2.0f, -EYE_DX - 2.0f, 1);
    add_part_to(a, make_dot(a->body, 0, 0, 1, 0xffffff),
                CEPH_FWD + EYE_FWD + 2.0f, +EYE_DX - 2.0f, 1);

    /* Eight jointed legs, drawn as 3-point polylines (hip -> knee -> foot). */
    for (int i = 0; i < NUM_LEGS; i++) {
        lv_obj_t *line = lv_line_create(leg_parent);
        lv_obj_set_style_line_width(line, LEG_WIDTH, 0);
        lv_obj_set_style_line_color(line, lv_color_hex(leg_colour()), 0);
        lv_obj_set_style_line_opa(line, LV_OPA_COVER, 0);
        lv_obj_set_style_line_rounded(line, true, 0);
        lv_obj_set_clickable(line, false);
        lv_obj_set_scrollable(line, false);
        lv_obj_set_hidden(line, true);
        a->leg[i] = line;
    }
}

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */
void spider_init(lv_obj_t *parent)
{
    /* The creature itself is drawn by cspider.c into a single canvas. This
     * module now owns only the slingshot aim visuals, which are genuinely
     * widget-shaped: a line whose points change every frame and a set of
     * hidden power-meter bars.
     *
     * The anatomy builder is still used, but only for the setup-screen preview
     * (spider_preview_init), which needs a static spider in a small box. */

    /* Focus ring: the aim-window telegraph. Persistent object, hidden by
     * default; spider_render only moves it and sets its value. */
    s_focus_ring = lv_arc_create(parent);
    lv_obj_set_size(s_focus_ring, 76, 76);
    lv_obj_set_style_bg_opa(s_focus_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_focus_ring, 0, 0);
    lv_obj_set_style_arc_width(s_focus_ring, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_focus_ring, lv_color_hex(0x2a2f3a), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_focus_ring, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_focus_ring, lv_color_hex(0xf5b342), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_focus_ring, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_clickable(s_focus_ring, false);
    lv_obj_set_scrollable(s_focus_ring, false);
    lv_arc_set_range(s_focus_ring, 0, 100);
    lv_arc_set_bg_angles(s_focus_ring, 0, 360);
    lv_obj_set_hidden(s_focus_ring, true);

    /* --- golf-style aim visuals ------------------------------------- */

    /* Direction arrow: a thick line from the spider outward. */
    s_aim_arrow = lv_line_create(parent);
    lv_obj_set_style_line_width(s_aim_arrow, 4, 0);
    lv_obj_set_style_line_color(s_aim_arrow, lv_color_hex(0xf5b342), 0);
    lv_obj_set_style_line_opa(s_aim_arrow, LV_OPA_COVER, 0);
    lv_obj_set_style_line_rounded(s_aim_arrow, true, 0);
    lv_obj_set_clickable(s_aim_arrow, false);
    lv_obj_set_scrollable(s_aim_arrow, false);
    lv_obj_set_hidden(s_aim_arrow, true);

    /* Power meter: a track, a fill, and an ideal marker. Horizontal, centred
     * near the bottom of the playfield where a thumb is not covering it. */
    const int32_t pw = 220;
    const int32_t ph = 14;
    const int32_t px = (ui.W - pw) / 2;
    const int32_t py = ui.H - HUD_BOTTOM_H - 34;

    s_power_bg = lv_obj_create(parent);
    lv_obj_remove_style_all(s_power_bg);
    lv_obj_set_size(s_power_bg, pw, ph);
    lv_obj_set_pos(s_power_bg, px, py);
    lv_obj_set_style_bg_color(s_power_bg, lv_color_hex(0x1e232b), 0);
    lv_obj_set_style_bg_opa(s_power_bg, LV_OPA_90, 0);
    lv_obj_set_style_radius(s_power_bg, ph / 2, 0);
    lv_obj_set_style_border_width(s_power_bg, 1, 0);
    lv_obj_set_style_border_color(s_power_bg, lv_color_hex(0x2a2f3a), 0);
    lv_obj_set_clickable(s_power_bg, false);
    lv_obj_set_scrollable(s_power_bg, false);
    lv_obj_set_hidden(s_power_bg, true);

    s_power_fill = lv_obj_create(parent);
    lv_obj_remove_style_all(s_power_fill);
    lv_obj_set_size(s_power_fill, 0, ph - 4);
    lv_obj_set_pos(s_power_fill, px + 2, py + 2);
    lv_obj_set_style_bg_color(s_power_fill, lv_color_hex(0x58d68d), 0);
    lv_obj_set_style_bg_opa(s_power_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_power_fill, (ph - 4) / 2, 0);
    lv_obj_set_clickable(s_power_fill, false);
    lv_obj_set_scrollable(s_power_fill, false);
    lv_obj_set_hidden(s_power_fill, true);

    /* The ideal marker: where the player should stop the meter. */
    s_power_ideal = lv_obj_create(parent);
    lv_obj_remove_style_all(s_power_ideal);
    lv_obj_set_size(s_power_ideal, 3, ph + 8);
    lv_obj_set_pos(s_power_ideal, px, py - 4);
    lv_obj_set_style_bg_color(s_power_ideal, lv_color_hex(0xf5b342), 0);
    lv_obj_set_style_bg_opa(s_power_ideal, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_power_ideal, 1, 0);
    lv_obj_set_clickable(s_power_ideal, false);
    lv_obj_set_scrollable(s_power_ideal, false);
    lv_obj_set_hidden(s_power_ideal, true);

    s_phase = 0.0f;
    s_last_valid = false;   /* first frame establishes the baseline */
    s_speed_smooth = 0.0f;
    s_ready = true;
}

/* Re-apply the stored colours to the live objects. The setup screen calls this
 * after a picker changes so the player sees the result immediately, and the
 * factory reset calls it so a new tank does not inherit the old palette. */
void spider_refresh_colours(void)
{
    const body_palette_t *p = palette();

    if (s_abd)      lv_obj_set_style_bg_color(s_abd,      lv_color_hex(p->base), 0);
    if (s_abd_mid)  lv_obj_set_style_bg_color(s_abd_mid,  lv_color_hex(p->mid),  0);
    if (s_abd_hi)   lv_obj_set_style_bg_color(s_abd_hi,   lv_color_hex(p->hi),   0);
    if (s_ceph)     lv_obj_set_style_bg_color(s_ceph,     lv_color_hex(p->base), 0);
    if (s_ceph_mid) lv_obj_set_style_bg_color(s_ceph_mid, lv_color_hex(p->mid),  0);
    if (s_ceph_hi)  lv_obj_set_style_bg_color(s_ceph_hi,  lv_color_hex(p->hi),   0);

    for (int i = 0; i < FUZZ_COUNT; i++) {
        if (s_fuzz_abd[i])
            lv_obj_set_style_bg_color(s_fuzz_abd[i], lv_color_hex(p->fuzz), 0);
        if (s_fuzz_ceph[i])
            lv_obj_set_style_bg_color(s_fuzz_ceph[i], lv_color_hex(p->fuzz), 0);
    }

    if (s_pup_l) lv_obj_set_style_bg_color(s_pup_l, lv_color_hex(pupil_colour()), 0);
    if (s_pup_r) lv_obj_set_style_bg_color(s_pup_r, lv_color_hex(pupil_colour()), 0);
    /* Fangs are drawn in a DRAW callback, not filled, so they have no background
     * colour to set -- invalidate them so the callback runs again with the new
     * colour. */
    if (s_fang_l) lv_obj_invalidate(s_fang_l);
    if (s_fang_r) lv_obj_invalidate(s_fang_r);

    for (int i = 0; i < NUM_LEGS; i++) {
        if (s_leg[i]) {
            lv_obj_set_style_line_color(s_leg[i], lv_color_hex(leg_colour()), 0);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Preview                                                             */
/* ------------------------------------------------------------------ */
/* A second, static anatomy for the setup screen. It is built by the same
 * anatomy_build() as the live spider, so it is the real creature rather than an
 * approximation -- the previous preview was hand-placed and looked nothing like
 * the spider it was supposed to preview.
 *
 * The preview is drawn at 1:1 and then scaled up by placing its body container
 * and leg lines inside a box that LVGL scales via the object's own transform.
 * LV_USE_MATRIX is off, so instead the anatomy is simply built at its natural
 * size and the box is sized to fit it. */

static anatomy_t s_preview;
static bool      s_preview_ready;

void spider_preview_init(lv_obj_t *parent)
{
    /* Legs go inside the body container here, not on the screen: the preview
     * never moves, so screen coordinates would be wrong. */
    anatomy_build(&s_preview, parent, parent, false);

    /* Lay the anatomy out once, facing up, at its natural size. */
    layout_parts(&s_preview, -PI_F * 0.5f, 256, 256);

    /* Legs: a static standing pose. The live spider recomputes these every
     * frame from its gait; the preview just needs one readable silhouette. */
    for (int i = 0; i < NUM_LEGS; i++) {
        int side = (i < 4) ? -1 : 1;
        int k    = i & 3;

        float off  = (float)(k - 1.5f) * 0.42f;
        float base = -PI_F * 0.5f + (float)side * (PI_F * 0.5f + off);

        float bend = 0.55f;
        float dir1 = base + (float)side * bend;
        float dir2 = base - (float)side * bend;

        float hx = (float)BODY_C + (float)BODY_R * cosf(base);
        float hy = (float)BODY_C + (float)BODY_R * sinf(base);
        float kx = hx + LEG_LEN * LEG_SEG1 * cosf(dir1);
        float ky = hy + LEG_LEN * LEG_SEG1 * sinf(dir1);
        float fx = kx + LEG_LEN * LEG_SEG2 * cosf(dir2);
        float fy = ky + LEG_LEN * LEG_SEG2 * sinf(dir2);

        float minx = hx < kx ? hx : kx; if (fx < minx) minx = fx;
        float miny = hy < ky ? hy : ky; if (fy < miny) miny = fy;

        lv_obj_set_pos(s_preview.leg[i], (int32_t)minx, (int32_t)miny);

        s_preview.leg_pts[i][0].x = (lv_value_precise_t)(hx - minx);
        s_preview.leg_pts[i][0].y = (lv_value_precise_t)(hy - miny);
        s_preview.leg_pts[i][1].x = (lv_value_precise_t)(kx - minx);
        s_preview.leg_pts[i][1].y = (lv_value_precise_t)(ky - miny);
        s_preview.leg_pts[i][2].x = (lv_value_precise_t)(fx - minx);
        s_preview.leg_pts[i][2].y = (lv_value_precise_t)(fy - miny);

        lv_line_set_points(s_preview.leg[i], s_preview.leg_pts[i], 3);
        lv_obj_set_hidden(s_preview.leg[i], false);
    }

    /* Centre the anatomy in its parent.
     *
     * The anatomy is built at its natural size with the body container at the
     * parent's origin, so it sits in the top-left corner. Its true extent is
     * larger than BODY_BOX because the legs reach past it:
     *
     *   reach = BODY_R + LEG_LEN * (LEG_SEG1 + LEG_SEG2)
     *         = 16 + 22 * 1.30 = 44.6 px from the body centre
     *
     * so it spans centre +/- 44.6, i.e. about 89 px, while the body container's
     * origin sits at centre - BODY_C = -10.6. Shifting every child by the
     * difference centres the whole creature rather than just its body box. */
    lv_obj_update_layout(parent);
    int32_t pw = lv_obj_get_content_width(parent);
    int32_t ph = lv_obj_get_content_height(parent);

    const float reach = (float)BODY_R + LEG_LEN * (LEG_SEG1 + LEG_SEG2);
    const float span  = reach * 2.0f;
    const float origin = (float)BODY_C - reach;   /* anatomy left edge */

    int32_t off_x = (int32_t)(((float)pw - span) * 0.5f - origin);
    int32_t off_y = (int32_t)(((float)ph - span) * 0.5f - origin);

    uint32_t n = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(parent, (int32_t)i);
        if (!c) continue;
        lv_obj_set_pos(c, lv_obj_get_x(c) + off_x, lv_obj_get_y(c) + off_y);
    }

    s_preview_ready = true;
}

void spider_preview_refresh(void)
{
    if (!s_preview_ready) return;

    const body_palette_t *p = palette();

    lv_obj_set_style_bg_color(s_preview.abd,      lv_color_hex(p->base), 0);
    lv_obj_set_style_bg_color(s_preview.abd_mid,  lv_color_hex(p->mid),  0);
    lv_obj_set_style_bg_color(s_preview.abd_hi,   lv_color_hex(p->hi),   0);
    lv_obj_set_style_bg_color(s_preview.ceph,     lv_color_hex(p->base), 0);
    lv_obj_set_style_bg_color(s_preview.ceph_mid, lv_color_hex(p->mid),  0);
    lv_obj_set_style_bg_color(s_preview.ceph_hi,  lv_color_hex(p->hi),   0);

    for (int i = 0; i < FUZZ_COUNT; i++) {
        lv_obj_set_style_bg_color(s_preview.fuzz_abd[i],
                                  lv_color_hex(p->fuzz), 0);
        lv_obj_set_style_bg_color(s_preview.fuzz_ceph[i],
                                  lv_color_hex(p->fuzz), 0);
    }

    lv_obj_set_style_bg_color(s_preview.pup_l, lv_color_hex(pupil_colour()), 0);
    lv_obj_set_style_bg_color(s_preview.pup_r, lv_color_hex(pupil_colour()), 0);
    lv_obj_invalidate(s_preview.fang_l);
    lv_obj_invalidate(s_preview.fang_r);

    for (int i = 0; i < NUM_LEGS; i++) {
        lv_obj_set_style_line_color(s_preview.leg[i],
                                    lv_color_hex(leg_colour()), 0);
    }
}

/* ------------------------------------------------------------------ */
/* Arc / one-shots                                                     */
/* ------------------------------------------------------------------ */
void spider_launch_pounce(int32_t height, uint32_t ms)
{
    s_arc_start  = lv_tick_get();
    s_arc_ms     = ms;
    s_arc_height = height;
    s_arc_active = true;
}

void spider_play_once(spider_anim_t anim, bool force)
{
    if (s_once_active && !force) return;

    uint32_t ms = 0;
    switch (anim) {
    case ANIM_POUNCE: ms = 180; break;
    case ANIM_MOLT:   ms = 800; break;
    default:          ms = 0;   break;
    }

    if (ms == 0) return;

    s_once_active   = true;
    s_once_started  = lv_tick_get();
    s_once_duration = ms;
}

bool spider_oneshot_active(void)
{
    return s_once_active;
}

int32_t spider_render_dy(void)
{
    if (!s_arc_active) return 0;

    uint32_t now = lv_tick_get();
    uint32_t elapsed = now - s_arc_start;
    if (elapsed >= s_arc_ms) {
        s_arc_active = false;
        return 0;
    }

    /* Parabolic arc: 4h * t * (1 - t). */
    float t = (float)elapsed / (float)s_arc_ms;
    float h = 4.0f * (float)s_arc_height * t * (1.0f - t);
    return -(int32_t)h;
}

/* ------------------------------------------------------------------ */
/* Render                                                              */
/* ------------------------------------------------------------------ */
void spider_render(uint32_t now_ms)
{
    if (!s_ready) return;

    if (s_once_active && (now_ms - s_once_started) >= s_once_duration) {
        s_once_active = false;
    }

    int32_t dy = spider_render_dy();

    /* The focus-ring countdown is gone. It drained over the spider's crouch
     * window, which used to be the only time the player could shoot -- so it
     * read as "hurry up". The slingshot is now always available, so there is
     * nothing to count down. The ring object is still created (below) but is
     * never shown; removing the object entirely would mean touching the part
     * budget for no benefit. */
    if (s_focus_ring) lv_obj_set_hidden(s_focus_ring, true);

    /* --- slingshot aim visuals ---------------------------------------
     *
     * While the player pulls, draw a line from the spider to the finger (the
     * "band") and a second line in the OPPOSITE direction showing where the
     * shot will go. The forward line's length is the power, so the player can
     * see the shot before releasing it.
     *
     * The power meter is gone: with direct manipulation the pull IS the meter,
     * and a separate bar was a second thing to read for no benefit. */
    {
        bool aiming   = behaviour_aim_active();
        bool dragging = behaviour_aim_dragging();

        lv_obj_set_hidden(s_aim_arrow, !dragging);
        lv_obj_set_hidden(s_power_bg, true);
        lv_obj_set_hidden(s_power_fill, true);
        lv_obj_set_hidden(s_power_ideal, true);

        if (dragging) {
            float dx, dyv;
            behaviour_aim_drag_vec(&dx, &dyv);
            float d = sqrtf(dx * dx + dyv * dyv);

            if (d > 1.0f) {
                /* The shot fires TOWARD the finger, matching the drag-toward
                 * gesture. The arrow therefore points the same way the player
                 * is dragging, which is also the direction the spider will
                 * leap -- so the visual and the outcome agree. */
                float ux = dx / d;
                float uy = dyv / d;

                float max_px = POUNCE_MAX_PX;
                float len = d;
                if (len > max_px) len = max_px;

                int32_t x0 = g.spider_x;
                int32_t y0 = g.spider_y + dy;
                int32_t x1 = x0 + (int32_t)(ux * len);
                int32_t y1 = y0 + (int32_t)(uy * len);

                int32_t minx = (x0 < x1) ? x0 : x1;
                int32_t miny = (y0 < y1) ? y0 : y1;
                lv_obj_set_pos(s_aim_arrow, minx, miny);
                s_aim_pts[0].x = (lv_value_precise_t)(x0 - minx);
                s_aim_pts[0].y = (lv_value_precise_t)(y0 - miny);
                s_aim_pts[1].x = (lv_value_precise_t)(x1 - minx);
                s_aim_pts[1].y = (lv_value_precise_t)(y1 - miny);
                lv_line_set_points(s_aim_arrow, s_aim_pts, 2);

                /* Colour the band by how close the pull is to the ideal, so
                 * the player gets feedback without reading a number. */
                float ideal = behaviour_aim_ideal_power();
                float pull  = behaviour_aim_pull();
                float err   = fabsf(pull - ideal);
                uint32_t col = (err < 0.10f) ? 0x58d68d      /* green: on target */
                             : (err < 0.22f) ? 0xf5b342      /* amber: close */
                                             : 0xe06c6c;     /* red: off */
                lv_obj_set_style_line_color(s_aim_arrow, lv_color_hex(col), 0);
            }
        }
        (void)aiming;
    }
}
