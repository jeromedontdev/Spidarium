/*
 * Spidarium — prey.
 *
 * The body and animation are supplied by transparent, top-down sprite assets.
 * The artwork is deliberately orientation-neutral because LVGL matrix support
 * is disabled; movement and heading remain part of the prey simulation.
 */
#include "prey.h"

#include "audio.h"
#include "behaviour.h"
#include "assets.h"
#include "game_config.h"
#include "gift.h"
#include "species.h"
#include "sprites.h"
#include "terrarium.h"
#include "needs.h"
#include "ui_hud.h"

#include "esp_log.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "spidarium.prey";

#define PI_F 3.14159265f

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
    uint32_t       poop_at;     /* next time this bug leaves a speckle */
    lv_obj_t      *root;        /* container, positioned at the bug centre */
    lv_obj_t      *sprite;
    int            sprite_index;
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

/* ------------------------------------------------------------------ */
/* Body construction                                                   */
/* ------------------------------------------------------------------ */
static void update_sprite(prey_t *p)
{
    if (!p->sprite) return;
    int index = sprite_prey_index(p->species, p->fleeing, p->dead, p->frame);
    if (index < 0 || index == p->sprite_index) return;
    const lv_image_dsc_t *image = assets_image(index);
    if (!image) {
        lv_obj_set_hidden(p->sprite, true);
        lv_obj_set_style_bg_color(p->root,
                                  lv_color_hex(species_get(p->species)->body_col), 0);
        lv_obj_set_style_bg_opa(p->root, LV_OPA_COVER, 0);
        p->sprite_index = index;
        return;
    }

    lv_image_set_src(p->sprite, image);
    lv_obj_set_hidden(p->sprite, false);
    lv_obj_set_style_bg_opa(p->root, LV_OPA_TRANSP, 0);
    p->sprite_index = index;
}

