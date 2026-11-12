/*
 * Spidarium — behaviour engine implementation.
 */
#include "behaviour.h"

#include "audio.h"
#include "decor.h"
#include "game_config.h"
#include "gift.h"
#include "prey.h"
#include "spider.h"
#include "ui_hud.h"

#include "esp_log.h"

#include <math.h>
#include <stdlib.h>

#define PI_F 3.14159265f

/* Steering targets. */
static bool    s_has_target;
static int32_t s_target_x, s_target_y;

/* Wander. */
static uint32_t s_retarget_at;
static float    s_wander_dir;

/* Pounce. */
static bool     s_pouncing;
static uint32_t s_pounce_end;
static int32_t  s_pounce_tx, s_pounce_ty;
static uint32_t s_next_pounce_at;   /* cooldown after a miss */

/* Idle micro-behaviour. */
static uint32_t s_idle_until;
static uint32_t s_gift_visit_at, s_gift_hold_until, s_gift_visit_end;
static bool s_gift_visiting;
bool behaviour_gift_visiting(void) { return s_gift_visiting; }

/* Aim window: the spider crouches and holds while the player decides. */
static bool     s_aim_active;
static uint32_t s_aim_start;
static int32_t  s_aim_tx, s_aim_ty;
static bool     s_pounce_by_player;

/* Direct aiming: drag toward the target, then release to jump. */
static bool    s_dragging;      /* finger is down and pulling */
static int32_t s_drag_x, s_drag_y;   /* current finger position */
static int32_t s_drag_start_x, s_drag_start_y;
static float   s_aim_power;     /* power of the last released shot, 0..1 */
static float   s_aim_ideal;     /* power that would land exactly on the bug */

/* Last tap direction. A bonded spider turns toward it while idle. */
static bool  s_tap_heading_valid;
static float s_tap_heading;

/* Stuck detector: remembers where the spider was and when it last moved. */
static float    s_stuck_x, s_stuck_y;
static uint32_t s_stuck_at;

/* Player-initiated sleep. Held for a minimum duration so the SLEEP button
 * always visibly does something, even at full energy. */
static bool     s_sleep_forced;
static uint32_t s_sleep_start;
static uint32_t s_molt_start;

/* Sub-pixel position accumulator. g.spider_x/y are int16_t, so moving at
 * 55 px/s in 50 ms ticks (2.75 px) is fine, but any speed below ~20 px/s
 * would truncate to zero every tick and freeze the spider. Keeping the true
 * position in floats and publishing the rounded value avoids that. */
static float    s_px, s_py;
static bool     s_pos_init;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static float wrap_pi(float a)
{
    while (a > PI_F)  a -= 2.0f * PI_F;
    while (a < -PI_F) a += 2.0f * PI_F;
    return a;
}

static float dist_to(int32_t x, int32_t y)
{
    float dx = (float)(x - g.spider_x);
    float dy = (float)(y - g.spider_y);
    return sqrtf(dx * dx + dy * dy);
}

/* Turn the heading toward `want` at no more than `rate_deg` per second. */
static void steer_toward_rate(float want, float dt_s, float rate_deg)
{
    float delta = wrap_pi(want - g.spider_heading);
    float max_step = (rate_deg * PI_F / 180.0f) * dt_s;
    if (delta > max_step)  delta = max_step;
    if (delta < -max_step) delta = -max_step;
    g.spider_heading = wrap_pi(g.spider_heading + delta);
}

/* Turn at the normal movement rate. */
static void steer_toward(float want, float dt_s)
{
    steer_toward_rate(want, dt_s, TURN_RATE_DEG);
}

/* Defined below; declared here because steering needs them first. */
static float gift_avoidance(float heading);
static float decor_avoidance(float heading);
static float wall_avoidance(float want);

/* Steer toward `want` while bending away from the gift and any blocking decor
 * in the way. This is the steering-level avoidance: the spider turns around an
 * obstacle instead of walking into it and being shoved back out. */
static void steer_avoiding(float want, float dt_s)
{
    float avoid = gift_avoidance(want) + decor_avoidance(want) + wall_avoidance(want);
    steer_toward(wrap_pi(want + avoid), dt_s);
}

/* Wander direction that always points somewhere INSIDE the tank.
 *
 * A uniform random angle can point straight at a wall, and the wall avoidance
 * then has to fight it every frame. Biasing the direction toward the interior
 * means the spider mostly walks across the tank rather than into its edges.
 *
 * The target is drawn from the whole playfield, not the middle half. Aiming at
 * the middle half made every wander leg point at the centre, so the spider
 * orbited and settled there instead of exploring. A small inset keeps the
 * target off the very edge, where wall avoidance would immediately fight it. */
static float wander_dir_inward(void)
{
    float cx = (float)(g_play.left + g_play.right) * 0.5f;
    float cy = (float)(g_play.top + g_play.bottom) * 0.5f;

    /* 80% of the half-extent: reaches most of the tank, stays off the wall. */
    float hw = (float)(g_play.right - g_play.left) * 0.40f;
    float hh = (float)(g_play.bottom - g_play.top) * 0.40f;
    float tx = cx + ((float)(rand() % 2001) / 1000.0f - 1.0f) * hw;
    float ty = cy + ((float)(rand() % 2001) / 1000.0f - 1.0f) * hh;

    return atan2f(ty - (float)g.spider_y, tx - (float)g.spider_x);
}

/* How long the player has to tap, in ms. Shrinks with level so the game gets
 * tighter as the player improves, but never below AIM_WINDOW_MIN_MS. */
