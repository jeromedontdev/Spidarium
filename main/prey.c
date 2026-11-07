/*
 * Spidarium — prey.
 *
 * Drawn procedurally rather than from sprites. The sprite set is authored
 * facing right and LVGL 9.6 cannot mirror an image (scale_x is unsigned and
 * LV_USE_MATRIX is off), so a sprite-based bug always appeared to travel
 * backwards whenever it moved left. Building each bug from a few primitives
 * positioned in body-local coordinates lets it face its actual heading.
 */
#include "prey.h"

#include "audio.h"
#include "behaviour.h"
#include "game_config.h"
#include "species.h"
#include "terrarium.h"

#include "esp_log.h"

#include <math.h>
#include <stdlib.h>

static const char *TAG = "spidarium.prey";

#define PI_F 3.14159265f

/* Body-local part offsets, in (forward, right) pixels. */
/* Exact part budget. add_part() silently drops anything past this limit, and
 * the old value of 8 was too small for the cricket, which registers 11:
 *
 *   fly      2 wings + body + head            =  4
 *   cricket  body + head + 8 hind legs        = 10
 *   moth     2 wings + body + head            =  4
 *   beetle   body + head + seam + 4 legs      =  7
 *   splat    4 smear + 8 legs + 3 droplets    = 15
 *
 * The cricket's last three legs were therefore being thrown away, which is why
 * crickets looked wrong. This is the same silent-truncation bug that ate the
 * spider's pupils and glints. The splat is the largest at 15, so 16 is the
 * tightest safe value. */
#define MAX_PARTS 16

typedef struct {
    lv_obj_t *obj;
    float     fwd;
    float     right;
    int32_t   r;
} part_t;

typedef struct {
    bool           alive;
    bool           dead;
    bool           fleeing;
    bool           wild;        /* drifted in on its own: leaner, warier */
    prey_species_t species;
    float          x, y;
    float          heading;
    uint32_t       retarget_at;
    float          wander_dir;
    uint32_t       frame_at;
    int            frame;
    uint32_t       dead_at;
    lv_obj_t      *root;        /* container, positioned at the bug centre */
    part_t         parts[MAX_PARTS];
    int            part_n;
} prey_t;

static prey_t s_prey[MAX_PREY];

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static float wrap_pi(float a)
{
    while (a > PI_F)  a -= 2.0f * PI_F;
    while (a < -PI_F) a += 2.0f * PI_F;
    return a;
}

static float randf(void)
{
    return (float)(rand() % 10000) / 10000.0f;
}

static void clamp_prey(prey_t *p)
{
    if (p->x < (float)g_play.left)   p->x = (float)g_play.left;
    if (p->x > (float)g_play.right)  p->x = (float)g_play.right;
    if (p->y < (float)g_play.top)    p->y = (float)g_play.top;
    if (p->y > (float)g_play.bottom) p->y = (float)g_play.bottom;
}

static lv_obj_t *make_dot(lv_obj_t *parent, int32_t r, uint32_t col)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, r * 2, r * 2);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(col), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
    return o;
}

static void add_part(prey_t *p, int32_t r, uint32_t col, float fwd, float right)
{
    if (p->part_n >= MAX_PARTS) return;

    lv_obj_t *o = make_dot(p->root, r, col);
    p->parts[p->part_n].obj   = o;
    p->parts[p->part_n].fwd   = fwd;
    p->parts[p->part_n].right = right;
    p->parts[p->part_n].r     = r;
    p->part_n++;
}

