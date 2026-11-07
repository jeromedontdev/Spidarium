/*
 * Spidarium — behaviour engine.
 *
 * Steering (seek/arrive/flee), turn-rate-limited heading, decor separation and
 * the state machine. Owns the spider's position; the renderer only reads it.
 */
#pragma once

#include "game.h"

#include <stdbool.h>

/* Advance the spider one logic tick. */
void behaviour_tick(uint32_t now_ms, float dt_s);

/* Force a state transition (used by care actions). */
void behaviour_set_state(spider_state_t state);

/* Player-initiated sleep. Held for at least SLEEP_MIN_MS so the button always
 * visibly works, even at full energy. */
void behaviour_force_sleep(bool on);
bool behaviour_sleep_forced(void);

/* Ask the spider to walk to a point (used by feed). */
void behaviour_seek_to(int32_t x, int32_t y);

/* Record the direction of a tap. A bonded spider turns toward it while idle. */
void behaviour_note_tap(int32_t x, int32_t y);

/* Launch a pounce at a target point. */
void behaviour_pounce_at(int32_t x, int32_t y);

/* Pounce with an explicit origin flag. behaviour_pounce_at() is the
 * auto-pounce wrapper (by_player = false). */
void behaviour_pounce_at_ex(int32_t x, int32_t y, bool by_player);

/* True if the most recent pounce was player-initiated. Read by prey.c. */
bool behaviour_pounce_by_player(void);

/* True while the spider is crouched, waiting for the player's tap. */
bool behaviour_aim_active(void);

/* True when the spider is too full to hunt, so the HUD can explain why a pull
 * does nothing. */
bool behaviour_too_full(void);

/* Milliseconds left in the current aim window (0 if not aiming). */
uint32_t behaviour_aim_remaining_ms(void);

/* Total length of the current aim window, including level, decor and
 * temperament modifiers. The focus ring needs this to drain correctly --
 * using the raw AIM_WINDOW_MS constant makes the ring lie. */
uint32_t behaviour_aim_total_ms(void);

/* Player-initiated pounce at an explicit point. Returns false if not aiming.
 * The target is clamped to POUNCE_MAX_PX from the spider. */
bool behaviour_player_pounce(int32_t x, int32_t y);

/* --- Slingshot aim --------------------------------------------------
 * Drag back from the spider and release. The drag vector is inverted, so
 * pulling down-left fires up-right; pull length sets power. */

/* Press / move / release. Release returns true if a shot was taken. */
void behaviour_aim_drag_begin(int32_t x, int32_t y);
void behaviour_aim_drag_move(int32_t x, int32_t y);
bool behaviour_aim_release(void);

/* True while the finger is down and pulling. */
bool behaviour_aim_dragging(void);

/* The pull vector from the spider to the finger, in screen space. */
void behaviour_aim_drag_vec(float *dx, float *dy);

/* Current pull length as a fraction of the maximum, 0..1. */
float behaviour_aim_pull(void);

/* The power that would land exactly on the bug, 0..1. The HUD draws a marker
 * so the player can see how far to pull. */
float behaviour_aim_ideal_power(void);

/* Quality of the last locked shot, 0..1 (1 = perfect). Read by prey.c so
 * catch quality comes from aim rather than from a random roll. */
float behaviour_aim_quality(void);

/* True while the spider is mid-pounce. */
bool behaviour_pouncing(void);

/* True when the spider is deliberately not moving because it is out of
 * energy. The HUD uses this to explain the stillness instead of leaving the
 * player wondering whether the game has frozen. */
bool behaviour_resting(void);

/* Current sub-pixel position. The renderer uses this to measure how far the
 * spider actually moved, which is what drives the leg gait. g.spider_x/y are
 * int16_t and quantise to whole pixels, so they are too coarse for that. */
void behaviour_pos(float *x, float *y);