static uint32_t aim_window_ms(void)
{
    uint32_t w = AIM_WINDOW_MS;
    uint32_t shrink = g.level * AIM_WINDOW_PER_LEVEL;
    if (shrink >= w) w = AIM_WINDOW_MIN_MS;
    else w -= shrink;
    if (w < AIM_WINDOW_MIN_MS) w = AIM_WINDOW_MIN_MS;

    /* A crystal gift widens the window. */
    w = (uint32_t)((float)w * gift_bonus()->aim_window_mult);

    /* Temperament: a timid spider gives the player longer to aim. */
    w = (uint32_t)((float)w * game_temperament()->aim_window_mult);

    /* A bonded spider is calmer and holds the window open longer. */
    if (g.bond >= BOND_AIM_BONUS) {
        w = (uint32_t)((float)w * BOND_AIM_BONUS_MULT);
    }

    return w;
}

/* Move forward along the current heading, clamped to the playfield. */
static void advance(float speed, float dt_s)
{
    if (!s_pos_init) {
        s_px = (float)g.spider_x;
        s_py = (float)g.spider_y;
        s_pos_init = true;
    }

    float step = speed * dt_s;
    float nx = s_px + cosf(g.spider_heading) * step;
    float ny = s_py + sinf(g.spider_heading) * step;

    /* SLIDE along walls; do not reflect.
     *
     * Reflecting the heading on every wall contact fought the steering: the
     * spider steered back toward its target, hit the wall again, reflected
     * again, and jittered in place. That is the "glitches out at the edges"
     * behaviour. Clamping only the offending axis lets the spider slide along
     * the wall, which is stable and is what a real creature does.
     *
     * The position is still clamped, so the spider can never leave the field
     * even if the steering keeps pushing outward. */
    if (nx < (float)g_play.left)   nx = (float)g_play.left;
    if (nx > (float)g_play.right)  nx = (float)g_play.right;
    if (ny < (float)g_play.top)    ny = (float)g_play.top;
    if (ny > (float)g_play.bottom) ny = (float)g_play.bottom;

    /* Steering normally keeps the spider out of blocking decor, but the player
     * can drop an item straight onto it. Refuse that step rather than let the
     * spider stand inside a rock: it will steer out on the next frame. */
    if (decor_blocks_point((int16_t)lroundf(nx), (int16_t)lroundf(ny))) {
        nx = s_px;
        ny = s_py;
    }

    s_px = nx;
    s_py = ny;
    g.spider_x = (int16_t)lroundf(nx);
    g.spider_y = (int16_t)lroundf(ny);
}

/* Gift avoidance.
 *
 * There is only ONE gift now, so this is far simpler than the old multi-item
 * decor avoidance. The principle is unchanged and still the important part:
 * avoidance is a STEERING input, not a positional shove. The spider turns away
 * from the gift before reaching it, so it walks around instead of grinding
 * against it.
 *
 * The gift is also something the spider is fond of, so it is avoided gently --
 * a wide berth would read as fear.
 */
#define GIFT_AVOID_RADIUS   40.0f   /* start steering away this far out */
#define GIFT_PUSH_RADIUS    18.0f   /* only shove if actually this close */

/* Returns a heading offset (radians) that steers away from the gift, or 0 if
 * the path ahead is clear. */
static float gift_avoidance(float heading)
{
    if (!g.gift.used) return 0.0f;

    float dx = (float)(g.gift.x - g.spider_x);
    float dy = (float)(g.gift.y - g.spider_y);
    float d2 = dx * dx + dy * dy;
    if (d2 < 0.01f || d2 > GIFT_AVOID_RADIUS * GIFT_AVOID_RADIUS) return 0.0f;

    float d = sqrtf(d2);

    /* Only avoid the gift when it is roughly AHEAD. An item behind the spider
     * must not push it, or the spider could never turn around. */
    float dot = (dx * cosf(heading) + dy * sinf(heading)) / d;
    if (dot < 0.15f) return 0.0f;

    /* Steer perpendicular to the gift, on whichever side the spider is already
     * leaning. Picking the side from the current heading avoids the dithering
     * you get from always turning the same way. */
    float to_gift = atan2f(dy, dx);
    float side = wrap_pi(to_gift - heading);
    float turn = (side >= 0.0f) ? -1.0f : 1.0f;

    /* Weight by proximity: barely a nudge at the edge, a firm turn up close. */
    float w = 1.0f - d / GIFT_AVOID_RADIUS;
    return turn * w * (PI_F * 0.5f);
}

/* Decor avoidance.
 *
 * Same principle as gift_avoidance, but for the player-placed blocking items.
 * Non-blocking decor is ignored entirely: the spider is meant to walk over a
 * leaf pile, and steering around it would make the blocking flag meaningless.
 *
 * The berth is measured from the item's footprint rather than its centre, so a
 * big log pushes the spider away further out than a small rock. */
#define DECOR_AVOID_MARGIN  26.0f   /* clearance beyond the footprint radius */

/* Returns a heading offset that steers away from the nearest blocking decor
 * item ahead, or 0 if the path is clear. */
static float decor_avoidance(float heading)
{
    int16_t ix = 0, iy = 0;
    float d = 0.0f;
    if (!decor_nearest_blocking(g.spider_x, g.spider_y,
                                DECOR_AVOID_MARGIN + 40.0f, &ix, &iy, &d))
        return 0.0f;

    int slot = decor_hit_test(ix, iy);
    const decor_item_t *item = decor_at(slot);
    const decor_def_t *def = item ? decor_def(item->kind) : NULL;
    if (!def) return 0.0f;

    /* Distance from the footprint edge, not the centre. */
    float edge = d - (float)def->radius;
    if (edge > DECOR_AVOID_MARGIN) return 0.0f;

    float dx = (float)(ix - g.spider_x);
    float dy = (float)(iy - g.spider_y);
    if (d < 0.01f) return 0.0f;

    /* Only steer around an item that is roughly AHEAD. */
    float dot = (dx * cosf(heading) + dy * sinf(heading)) / d;
    if (dot < 0.15f) return 0.0f;

    float to_item = atan2f(dy, dx);
    float side = wrap_pi(to_item - heading);
    float turn = (side >= 0.0f) ? -1.0f : 1.0f;

    /* Firm turn: a blocking item is a wall, not a friend. */
    float w = 1.0f - edge / DECOR_AVOID_MARGIN;
    if (w < 0.0f) w = 0.0f;
    return turn * w * (PI_F * 0.6f);
}