/* ------------------------------------------------------------------ */
/* Body construction                                                   */
/* ------------------------------------------------------------------ */
static void build_body(prey_t *p)
{
    p->part_n = 0;

    /* Root container: a fixed-size transparent box the parts live in.
     *
     * Created once per slot and reused. It is only ever destroyed by prey_tick
     * when a corpse expires, and that path nulls the pointer, so a non-NULL
     * root here is always a live object.
     *
     * The guard below is belt-and-braces: if a root is ever destroyed without
     * nulling the pointer, lv_obj_clean() on it corrupts LVGL's event list and
     * the device dies with "LoadProhibited in lv_event_mark_deleted". Checking
     * the object is still valid costs one call and turns a crash into a
     * rebuild. */
    if (p->root && !lv_obj_is_valid(p->root)) {
        ESP_LOGW(TAG, "prey %d: stale root, rebuilding", (int)(p - s_prey));
        p->root = NULL;
    }

    if (!p->root) {
        p->root = lv_obj_create(ui.screen);
        lv_obj_remove_style_all(p->root);
        lv_obj_set_size(p->root, PREY_SPRITE_W, PREY_SPRITE_H);
        lv_obj_set_style_bg_opa(p->root, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(p->root, 0, 0);
        lv_obj_set_scrollable(p->root, false);
        lv_obj_set_clickable(p->root, false);
    }

    /* Clear any parts from a previous species. */
    lv_obj_clean(p->root);

    /* Everything about the body comes from the species table, so a new bug is
     * one row in species.c rather than a new case here. */
    const species_def_t *sp = species_get(p->species);

    const uint32_t body = sp->body_col;
    const uint32_t dark = sp->dark_col;
    const uint32_t wing = sp->wing_col;
    const uint32_t legc = 0x2e271f;

    if (p->dead) {
        /* SPLAT.
         *
         * The old corpse was a tidy bug lying on its back, which at this size
         * read as "the bug is still there, just rotated" -- the player could not
         * tell a kill from a bug that had wandered off. A splat is unambiguous:
         * a wide flattened smear with legs splayed outward and a few droplets
         * thrown clear.
         *
         * Everything is drawn flat (no rotation) because a splat has no
         * orientation. */
        const uint32_t goo = sp->body_col;

        /* The smear: a wide, low body plus two lobes so it reads as squashed
         * rather than as a circle. */
        add_part(p, 7, goo,  0.0f,  0.0f);
        add_part(p, 5, goo,  0.0f, -7.0f);
        add_part(p, 5, goo,  0.0f, +7.0f);
        add_part(p, 4, dark, 0.0f,  0.0f);

        /* Legs splayed outward from the centre, all at different angles. */
        static const float splat_leg[8][2] = {
            {  7.0f, -9.0f }, {  7.0f,  9.0f },
            {  2.0f, -11.0f }, {  2.0f,  11.0f },
            { -4.0f, -10.0f }, { -4.0f,  10.0f },
            { -8.0f, -6.0f }, { -8.0f,  6.0f },
        };
        for (int i = 0; i < 8; i++) {
            add_part(p, 1, legc, splat_leg[i][0], splat_leg[i][1]);
        }

        /* Droplets thrown clear of the smear. */
        add_part(p, 1, goo,  10.0f, -4.0f);
        add_part(p, 1, goo, -10.0f,  5.0f);
        add_part(p, 1, goo,   4.0f, 12.0f);

        return;
    }

    /* Wings first so the body sits on top of them. */
    if (sp->has_wings) {
        add_part(p, sp->body_r + 1, wing, -1.0f, -6.0f);
        add_part(p, sp->body_r + 1, wing, -1.0f, +6.0f);
    }

    /* Body and head. */
    add_part(p, sp->body_r, body, 0.0f, 0.0f);
    add_part(p, sp->head_r, dark, 6.0f, 0.0f);

    /* Beetle wing-case seam. */
    if (sp->has_seam) {
        add_part(p, 1, 0x14110e, 0.0f, 0.0f);
    }

    /* Cricket hind legs, swept back. */
    if (sp->has_hind_legs) {
        add_part(p, 2, legc, -6.0f, -6.0f);
        add_part(p, 2, legc, -6.0f, +6.0f);
        add_part(p, 1, legc, 2.0f, -5.0f);
        add_part(p, 1, legc, 2.0f, +5.0f);
    } else if (sp->has_seam) {
        /* Beetle: four short legs. */
        add_part(p, 1, legc, 3.0f, -6.0f);
        add_part(p, 1, legc, 3.0f, +6.0f);
        add_part(p, 1, legc, -3.0f, -6.0f);
        add_part(p, 1, legc, -3.0f, +6.0f);
    }
}

/* Place every part by rotating its body-local offset through the heading. */
static void layout_body(prey_t *p)
{
    float fx = cosf(p->heading), fy = sinf(p->heading);
    float rx = -fy,              ry = fx;

    const float c = (float)PREY_SPRITE_W * 0.5f;

    for (int i = 0; i < p->part_n; i++) {
        part_t *q = &p->parts[i];
        if (!q->obj) continue;

        float sx = fx * q->fwd + rx * q->right;
        float sy = fy * q->fwd + ry * q->right;

        lv_obj_set_pos(q->obj,
                       (int32_t)(c + sx) - q->r,
                       (int32_t)(c + sy) - q->r);
    }
}

/* ------------------------------------------------------------------ */
/* Spawning                                                            */
/* ------------------------------------------------------------------ */
int prey_unlocked_species(void)
{
    /* COUNT of species unlocked at the current level. Note this is a count,
     * not an index -- see species_next_locked() for the next one to unlock. */
    int n = 0;
    for (int i = 0; i < PREY_SPECIES_COUNT; i++) {
        if (g.level >= k_species[i].unlock_level) n++;
    }
    return (n > 0) ? n : 1;
}

int prey_next_locked_species(void)
{
    return species_next_locked();
}

const char *prey_species_name(prey_species_t species)
{
    return species_get(species)->name;
}

uint32_t prey_unlock_level(prey_species_t species)
{
    return species_get(species)->unlock_level;
}

void prey_spawn_species_ex(prey_species_t species, bool wild)
{
    int slot = -1;
    for (int i = 0; i < MAX_PREY; i++) {
        if (!s_prey[i].alive) { slot = i; break; }
    }
    if (slot < 0) return;

    prey_t *p = &s_prey[slot];
    p->alive       = true;
    p->dead        = false;
    p->fleeing     = false;
    p->wild        = wild;
    p->species     = species;
    p->frame       = 0;
    p->frame_at    = lv_tick_get();
    p->retarget_at = 0;
    p->dead_at     = 0;
    p->wander_dir  = randf() * 2.0f * PI_F - PI_F;
    p->heading     = p->wander_dir;

    /* Spawn away from the spider so it is not instantly caught. */
    for (int attempt = 0; attempt < 12; attempt++) {
        p->x = (float)(g_play.left + rand() % (g_play.right - g_play.left));
        p->y = (float)(g_play.top + rand() % (g_play.bottom - g_play.top));
        float dx = p->x - (float)g.spider_x;
        float dy = p->y - (float)g.spider_y;
        if (dx * dx + dy * dy > 90.0f * 90.0f) break;
    }

    build_body(p);
    lv_obj_set_hidden(p->root, false);
    layout_body(p);
    lv_obj_set_pos(p->root, (int32_t)p->x - PREY_PIVOT_X,
                   (int32_t)p->y - PREY_PIVOT_Y);
}

void prey_spawn_species(prey_species_t species)
{
    prey_spawn_species_ex(species, false);
}

void prey_spawn(void)
{
    int n = prey_unlocked_species();
    prey_spawn_species((prey_species_t)(rand() % n));
}

void prey_spawn_wild(void)
{
    int n = prey_unlocked_species();
    prey_spawn_species_ex((prey_species_t)(rand() % n), true);
}

void prey_clear(void)
{
    for (int i = 0; i < MAX_PREY; i++) {
        if (s_prey[i].root) {
            lv_obj_del(s_prey[i].root);
            s_prey[i].root = NULL;
        }
        /* Clear the part table as well.
         *
         * The parts are children of root, so deleting root frees them. Leaving
         * the pointers behind meant a later build_body() for this slot could
         * touch freed objects -- a use-after-free that corrupted LVGL's event
         * list and crashed in lv_event_mark_deleted. */
        for (int k = 0; k < s_prey[i].part_n; k++) {
            s_prey[i].parts[k].obj = NULL;
        }
        s_prey[i].alive = false;
        s_prey[i].dead = false;
        s_prey[i].dead_at = 0;
        s_prey[i].part_n = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Queries                                                             */
/* ------------------------------------------------------------------ */
int prey_count(void)
{
    int n = 0;
    for (int i = 0; i < MAX_PREY; i++) {
        if (s_prey[i].alive && !s_prey[i].dead) n++;
    }
    return n;
}

bool prey_nearest(int32_t *x, int32_t *y)
{
    float best = 1e9f;
    int found = -1;

    for (int i = 0; i < MAX_PREY; i++) {
        if (!s_prey[i].alive || s_prey[i].dead) continue;
        float dx = s_prey[i].x - (float)g.spider_x;
        float dy = s_prey[i].y - (float)g.spider_y;
        float d2 = dx * dx + dy * dy;
        if (d2 < best) { best = d2; found = i; }
    }

    if (found < 0) return false;
    if (x) *x = (int32_t)s_prey[found].x;
    if (y) *y = (int32_t)s_prey[found].y;
    return true;
}

/* ------------------------------------------------------------------ */
/* Catch resolution                                                    */
/* ------------------------------------------------------------------ */
typedef enum { CATCH_MISS = 0, CATCH_GOOD, CATCH_PERFECT } catch_quality_t;

void prey_resolve_pounce(void)
{
    int best = -1;
    float best_d = 1e9f;

    /* Resolve against the bug nearest to where the spider LANDED.
     *
     * g.spider_x/g.spider_y are the landing position by the time this runs, so
     * this is already the right measurement -- but the old code then judged the
     * shot by behaviour_aim_quality(), which recomputes the ideal power from
     * prey_nearest(). With more than one bug on screen those two disagree: the
     * player aims at the far bug, lands on it, and the quality is computed
     * against the near bug, so a perfect landing scores zero and reads as a
     * miss. That is the "hit box on bugs is bad" complaint.
     *
     * The catch is now decided by GEOMETRY -- did the body land on the bug --
     * with aim quality as a bonus rather than a gate. Geometry is what the
     * player can see, so it is what the game should judge. */
    for (int i = 0; i < MAX_PREY; i++) {
        if (!s_prey[i].alive || s_prey[i].dead) continue;
        float dx = s_prey[i].x - (float)g.spider_x;
        float dy = s_prey[i].y - (float)g.spider_y;
        float d = sqrtf(dx * dx + dy * dy);
        if (d < best_d) { best_d = d; best = i; }
    }

    if (best < 0) return;

    /* Measure from the EDGE of the spider, not its centre.
     *
     * The spider's body is ~40 px across. Measuring centre-to-bug meant only
     * the mouth counted: landing the abdomen or a flank on the bug still
     * measured 20-30 px from the bug centre, which fell outside the perfect
     * band. Subtracting the body radius means landing ANY part of the body on
     * the bug counts, which is what the player expects when they see the
     * spider's silhouette covering the bug.
     *
     * Clamped at 0 so a direct hit stays a direct hit. */
    float edge_d = best_d - SPIDER_BODY_R_PX;
    if (edge_d < 0.0f) edge_d = 0.0f;

    /* REACH GATE. This is why bugs appeared to vanish at random.
     *
     * The loop above picks the nearest bug with no distance limit, so a pounce
     * that landed on empty substrate still resolved against whatever bug
     * happened to be closest -- anywhere in the tank. The player saw a bug
     * across the screen disappear for no reason.
     *
     * A pounce only resolves if it actually landed near a bug. CATCH_GOOD_PX is
     * the outer edge of a plausible catch, so it is the natural gate. */
    if (edge_d > CATCH_GOOD_PX) {
        ESP_LOGI(TAG, "MISSED (out of reach) d=%.0f", best_d);
        g.streak = 0;
        game_add_xp(XP_MISS);
        return;
    }

    /* Catch probability: closer is better, energy and stage help. */
    float p = CATCH_BASE - edge_d * CATCH_PER_PX;
    p += ((float)g.energy / 100.0f) * CATCH_ENERGY_BONUS;
    p += ((float)g.stage / 2.0f) * CATCH_STAGE_BONUS;

    /* A sick spider is worse at hunting, but the penalty must not make
     * recovery impossible: at 0.3x the observed rate was 0.14, so a sick
     * spider effectively starved. 0.7x keeps it clearly handicapped while
     * still letting it feed itself. */
    if (g.sick) p *= 0.7f;

    /* Player skill vs. the auto-pounce safety valve. */
    p *= behaviour_pounce_by_player() ? PLAYER_POUNCE_BONUS : AUTO_POUNCE_PENALTY;

    /* Temperament: a bold spider commits harder to a catch. */
    p *= game_temperament()->catch_mult;

    if (p < 0.20f) p = 0.20f;
    if (p > 0.95f) p = 0.95f;

    /* Quality comes from GEOMETRY: did the body land on the bug.
     *
     * This used to be judged by behaviour_aim_quality(), which compares the
     * released pull against the ideal power for prey_nearest(). That is a
     * different bug from the one the spider actually landed on whenever more
     * than one is on screen, so a perfect landing could score zero. Judging by
     * where the spider ended up is both fairer and visible to the player.
     *
     * Aim quality is still used, but as a BONUS on top of a geometric hit
     * rather than as the thing that decides whether the hit happened. */
    catch_quality_t q = CATCH_MISS;

    float perfect_px = CATCH_PERFECT_PX *
                       species_get(s_prey[best].species)->perfect_mult;

    if (edge_d <= perfect_px) {
        q = CATCH_PERFECT;
    } else if (edge_d <= CATCH_GOOD_PX) {
        q = CATCH_GOOD;
    }

    /* A well-aimed shot promotes a good landing to a perfect one. It can never
     * turn a miss into a hit -- the spider has to actually reach the bug. */
    if (q == CATCH_GOOD && behaviour_pounce_by_player() &&
        behaviour_aim_quality() >= 0.99f) {
        q = CATCH_PERFECT;
    }

    float roll = randf();

    bool caught;
    if (q == CATCH_PERFECT)      caught = true;
    else if (q == CATCH_GOOD)    caught = (roll <= p);
    else                         caught = (roll <= p * 0.5f);

    if (caught) {
        /* Caught. */
        prey_t *pr = &s_prey[best];
        pr->dead = true;
        pr->fleeing = false;
        /* The splat lingers longer than the old corpse did. 1.5s was enough to
         * notice a bug lying on its back, but a splat is the only confirmation
         * the player gets that the catch landed, so it needs to be readable. */
        pr->dead_at = lv_tick_get() + 2200;
        build_body(pr);
        layout_body(pr);

        g.state        = SPIDER_EAT;
        g.eat_until_ms = (int32_t)(lv_tick_get() + EAT_MS);

        int hunger_gain = pr->wild ? WILD_HUNGER_GAIN : FEED_HUNGER_GAIN;
        int mood_gain   = pr->wild ? WILD_MOOD_GAIN   : FEED_MOOD_GAIN;
        uint32_t base_xp = XP_GOOD;

        if (q == CATCH_PERFECT) {
            hunger_gain += PERFECT_HUNGER_BONUS;
            mood_gain   += PERFECT_MOOD_BONUS;
            base_xp      = XP_PERFECT;
            g.streak++;
            if (g.streak > g.best_streak) g.best_streak = g.streak;
            game_award(ACH_FIRST_PERFECT);
        }

        int h = (int)g.hunger + hunger_gain;
        if (h > 100) h = 100;
        g.hunger = (uint8_t)h;

        int m = (int)g.mood + mood_gain;
        if (m > 100) m = 100;
        g.mood = (uint8_t)m;

        /* Skilled play is roughly energy-neutral: the pounce cost is paid
         * back by the meal. */
        int e = (int)g.energy + EAT_ENERGY_GAIN;
        if (e > 100) e = 100;
        g.energy = (uint8_t)e;

        /* Streak multiplies XP. */
        uint32_t bonus = g.streak > STREAK_MAX ? STREAK_MAX : g.streak;
        uint32_t xp = (uint32_t)((float)base_xp *
                                 (1.0f + (float)bonus * STREAK_BONUS_PER));
        game_add_xp(xp);

        g.meals++;
        /* ACH_FIRST_MEAL was defined and displayed on the status page but
         * never awarded anywhere, so "first meal" could never light up. */
        game_award(ACH_FIRST_MEAL);
        game_award(ACH_FIRST_HUNT);
        if (g.meals >= 10) game_award(ACH_TEN_MEALS);

        /* Bond grows with successful hunts; a perfect catch is worth more. */
        if (g.bond < 100) {
            uint16_t inc = (q == CATCH_PERFECT) ? BOND_PER_PERFECT
                                                : BOND_PER_CATCH;
            uint16_t b = (uint16_t)(g.bond + inc);
            g.bond = (b > 100) ? 100 : b;
        }

        /* Eating dirties the tank. FEED_HYGIENE_COST and terrarium_add_poop()
         * both existed but were never called, so poop never appeared. */
        g.hygiene = (g.hygiene > FEED_HYGIENE_COST)
                  ? (uint8_t)(g.hygiene - FEED_HYGIENE_COST) : 0;
        terrarium_add_poop(g.spider_x + (rand() % 17) - 8,
                           g.spider_y + (rand() % 17) - 8);

        audio_play(q == CATCH_PERFECT ? SFX_PERFECT : SFX_EAT);
        ESP_LOGI(TAG, "CAUGHT q=%d p=%.2f d=%.0f streak=%u",
                 (int)q, p, best_d, (unsigned)g.streak);
    } else {
        /* Missed: the prey bolts and the streak resets. */
        prey_t *pr = &s_prey[best];
        pr->fleeing = true;
        pr->retarget_at = 0;
        g.streak = 0;
        game_add_xp(XP_MISS);
        audio_play(SFX_ALERT);
        ESP_LOGI(TAG, "MISSED q=%d p=%.2f d=%.0f streak=%u",
                 (int)q, p, best_d, (unsigned)g.streak);
    }
}

/* ------------------------------------------------------------------ */
/* Tick                                                                */
/* ------------------------------------------------------------------ */
void prey_tick(uint32_t now_ms, float dt_s)
{
    for (int i = 0; i < MAX_PREY; i++) {
        prey_t *p = &s_prey[i];
        if (!p->alive) continue;

        if (p->dead) {
            /* The corpse lingers briefly so the player sees the catch, then
             * frees its slot. Without this, three catches would permanently
             * fill MAX_PREY and prey_spawn() would silently stop working. */
            if (p->dead_at && (int32_t)(now_ms - p->dead_at) >= 0) {
                if (p->root) {
                    lv_obj_del(p->root);
                    p->root = NULL;
                }
                /* Clear the part table too.
                 *
                 * The parts are children of p->root, so deleting the root frees
                 * them. Leaving the pointers in p->parts[] meant the next
                 * build_body() for this slot called lv_obj_clean() on a root
                 * that had already been destroyed -- a use-after-free that
                 * corrupted LVGL's event list and crashed with
                 * "LoadProhibited in lv_event_mark_deleted". */
                for (int k = 0; k < p->part_n; k++) {
                    p->parts[k].obj = NULL;
                }
                p->alive = false;
                p->dead = false;
                p->dead_at = 0;
                p->part_n = 0;
            }
            continue;
        }

        float dx = p->x - (float)g.spider_x;
        float dy = p->y - (float)g.spider_y;
        float d = sqrtf(dx * dx + dy * dy);

        /* Level scaling: prey get faster and warier as the player levels, so
         * the game does not stay flat forever. Computed once per bug. */
        float lvl = (float)g.level;
        float speed_mult = 1.0f + lvl * PREY_SPEED_PER_LEVEL;
        if (speed_mult > PREY_SPEED_MAX_MULT) speed_mult = PREY_SPEED_MAX_MULT;

        float alert_mult = 1.0f + lvl * PREY_ALERT_PER_LEVEL;
        if (alert_mult > PREY_ALERT_MAX_MULT) alert_mult = PREY_ALERT_MAX_MULT;

        /* Per-species traits come from the catalogue. */
        const species_def_t *sp = species_get(p->species);

        /* Wild prey are warier and quicker off the mark. */
        float flee_r = PREY_AGGRO_RADIUS * alert_mult * sp->flee_radius_mult
                     * (p->wild ? WILD_FLEE_RADIUS_MULT : 1.0f);
        float flee_s = PREY_FLEE_SPEED * speed_mult * sp->flee_speed_mult
                     * (p->wild ? WILD_FLEE_SPEED_MULT : 1.0f);
        /* Never let the alert radius grow past the aim range: the spider must
         * be able to get close enough to crouch, or the hunt deadlocks. */
        if (flee_r > (float)PREY_FLEE_RADIUS_MAX) {
            flee_r = (float)PREY_FLEE_RADIUS_MAX;
        }
        /* FREEZE while the player is actively pulling.
         *
         * This is the fix for "the hunt doesn't work properly". The bug used to
         * keep fleeing while the player aimed: at level 13 it covers ~324 px in
         * a 2.6 s window, while the longest possible pounce is 160 px. The bug
         * was therefore ALWAYS out of reach by the time the shot fired, so the
         * hunt could not succeed no matter how well the player timed it.
         *
         * The gate is now behaviour_aim_dragging(), not behaviour_aim_active().
         * The latter is true whenever prey exists, so using it here would freeze
         * every bug permanently. Dragging is the actual "player is lining up a
         * shot" signal.
         *
         * Holding the bug still while the player lines up the shot is also the
         * right feel: the spider has it cornered, and the tension is in the
         * timing, not in chasing a moving target with a timing meter. */
        if (behaviour_aim_dragging()) {
            /* Still update the flutter so it does not look frozen dead. */
            if (sp->move == MOVE_FLUTTER) {
                float lateral = sinf((float)now_ms * MOTH_FLUTTER_HZ)
                              * MOTH_FLUTTER_PX * dt_s;
                p->x += -sinf(p->heading) * lateral;
                p->y +=  cosf(p->heading) * lateral;
                clamp_prey(p);
            }
            continue;
        }

        /* Flee when the spider is inside the aggro radius. Outside it the bug
         * goes about its business and ignores the spider entirely.
         *
         * The hysteresis is what stops the bug flickering between fleeing and
         * wandering when the spider hovers at the boundary. It was 1.6, which
         * kept a bug sprinting until it was 112px clear -- the tank felt
         * permanently panicked. 1.25 lets it settle soon after it escapes. */
        if (d < flee_r && !g.sick) {
            p->fleeing = true;
        } else if (d > flee_r * PREY_AGGRO_HYSTERESIS) {
            p->fleeing = false;
        }

        if (p->fleeing) {
            /* Run directly away from the spider, but in bursts: the bug
             * sprints, then pauses to recover. The pause is the spider's
             * opening, and the rhythm is what the player learns to read. */
            float want = atan2f(dy, dx);
            float delta = wrap_pi(want - p->heading);
            p->heading = wrap_pi(p->heading + delta * 0.35f);

            uint32_t cycle = PREY_FLEE_BURST_MS + PREY_FLEE_REST_MS;
            bool sprinting = (now_ms % cycle) < PREY_FLEE_BURST_MS;

            float step = sprinting ? flee_s * dt_s : 0.0f;
            p->x += cosf(p->heading) * step;
            p->y += sinf(p->heading) * step;
        } else {
            /* Wander. The movement STYLE comes from the species table, so a
             * new bug picks an existing style with no code change. */
            uint32_t retarget = (sp->move == MOVE_ERRATIC)
                              ? FLY_RETARGET_MS : PREY_WANDER_RETARGET_MS;

            if (now_ms >= p->retarget_at) {
                p->retarget_at = now_ms + retarget;
                p->wander_dir = randf() * 2.0f * PI_F - PI_F;
            }
            float delta = wrap_pi(p->wander_dir - p->heading);
            p->heading = wrap_pi(p->heading + delta * 0.12f);

            float step = PREY_SPEED * speed_mult * sp->speed_mult * dt_s;

            switch (sp->move) {
            case MOVE_ERRATIC:
                /* Constant small heading noise. */
                p->heading = wrap_pi(p->heading +
                                     (randf() - 0.5f) * FLY_JITTER);
                break;

            case MOVE_HOP: {
                /* Fast burst, then still. Predictable if you watch the
                 * rhythm, which is what makes it learnable. */
                uint32_t phase = now_ms % CRICKET_HOP_CYCLE_MS;
                if (phase < CRICKET_HOP_BURST_MS) {
                    step *= CRICKET_HOP_SPEED_MULT;
                } else {
                    step = 0.0f;
                }
                break;
            }

            case MOVE_SLOW:
            case MOVE_FLUTTER:
            case MOVE_WANDER:
            default:
                break;
            }

            p->x += cosf(p->heading) * step;
            p->y += sinf(p->heading) * step;

            if (sp->move == MOVE_FLUTTER) {
                /* Flutter: a sinusoidal lateral offset on top of the heading,
                 * applied perpendicular to travel. */
                float lateral = sinf((float)now_ms * MOTH_FLUTTER_HZ)
                              * MOTH_FLUTTER_PX * dt_s;
                p->x += -sinf(p->heading) * lateral;
                p->y +=  cosf(p->heading) * lateral;
            }
        }

        /* Bounce off the playfield edges. */
        if (p->x <= (float)g_play.left || p->x >= (float)g_play.right) {
            p->heading = wrap_pi(PI_F - p->heading);
            p->wander_dir = p->heading;
        }
        if (p->y <= (float)g_play.top || p->y >= (float)g_play.bottom) {
            p->heading = wrap_pi(-p->heading);
            p->wander_dir = p->heading;
        }
        clamp_prey(p);

        /* Wing/leg flutter: alternate the lateral offsets so the bug reads as
         * moving rather than sliding. */
        if ((now_ms - p->frame_at) >=
            (p->fleeing ? PREY_FLEE_FRAME_MS : PREY_WALK_FRAME_MS)) {
            p->frame_at = now_ms;
            p->frame = (p->frame + 1) & 1;
        }
    }
}

void prey_render(uint32_t now_ms)
{
    (void)now_ms;

    for (int i = 0; i < MAX_PREY; i++) {
        prey_t *p = &s_prey[i];
        if (!p->alive || !p->root) continue;

        layout_body(p);
        lv_obj_set_pos(p->root, (int32_t)p->x - PREY_PIVOT_X,
                       (int32_t)p->y - PREY_PIVOT_Y);
    }
}