static void build_body(prey_t *p)
{
    if (p->root && !lv_obj_is_valid(p->root)) {
        ESP_LOGW(TAG, "prey %d: stale root, rebuilding", (int)(p - s_prey));
        p->root = NULL;
        p->sprite = NULL;
        p->sprite_index = -1;
    }

    if (!p->root) {
        p->root = lv_obj_create(ui.creature_layer);
        lv_obj_remove_style_all(p->root);
        lv_obj_set_size(p->root, PREY_SPRITE_W, PREY_SPRITE_H);
        lv_obj_set_style_bg_opa(p->root, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(p->root, 0, 0);
        lv_obj_set_style_radius(p->root, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_scrollable(p->root, false);
        lv_obj_set_clickable(p->root, false);
    }

    if (!p->sprite || !lv_obj_is_valid(p->sprite)) {
        p->sprite = lv_image_create(p->root);
        lv_obj_center(p->sprite);
        lv_obj_set_clickable(p->sprite, false);
        lv_obj_set_scrollable(p->sprite, false);
        p->sprite_index = -1;
    }
    update_sprite(p);
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

bool prey_spawn_species_ex(prey_species_t species, bool wild)
{
    if (!species_unlocked(species)) {
        ESP_LOGW(TAG, "spawn rejected: species %d invalid or level locked", species);
        return false;
    }
    int slot = -1;
    for (int i = 0; i < MAX_PREY; i++) {
        if (!s_prey[i].alive) { slot = i; break; }
    }
    if (slot < 0) { ESP_LOGI(TAG, "spawn skipped: tank already has three prey"); return false; }

    prey_t *p = &s_prey[slot];
    p->alive       = true;
    p->dead        = false;
    p->fleeing     = false;
    p->wild        = wild;
    p->species     = species;
    p->frame       = 0;
    p->sprite_index = -1;
    p->frame_at    = lv_tick_get();
    p->retarget_at = 0;
    p->dead_at     = 0;
    p->poop_at     = lv_tick_get() + BUG_POOP_MS;
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
    lv_obj_set_pos(p->root, (int32_t)p->x - PREY_PIVOT_X,
                   (int32_t)p->y - PREY_PIVOT_Y);
    return true;
}

bool prey_spawn_species(prey_species_t species)
{
    return prey_spawn_species_ex(species, false);
}

bool prey_spawn(void)
{
    return prey_spawn_species(species_choose_unlocked((uint32_t)rand()));
}

bool prey_spawn_wild(void)
{
    return prey_spawn_species_ex(species_choose_unlocked((uint32_t)rand()), true);
}

bool prey_progression_test(void)
{
    uint32_t level = g.level;
    int original_count = prey_count();
    prey_t original[MAX_PREY];
    memcpy(original, s_prey, sizeof original);
    memset(s_prey, 0, sizeof s_prey);
    bool ok = true;
    const uint32_t levels[] = {1, 2, 4, 6, 10, 13, 16, 20};
    for (int stage = 0; stage < PREY_SPECIES_COUNT; stage++) {
        g.level = levels[stage] - 1;
        ok = ok && !species_unlocked(stage);
        g.level = levels[stage];
        ok = prey_spawn_species(stage) && ok;
        ok = ok && prey_count() == 1 && s_prey[0].species == stage;
        ok = ok && prey_unlocked_species() == stage + 1;
        ok = ok && prey_next_locked_species() ==
            (stage + 1 == PREY_SPECIES_COUNT ? -1 : stage + 1);
        bool seen[PREY_SPECIES_COUNT] = {false};
        for (uint32_t roll = 0; roll < 64; roll++) {
            prey_species_t species = species_choose_unlocked(roll);
            ok = ok && species_unlocked(species);
            seen[species] = true;
        }
        for (int i = 0; i <= stage; i++) ok = ok && seen[i];
        for (int i = stage + 1; i < PREY_SPECIES_COUNT; i++)
            ok = !prey_spawn_species_ex(i, true) && ok;
        if (s_prey[0].root) lv_obj_delete(s_prey[0].root);
        memset(s_prey, 0, sizeof s_prey);
    }
    ok = !prey_spawn_species_ex(-1, false) && ok;
    ok = !prey_spawn_species_ex(PREY_SPECIES_COUNT, false) && ok;
    for (int i = 0; i < MAX_PREY; i++) s_prey[i].alive = true;
    uint32_t taps = g.taps;
    for (int i = 0; i < 5; i++) game_on_feed(NULL);
    needs_feed();
    ui_hud_tick(lv_tick_get() + 10000);
    ok = ok && prey_count() == MAX_PREY && g.taps == taps &&
         lv_obj_has_state(ui.btn_feed, LV_STATE_DISABLED);
    s_prey[0].alive = false;
    ui_hud_tick(lv_tick_get() + 11000);
    ok = ok && !lv_obj_has_state(ui.btn_feed, LV_STATE_DISABLED);
    memset(s_prey, 0, sizeof s_prey);
    g.level = level;
    memcpy(s_prey, original, sizeof original);
    ui_hud_tick(lv_tick_get() + 12000);
    return ok && prey_count() == original_count;
}

bool prey_hunt_test(bool (*exercise)(int32_t x, int32_t y))
{
    prey_t original[MAX_PREY];
    memcpy(original, s_prey, sizeof original);
    memset(s_prey, 0, sizeof s_prey);
    s_prey[0].alive = true;
    s_prey[0].species = PREY_FLY;
    s_prey[0].x = 240;
    s_prey[0].y = 240;
    bool ok = exercise(240, 240);
    for (int i = 0; i < MAX_PREY; i++)
        if (s_prey[i].root) lv_obj_delete(s_prey[i].root);
    memcpy(s_prey, original, sizeof original);
    return ok;
}
void prey_clear(void)
{
    for (int i = 0; i < MAX_PREY; i++) {
        if (s_prey[i].root) {
            lv_obj_del(s_prey[i].root);
            s_prey[i].root = NULL;
        }
        s_prey[i].sprite = NULL;
        s_prey[i].sprite_index = -1;
        s_prey[i].alive = false;
        s_prey[i].dead = false;
        s_prey[i].dead_at = 0;
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

    const gift_bonus_t *gift = gift_bonus();
    p *= gift->catch_success_mult * gift->pounce_success_mult;

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
        pr->frame = 0;
        /* The distinct down pose lingers long enough to confirm the catch. */
        pr->dead_at = lv_tick_get() + 2200;
        build_body(pr);

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
        game_add_silk(q == CATCH_PERFECT ? 5 : 3);

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
    const gift_bonus_t *gift = gift_bonus();
    static uint32_t last_wild;
    static bool wild_seeded;
    static uint32_t last_visitor_day = UINT32_MAX;
    static int reported_auto_feed = -1;
    if (reported_auto_feed != g.auto_feed) {
        reported_auto_feed = g.auto_feed;
        ESP_LOGI(TAG, "auto feed=%u (manual feed unchanged)", g.auto_feed);
    }
    if (!g.auto_feed) {
        wild_seeded = false;
        last_wild = now_ms;
        last_visitor_day = g.days_lived;
    } else {
        if (!wild_seeded) {
            wild_seeded = true;
            last_wild = now_ms;
            if (prey_count() == 0) prey_spawn_wild();
        } else if (prey_count() == 0 &&
                   now_ms - last_wild >= WILD_SPAWN_MS / (1u + gift->wild_spawn_bonus)) {
            last_wild = now_ms;
            prey_spawn_wild();
        }
        if (g.days_lived != last_visitor_day) {
            last_visitor_day = g.days_lived;
            if (g.days_lived > 0) {
                prey_species_t rare = PREY_FLY;
                for (int i = 0; i < PREY_SPECIES_COUNT; i++)
                    if (species_unlocked(i)) rare = (prey_species_t)i;
                prey_spawn_species_ex(rare, true);
                audio_play(SFX_ALERT);
                ESP_LOGI(TAG, "daily visitor: species %d", (int)rare);
            }
        }
    }

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
                p->sprite = NULL;
                p->sprite_index = -1;
                p->alive = false;
                p->dead = false;
                p->dead_at = 0;
            }
            continue;
        }

        /* A bug left in the tank fouls it. This is the consequence of not
         * catching it: a pale speckle appears near the bug every BUG_POOP_MS,
         * which dirties the tank and lowers hygiene until Clean is used. */
        if (p->poop_at && (int32_t)(now_ms - p->poop_at) >= 0) {
            p->poop_at = now_ms + BUG_POOP_MS;
            terrarium_add_bug_poop((int32_t)p->x + (rand() % 13) - 6,
                                   (int32_t)p->y + (rand() % 13) - 6);
            ESP_LOGI(TAG, "bug %d left a speckle; tank dirt=%d", i, terrarium_dirt());
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
                     * (p->wild ? WILD_FLEE_RADIUS_MULT : 1.0f)
                     * gift->fear_mult;
        float flee_s = PREY_FLEE_SPEED * speed_mult * sp->flee_speed_mult
                     * (p->wild ? WILD_FLEE_SPEED_MULT : 1.0f)
                     * gift->prey_flee_penalty;
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

        /* Alternate the authored sprite frames so the bug reads as moving. */
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

        update_sprite(p);
        lv_obj_set_pos(p->root, (int32_t)p->x - PREY_PIVOT_X,
                       (int32_t)p->y - PREY_PIVOT_Y);
    }
}