/* Wall avoidance.
 *
 * The spider used to walk straight at the boundary and then slide along it with
 * its heading still pointing outward, which reads as "trying to leave the
 * tank". Clamping the position stops it escaping but does nothing about the
 * intent, so it pressed into the wall until the stuck detector fired 1.5 s
 * later.
 *
 * This turns the spider BEFORE it reaches the edge, the same way gift_avoidance
 * turns it before the gift. The margin is wider than the playfield inset so the
 * turn starts early enough to look deliberate. */
#define WALL_AVOID_PX   70.0f

/* Returns a heading offset that steers the spider away from a wall it is
 * travelling into, or 0 if the path is clear.
 *
 * The previous version summed a signed turn per wall, which CANCELS in a
 * corner: heading up-right into the top-right corner, the right wall pushed one
 * way and the top wall pushed the other by the same amount, so the total was
 * zero and the spider drove straight into the corner. That is the "repeatedly
 * going to the top right" behaviour.
 *
 * This builds an inward PUSH VECTOR instead. In a corner both components point
 * inward, so the vector points diagonally away from the corner and there is
 * nothing to cancel. The desired heading is then lerped toward that vector,
 * weighted by how deep into the margin the spider is. */
static float wall_avoidance(float want)
{
    float dl = (float)(g.spider_x - g_play.left);
    float dr = (float)(g_play.right - g.spider_x);
    float dt = (float)(g.spider_y - g_play.top);
    float db = (float)(g_play.bottom - g.spider_y);

    /* Inward push, one component per axis. Each wall contributes only inside
     * its margin, weighted by proximity. */
    float px = 0.0f, py = 0.0f;
    if (dl < WALL_AVOID_PX) px += 1.0f - dl / WALL_AVOID_PX;
    if (dr < WALL_AVOID_PX) px -= 1.0f - dr / WALL_AVOID_PX;
    if (dt < WALL_AVOID_PX) py += 1.0f - dt / WALL_AVOID_PX;
    if (db < WALL_AVOID_PX) py -= 1.0f - db / WALL_AVOID_PX;

    if (px == 0.0f && py == 0.0f) return 0.0f;

    /* Only act when the spider is actually TRAVELLING into the wall. A wall
     * behind it must not push, or it could never turn around. This tests the
     * current heading, not the desired one. */
    float fx = cosf(g.spider_heading), fy = sinf(g.spider_heading);
    if (fx * px + fy * py >= 0.0f) return 0.0f;

    /* Lerp the desired heading toward the inward direction. */
    float want_in = atan2f(py, px);
    float depth = sqrtf(px * px + py * py);
    if (depth > 1.0f) depth = 1.0f;

    return wrap_pi(want_in - want) * depth;
}

/* Last-resort shove: only fires when the spider is genuinely inside the gift. */
static void separate_from_gift(void)
{
    if (!g.gift.used) return;

    float dx = (float)(g.spider_x - g.gift.x);
    float dy = (float)(g.spider_y - g.gift.y);
    float d2 = dx * dx + dy * dy;

    if (d2 > 0.01f && d2 < GIFT_PUSH_RADIUS * GIFT_PUSH_RADIUS) {
        float d = sqrtf(d2);
        float push = GIFT_PUSH_RADIUS - d;
        g.spider_x = (int16_t)((float)g.spider_x + dx / d * push);
        g.spider_y = (int16_t)((float)g.spider_y + dy / d * push);
    }
}

static void clamp_to_playfield(void)
{
    /* Adopt a position written directly by another code path (pounce, gift
     * separation) -- but do NOT overwrite the sub-pixel accumulator every
     * tick. Doing so rounded the position to whole pixels each tick and
     * destroyed sub-pixel motion, which made the gait stutter. */
    if (g.spider_x != (int16_t)lroundf(s_px)) s_px = (float)g.spider_x;
    if (g.spider_y != (int16_t)lroundf(s_py)) s_py = (float)g.spider_y;

    if (g.spider_x < g_play.left)   { g.spider_x = (int16_t)g_play.left;   s_px = (float)g.spider_x; }
    if (g.spider_x > g_play.right)  { g.spider_x = (int16_t)g_play.right;  s_px = (float)g.spider_x; }
    if (g.spider_y < g_play.top)    { g.spider_y = (int16_t)g_play.top;    s_py = (float)g.spider_y; }
    if (g.spider_y > g_play.bottom) { g.spider_y = (int16_t)g_play.bottom; s_py = (float)g.spider_y; }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */
void behaviour_force_sleep(bool on)
{
    if (on) {
        s_sleep_forced = true;
        s_sleep_start = lv_tick_get();
        behaviour_set_state(SPIDER_SLEEP);
    } else {
        s_sleep_forced = false;
        behaviour_set_state(SPIDER_IDLE);
    }
}

bool behaviour_sleep_forced(void)
{
    return s_sleep_forced;
}

void behaviour_set_state(spider_state_t state)
{
    if (g.state == state) return;

    g.state = (uint8_t)state;
    if (state == SPIDER_MOLT) s_molt_start = lv_tick_get();
    s_has_target = false;
    s_pouncing = false;
    s_aim_active = false;
    s_dragging = false;

    if (state == SPIDER_IDLE) {
        s_idle_until = lv_tick_get() + 1200u + (uint32_t)(rand() % 1800);
    }
}

void behaviour_seek_to(int32_t x, int32_t y)
{
    s_target_x = x;
    s_target_y = y;
    behaviour_set_state(SPIDER_WANDER);
    s_has_target = true;   /* set_state clears it */
}

void behaviour_note_tap(int32_t x, int32_t y)
{
    float dx = (float)(x - g.spider_x);
    float dy = (float)(y - g.spider_y);
    if (dx == 0.0f && dy == 0.0f) return;
    s_tap_heading = atan2f(dy, dx);
    s_tap_heading_valid = true;
}

void behaviour_pounce_at_ex(int32_t x, int32_t y, bool by_player)
{
    if (x < g_play.left) x = g_play.left;
    if (x > g_play.right) x = g_play.right;
    if (y < g_play.top) y = g_play.top;
    if (y > g_play.bottom) y = g_play.bottom;
    s_pounce_tx = x;
    s_pounce_ty = y;
    s_pouncing = true;
    s_pounce_by_player = by_player;

    /* The leap must actually REACH the target. The old fixed 260 ms window at
     * 260 px/s only covered ~68 px, so a tap at POUNCE_MAX_PX (130 px) landed
     * the spider half way and always scored a miss. Scale the flight time to
     * the distance instead, with a floor so a short hop still reads as a hop. */
    float pdx = (float)(x - g.spider_x);
    float pdy = (float)(y - g.spider_y);
    float pd  = sqrtf(pdx * pdx + pdy * pdy);
    uint32_t flight_ms = (uint32_t)(pd / 260.0f * 1000.0f);
    if (flight_ms < 120u) flight_ms = 120u;

    /* The cap must be long enough to cover POUNCE_MAX_PX.
     *
     * It was 520 ms, which at 260 px/s only covers 135 px -- but the maximum
     * pull is 160 px. A full-power shot therefore landed 25 px short of where
     * the player aimed, every time, which is a large part of why the catch
     * target felt impossibly small: the spider was never arriving at the bug.
     *
     * Derived from the constants rather than hardcoded, so raising
     * POUNCE_MAX_PX cannot silently reintroduce the shortfall. */
    uint32_t max_flight = (uint32_t)(POUNCE_MAX_PX * gift_bonus()->pounce_range_mult
                                     / 260.0f * 1000.0f) + 40u;
    if (flight_ms > max_flight) flight_ms = max_flight;
    s_pounce_end = lv_tick_get() + flight_ms;

    /* Every pounce costs energy, whether the player aimed it or the safety
     * valve fired. A miss therefore costs something, which is where the
     * tension comes from without any death state. */
    int energy_cost = (int)ceilf(POUNCE_ENERGY_COST / gift_bonus()->movement_efficiency_mult);
    g.energy = g.energy > energy_cost ? (uint8_t)(g.energy - energy_cost) : 0;

    /* Cooldown: without this the spider re-pounced every ~600 ms from the
     * same spot after a miss, producing an endless "missed" log and never
     * giving the prey time to flee. */
    s_next_pounce_at = lv_tick_get() + 1400u;

    behaviour_set_state(SPIDER_POUNCE);
    s_pouncing = true;

    spider_play_once(ANIM_POUNCE, true);
    spider_launch_pounce(26, flight_ms);
}

void behaviour_pounce_at(int32_t x, int32_t y)
{
    behaviour_pounce_at_ex(x, y, false);
}

bool behaviour_pounce_by_player(void)
{
    return s_pounce_by_player;
}

/* Is the slingshot available right now?
 *
 * This used to be `s_aim_active`, which was only true during the spider's own
 * crouch window -- so the player could only shoot in the few seconds after the
 * spider decided to stalk, and the focus ring counted that window down. That
 * made hunting a timing test against the spider's AI rather than against the
 * bug.
 *
 * The slingshot is now always available whenever there is something to shoot
 * at. The spider's crouch still exists, but only as its own dwell timer before
 * lunges unattended; it no longer gates the player.
 *
 * A FULL spider cannot hunt. Without this the player could pull and release all
 * day and nothing would happen, because prey_resolve_pounce() refuses to award
 * a meal at hunger 100 -- which reads as a broken control rather than a full
 * spider. Refusing the pull outright makes the state legible. */
bool behaviour_aim_active(void)
{
    if (s_pouncing || g.state == SPIDER_POUNCE || g.state == SPIDER_EAT ||
        g.state == SPIDER_MOLT || g.state == SPIDER_SLEEP) return false;
    if (prey_count() <= 0) return false;
    if (g.hunger >= HUNGER_FULL) return false;
    return true;
}
void behaviour_aim_cancel(void) { s_dragging = false; }

/* True when the spider is too full to hunt. The HUD uses this to explain why a
 * pull does nothing. */
bool behaviour_too_full(void)
{
    return g.hunger >= HUNGER_FULL;
}

uint32_t behaviour_aim_total_ms(void)
{
    return aim_window_ms();
}

uint32_t behaviour_aim_remaining_ms(void)
{
    if (!s_aim_active) return 0;

    uint32_t total = aim_window_ms();
    uint32_t elapsed = lv_tick_get() - s_aim_start;
    if (elapsed >= total) return 0;
    return total - elapsed;
}

/* --- Slingshot aim --------------------------------------------------- */

/* Begin a pull. Called on press. */
void behaviour_aim_drag_begin(int32_t x, int32_t y)
{
    if (!behaviour_aim_active()) return;
    s_dragging = true;
    s_drag_x = x;
    s_drag_y = y;
    s_drag_start_x = x;
    s_drag_start_y = y;
}

/* Update the pull. Called on every press-move. */
void behaviour_aim_drag_move(int32_t x, int32_t y)
{
    if (!behaviour_aim_active() || !s_dragging) return;
    s_drag_x = x;
    s_drag_y = y;
}

/* The shot vector runs from the spider to the finger. */
void behaviour_aim_drag_vec(float *dx, float *dy)
{
    if (dx) *dx = (float)(s_drag_x - g.spider_x);
    if (dy) *dy = (float)(s_drag_y - g.spider_y);
}

/* Current pull length as a fraction of the maximum, 0..1. */
float behaviour_aim_pull(void)
{
    if (!behaviour_aim_active() || !s_dragging) return 0.0f;

    float dx = (float)(s_drag_x - g.spider_x);
    float dy = (float)(s_drag_y - g.spider_y);
    float d = sqrtf(dx * dx + dy * dy);

    float max_pull = POUNCE_MAX_PX * gift_bonus()->pounce_range_mult;
    if (max_pull < 1.0f) max_pull = 1.0f;

    float p = d / max_pull;
    if (p > 1.0f) p = 1.0f;
    return p;
}

/* The power that would land exactly on the bug, 0..1. The HUD draws a marker
 * on the pull line so the player can see how far to pull.
 *
 * Computed live from the nearest bug rather than cached when the crouch opened.
 * The slingshot is now always available, so there is no "window opened" moment
 * to cache at -- and the bug moves, so a cached value would go stale. */
float behaviour_aim_ideal_power(void)
{
    int32_t px, py;
    if (!prey_nearest(&px, &py)) return 1.0f;

    float dx = (float)(px - g.spider_x);
    float dy = (float)(py - g.spider_y);
    float d = sqrtf(dx * dx + dy * dy);

    float max_px = POUNCE_MAX_PX * gift_bonus()->pounce_range_mult;
    if (max_px < 1.0f) max_px = 1.0f;

    float ideal = d / max_px;
    if (ideal > 1.0f) ideal = 1.0f;
    return ideal;
}

bool behaviour_aim_dragging(void)
{
    return behaviour_aim_active() && s_dragging;
}

/* Release the pull and fire. Returns true if a shot was taken. */
bool behaviour_aim_release(void)
{
    if (!behaviour_aim_active() || !s_dragging) {
        s_dragging = false;
        return false;
    }

    s_dragging = false;
    float gesture_dx = s_drag_x - s_drag_start_x;
    float gesture_dy = s_drag_y - s_drag_start_y;
    if (gesture_dx * gesture_dx + gesture_dy * gesture_dy <
        AIM_MIN_PULL_PX * AIM_MIN_PULL_PX) return false;

    float dx = (float)(s_drag_x - g.spider_x);
    float dy = (float)(s_drag_y - g.spider_y);
    float d = sqrtf(dx * dx + dy * dy);

    /* A tap with no pull is not a shot: it would fire at zero power. Treat it
     * as a cancel so a stray tap does not waste energy. */
    if (d < AIM_MIN_PULL_PX) {
        return false;
    }

    float max_px = POUNCE_MAX_PX * gift_bonus()->pounce_range_mult;

    /* Drag TOWARD the target, not away from it.
     *
     * The old slingshot inverted the drag, so the player pulled back and the
     * spider fired the opposite way. That is the right model for a catapult,
     * but it breaks at the screen edge: to fire at a bug near the top of the
     * tank you had to drag BELOW the spider, and near the bottom you had to
     * drag ABOVE it -- both of which run out of screen. Dragging toward the
     * target means the gesture always fits inside the tank, because the target
     * is inside the tank. */
    float ux = dx / d;
    float uy = dy / d;

    float dist = d;
    if (dist > max_px) dist = max_px;

    s_aim_power = dist / max_px;
    s_aim_ideal = behaviour_aim_ideal_power();

    int32_t tx = g.spider_x + (int32_t)(ux * dist);
    int32_t ty = g.spider_y + (int32_t)(uy * dist);

    s_aim_active = false;
    behaviour_pounce_at_ex(tx, ty, true);
    return true;
}

/* How good the released shot was, 0..1 (1 = perfect). Read by prey.c so catch
 * quality comes from aim, not from a random roll. */
float behaviour_aim_quality(void)
{
    float err = fabsf(s_aim_power - s_aim_ideal);
    if (err <= AIM_POWER_PERFECT) return 1.0f;
    if (err <= AIM_POWER_GOOD)    return 0.6f;
    return 0.0f;
}

bool behaviour_pouncing(void)
{
    return s_pouncing;
}

bool behaviour_resting(void)
{
    /* Sleeping, or awake but too tired to move. Both look like a frozen spider
     * from the outside, so both need to be explainable. */
    if (g.state == SPIDER_SLEEP) return true;
    return g.energy < game_temperament()->sleep_threshold;
}

void behaviour_pos(float *x, float *y)
{
    if (!s_pos_init) {
        s_px = (float)g.spider_x;
        s_py = (float)g.spider_y;
        s_pos_init = true;
    }
    if (x) *x = s_px;
    if (y) *y = s_py;
}

void behaviour_sync_position(void)
{
    s_px = g.spider_x;
    s_py = g.spider_y;
    s_pos_init = true;
}

/* ------------------------------------------------------------------ */
/* Tick                                                                */
/* ------------------------------------------------------------------ */
void behaviour_tick(uint32_t now_ms, float dt_s)
{
    static int reported_auto_hunt = -1;
    if (reported_auto_hunt != g.auto_hunt) {
        reported_auto_hunt = g.auto_hunt;
        ESP_LOGI("spidarium.behaviour", "auto hunt=%u (manual aim unchanged)", g.auto_hunt);
    }
    /* Player aiming owns the creature; AI cannot wander/crouch or auto-fire
     * midway through a held drag. Prey uses the same signal to hold position. */
    if (s_dragging && behaviour_aim_active()) return;
    if (!g.auto_hunt && (g.state == SPIDER_STALK || g.state == SPIDER_AIM))
        behaviour_set_state(SPIDER_IDLE);
    /* Sick spiders move at a fraction of normal speed. */
    float speed_scale = g.sick ? 0.45f : 1.0f;

    /* Stage affects speed: spiderlings are slower. */
    if (g.stage == STAGE_SPIDERLING) speed_scale *= 0.75f;
    else if (g.stage == STAGE_ADULT) speed_scale *= 1.15f;

    /* Temperament: bold spiders are quicker, lazy ones slower. */
    speed_scale *= game_temperament()->speed_mult;

    bool can_visit = g.gift.used && !g.sick && g.energy >= 20 &&
                     g.hunger >= 60 && (g.state == SPIDER_IDLE || g.state == SPIDER_WANDER);
    if (!can_visit) {
        s_gift_visiting = false;
        s_gift_hold_until = 0;
        s_gift_visit_at = now_ms + 15000;
    } else {
        if (!s_gift_visit_at) s_gift_visit_at = now_ms + 15000;
        if (!s_gift_visiting && (int32_t)(now_ms - s_gift_visit_at) >= 0) {
            s_gift_visiting = true;
            s_gift_visit_end = now_ms + 20000;
            s_gift_hold_until = 0;
        }
        if (s_gift_visiting) {
            float d = dist_to(g.gift.x, g.gift.y);
            if (d > 18 && !s_gift_hold_until) {
                behaviour_set_state(SPIDER_WANDER);
                float want = atan2f(g.gift.y - s_py, g.gift.x - s_px);
                float delta = wrap_pi(want - g.spider_heading);
                g.spider_heading = wrap_pi(g.spider_heading + delta * 0.15f);
                advance(MOVE_SPEED * speed_scale, dt_s);
            } else if (!s_gift_hold_until) {
                behaviour_set_state(SPIDER_IDLE);
                s_gift_hold_until = now_ms + 3000;
                ui_hud_bubble(gift_interaction(g.gift.kind), 0x76d6b1, 3000);
                ESP_LOGI("spidarium.behaviour", "gift interaction: %s",
                         gift_kind_name(g.gift.kind));
            } else if ((int32_t)(now_ms - s_gift_hold_until) >= 0) {
                g.mood = g.mood > 96 ? 100 : g.mood + 4;
                g.energy = g.energy > 96 ? 100 : g.energy + 4;
                s_gift_visiting = false;
                s_gift_visit_at = now_ms + 60000;
            }

            if ((int32_t)(now_ms - s_gift_visit_end) >= 0) {
                s_gift_visiting = false;
                s_gift_visit_at = now_ms + 60000;
            }
            return;
        }
    }

    switch (g.state) {

    case SPIDER_SLEEP:
        /* Wake conditions.
         *
         * The old version woke on `energy >= 95`, which meant pressing SLEEP
         * at high energy bounced straight back to IDLE -- the button appeared
         * to do nothing and the bubble said "awake". That is the "stuck on
         * awake" report.
         *
         * A player-initiated sleep now holds for a minimum duration, so the
         * button always visibly works. Automatic sleep (from exhaustion) still
         * wakes on the energy threshold.
         *
         * A SICK spider wakes much earlier: sleeping to 95 takes ~12 min at the
         * asleep regen rate, during which mood sits at 0 and it cannot feed
         * itself, which would make SICK permanent. */
        if (s_sleep_forced) {
            /* Player asked for this. Hold until the spider is actually rested,
             * with SLEEP_MIN_MS as a floor so the button always visibly works
             * even at full energy. Waking at a fixed 95 meant a spider at 80
             * energy slept for six seconds and stood up again, which is why
             * the button read as pointless. */
            if ((now_ms - s_sleep_start) >= SLEEP_MIN_MS &&
                g.energy >= SLEEP_WAKE_ENERGY) {
                s_sleep_forced = false;
                behaviour_set_state(SPIDER_IDLE);
            }
            break;
        }

        if (g.sick ? (g.energy >= 30)
                   : ((g.energy >= 95) || (g.hunger < 25))) {
            behaviour_set_state(SPIDER_IDLE);
        }
        break;

    case SPIDER_EAT:
        if ((int32_t)(now_ms - (uint32_t)g.eat_until_ms) >= 0) {
            behaviour_set_state(SPIDER_IDLE);
        }
        break;

    case SPIDER_MOLT:
        if (now_ms - s_molt_start >= 800u) behaviour_set_state(SPIDER_IDLE);
        break;

    case SPIDER_POUNCE: {
        /* Ballistic: travel toward the target over the pounce window. */
        if (!s_pouncing) {
            behaviour_set_state(SPIDER_IDLE);
            break;
        }

        float dx = (float)(s_pounce_tx - g.spider_x);
        float dy = (float)(s_pounce_ty - g.spider_y);
        float d = sqrtf(dx * dx + dy * dy);

        if (d > 2.0f) {
            g.spider_heading = atan2f(dy, dx);
            float step = 260.0f * dt_s;
            if (step > d) step = d;
            g.spider_x = (int16_t)((float)g.spider_x + dx / d * step);
            g.spider_y = (int16_t)((float)g.spider_y + dy / d * step);
            s_px = g.spider_x;
            s_py = g.spider_y;
            s_pos_init = true;
        }

        if ((int32_t)(now_ms - s_pounce_end) >= 0) {
            g.spider_x = (int16_t)s_pounce_tx;
            g.spider_y = (int16_t)s_pounce_ty;
            behaviour_sync_position();
            s_pouncing = false;
            /* Hand control to prey.c, which resolves the catch. */
            prey_resolve_pounce();
            if (g.state == SPIDER_POUNCE) behaviour_set_state(SPIDER_IDLE);
        }
        break;
    }

    case SPIDER_STALK: {
        int32_t px, py;
        if (prey_nearest(&px, &py)) {
            float d = dist_to(px, py);
            float want = atan2f((float)(py - g.spider_y), (float)(px - g.spider_x));
            /* Avoid the gift on the way to the bug. A spider that snags on it
             * while stalking never reaches the aim range and the hunt stalls,
             * which reads as the spider being stuck. */
            steer_avoiding(want, dt_s);

            /* Creep slowly while stalking. */
            advance(MOVE_SPEED * 0.55f * speed_scale, dt_s);

            /* Close enough: crouch and open the aim window. A sick spider
             * still hunts, just badly -- it must be able to feed itself or
             * SICK becomes permanent, which the spec forbids. Its catch
             * chance is already cut in prey.c. */
            if (d < AIM_RANGE_PX * gift_bonus()->prey_detection_range_mult &&
                (int32_t)(now_ms - s_next_pounce_at) >= 0) {
                s_aim_tx = px;
                s_aim_ty = py;
                s_aim_start = now_ms;
                s_aim_active = true;
                behaviour_set_state(SPIDER_AIM);
                s_aim_active = true;   /* set_state clears it */

                /* The ideal pull is the fraction of the maximum leap that
                 * would land on the bug, so the band can be coloured by how
                 * close the player is. */
                s_dragging = false;
                float max_px = POUNCE_MAX_PX * gift_bonus()->pounce_range_mult;
                s_aim_ideal = (max_px > 1.0f) ? (d / max_px) : 1.0f;
                if (s_aim_ideal > 1.0f) s_aim_ideal = 1.0f;

                audio_play(SFX_AIM);
            }
        } else {
            behaviour_set_state(SPIDER_WANDER);
        }
        break;
    }

    case SPIDER_AIM: {
        /* Hold position. Track the bug so the auto-pounce aims at where it is
         * now, not where it was when the window opened. */
        int32_t px, py;
        if (prey_nearest(&px, &py)) {
            s_aim_tx = px;
            s_aim_ty = py;
        } else {
            /* Prey vanished (cleaned away). Abort cleanly. */
            s_aim_active = false;
            behaviour_set_state(SPIDER_IDLE);
            break;
        }

        /* Face the target while crouched. */
        float want = atan2f((float)(s_aim_ty - g.spider_y),
                            (float)(s_aim_tx - g.spider_x));
        steer_toward(want, dt_s);

        /* Keep the ideal power current: the bug moves while the player aims,
         * so the target marker has to move with it. */
        {
            float max_px = POUNCE_MAX_PX * gift_bonus()->pounce_range_mult;
            float d = dist_to(s_aim_tx, s_aim_ty);
            s_aim_ideal = (max_px > 1.0f) ? (d / max_px) : 1.0f;
            if (s_aim_ideal > 1.0f) s_aim_ideal = 1.0f;
        }

        /* Window expired: auto-pounce at reduced odds. This is the safety
         * valve that keeps the loop from deadlocking when the player is
         * absent.
         *
         * The unattended lunge is deliberately SCATTERED. Aiming perfectly at
         * the bug's live position made it land at d=0-1 and then miss on the
         * roll, which reads as a broken catch rather than a missed lunge. A
         * few pixels of scatter makes the failure legible: the spider lunged
         * and fell short. */
        if ((now_ms - s_aim_start) >= aim_window_ms()) {
            s_aim_active = false;
            int32_t ax = s_aim_tx + (rand() % 25) - 12;
            int32_t ay = s_aim_ty + (rand() % 25) - 12;
            behaviour_pounce_at_ex(ax, ay, false);
        }
        break;
    }

    case SPIDER_PLAY: {
        /* Hop in place, then settle. */
        if (!spider_oneshot_active() && (now_ms - s_idle_until) > 0) {
            behaviour_set_state(SPIDER_IDLE);
        }
        break;
    }

    case SPIDER_SICK:
        /* Sluggish, but still hunts: a spider that cannot feed itself can
         * never recover, which would make SICK a de-facto death state. */
        if (g.energy < 12) {
            /* Exhausted spiders must rest or energy can never recover, since
             * hunting keeps them out of SLEEP indefinitely. */
            behaviour_set_state(SPIDER_SLEEP);
            break;
        }
        if (g.auto_hunt && prey_count() > 0) {
            behaviour_set_state(SPIDER_STALK);
            break;
        }
        if (now_ms >= s_retarget_at) {
            s_retarget_at = now_ms + 4000u;
            s_wander_dir = wander_dir_inward();
        }
        steer_avoiding(s_wander_dir, dt_s);
        advance(MOVE_SPEED * 0.35f, dt_s);
        break;

    case SPIDER_WANDER: {
        /* Wandering must be interruptible. Without this check the spider
         * wandered forever and never hunted, because only IDLE looked for
         * prey and WANDER had no exit when it had no target (observed: 3 min
         * in WANDER with prey=1 and hunger falling). */
        if (g.auto_hunt && prey_count() > 0 && g.hunger < HUNGER_FULL && !g.sick) {
            behaviour_set_state(SPIDER_STALK);
            break;
        }

        if (s_has_target) {
            float d = dist_to(s_target_x, s_target_y);
            if (d < ARRIVE_RADIUS) {
                s_has_target = false;
                behaviour_set_state(SPIDER_IDLE);
                break;
            }
            float want = atan2f((float)(s_target_y - g.spider_y),
                                (float)(s_target_x - g.spider_x));
            steer_avoiding(want, dt_s);
            advance(MOVE_SPEED * speed_scale, dt_s);
        } else {
            if (now_ms >= s_retarget_at) {
                s_retarget_at = now_ms + WANDER_RETARGET_MS;
                s_wander_dir = wander_dir_inward();
            }
            steer_avoiding(s_wander_dir, dt_s);
            advance(MOVE_SPEED * speed_scale, dt_s);
        }

        /* Stuck detector. Sliding along a wall is stable, but a spider pressed
         * into a corner can still sit there while its heading points outward.
         * If it has not actually moved for a while, pick a fresh random target
         * rather than aiming at the centre - aiming at the centre made every
         * recovery leg converge on the middle of the tank. */
        if (fabsf(s_px - s_stuck_x) < 1.5f && fabsf(s_py - s_stuck_y) < 1.5f) {
            if (now_ms - s_stuck_at > 1500u) {
                s_wander_dir = wander_dir_inward();
                s_has_target = false;
                s_stuck_at = now_ms;
                s_stuck_x = s_px;
                s_stuck_y = s_py;
            }
        } else {
            s_stuck_at = now_ms;
            s_stuck_x = s_px;
            s_stuck_y = s_py;
        }
        break;
    }

    case SPIDER_IDLE:
    default: {

        /* Micro-behaviour: glance around occasionally.
         *
         * The heading is NOT rotated while idle. The legs are drawn relative to
         * it, so even a slow turn sweeps the whole leg fan around while the
         * body (a circle) looks stationary -- which reads as the legs moving on
         * a stopped spider. Idle now holds its heading; the glance is expressed
         * by the eyes instead. */
        /* Capture expiry BEFORE refreshing the deadline. The old code pushed
         * s_idle_until into the future here and then tested it below, so the
         * wander branch could never fire and the spider sat in IDLE forever
         * whenever no prey was present (observed: pinned at (396,410) for
         * 90+ s). This is the "frozen spider" failure the handoff warned
         * about. */
        bool idle_done = (now_ms >= s_idle_until);
        if (idle_done) {
            s_idle_until = now_ms + 1200u + (uint32_t)(rand() % 1800);
        }

        /* A bonded spider turns to face the last tap even while resting.
         * This is the visible payoff for bond 25: the spider notices you. */
        if (g.bond >= BOND_TURN_TO_TAP && s_tap_heading_valid) {
            float delta = wrap_pi(s_tap_heading - g.spider_heading);
            g.spider_heading = wrap_pi(g.spider_heading + delta * 0.12f);
        }

        /* Decide what to do next. */
        if (g.sick) {
            behaviour_set_state(SPIDER_SICK);
        } else if (g.energy < game_temperament()->sleep_threshold) {
            behaviour_set_state(SPIDER_SLEEP);
        } else if (g.mood < MOOD_REFUSE_HUNT && g.hunger > MOOD_HUNGER_OVERRIDE) {
            /* Bored spiders will not hunt. This chains the verbs:
             * play -> hunt -> sleep -> clean.
             *
             * Two guards keep this from deadlocking:
             *  - SICK spiders are exempt (handled above), because a sick
             *    spider must be able to feed itself.
             *  - HUNGER OVERRIDES BOREDOM. Without this, a spider with low
             *    mood and low stats refuses to hunt, mood drifts toward the
             *    same low average, and it starves itself into SICK before the
             *    gate ever lifts. Hunger is the stronger drive. */
            if (idle_done) behaviour_set_state(SPIDER_WANDER);
        } else if (g.auto_hunt && prey_count() > 0 && g.hunger < HUNGER_FULL) {
            /* Hunt whenever prey is present and the spider is not stuffed.
             * The old <70 gate meant a fed spider ignored prey the player had
             * just spawned with FEED, which read as a broken button. */
            behaviour_set_state(SPIDER_STALK);
        } else if (idle_done) {
            behaviour_set_state(SPIDER_WANDER);
        }
        break;
    }
    }

    /* The hard shove must NOT run during a pounce. A pounce is a committed
     * ballistic leap: nudging the landing point away from the gift moved the
     * spider off the bug it was aiming at, so catches that looked dead-on
     * missed. The pounce owns its position until it resolves. */
    if (!s_pouncing) separate_from_gift();
    clamp_to_playfield();
}

bool behaviour_gift_test(void)
{
    game_state_t original = g;
    float px = s_px, py = s_py;
    bool pos_init = s_pos_init, visiting = s_gift_visiting;
    uint32_t visit_at = s_gift_visit_at, hold = s_gift_hold_until;
    uint32_t end = s_gift_visit_end, idle = s_idle_until;
    uint32_t now = lv_tick_get();
    g.gift.used = 1; g.gift.kind = 0;
    g.gift.x = 240; g.gift.y = 240;
    g.spider_x = 245; g.spider_y = 240;
    g.state = SPIDER_IDLE; g.hunger = 80; g.energy = 50; g.mood = 50; g.sick = 0;
    s_gift_visiting = false; s_gift_visit_at = now; s_gift_hold_until = 0;
    behaviour_sync_position();
    behaviour_tick(now, 0.05f);
    bool ok = s_gift_visiting && s_gift_hold_until == now + 3000;
    behaviour_tick(now + 3001, 0.05f);
    ok = ok && !s_gift_visiting && g.energy == 54 && g.mood == 54 &&
         s_gift_visit_at == now + 3001 + 60000;
    g = original; s_px = px; s_py = py; s_pos_init = pos_init;
    s_gift_visiting = visiting; s_gift_visit_at = visit_at; s_gift_hold_until = hold;
    s_gift_visit_end = end; s_idle_until = idle;
    return ok;
}
