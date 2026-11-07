/*
 * Spidarium — canvas spider renderer.
 *
 * Draws the spider into a single LVGL canvas instead of composing it from ~46
 * separate lv_obj widgets. This is the architecture Pocket Tank uses, adapted:
 * the creature is a picture, not a scene graph.
 *
 * Why this is better than the widget approach it replaces:
 *
 *   - No part budget. The old renderer had MAX_PARTS = 46 and silently dropped
 *     anything past it, which ate the pupils and glints once and the fangs
 *     another time. A canvas has no such limit.
 *
 *   - No draw callbacks. The fangs needed a LV_EVENT_DRAW_MAIN callback calling
 *     lv_draw_triangle() because LVGL has no triangle widget. Here a triangle is
 *     three lines of arithmetic.
 *
 *   - No transform limits. LV_USE_MATRIX is off, so the old renderer squashed
 *     the body by moving every part individually. A canvas can just scale the
 *     coordinates.
 *
 *   - One object instead of 46. The scene graph cost of repositioning 46 widgets
 *     at 30 Hz is gone; this is one invalidate.
 *
 * The canvas is 96x96 at 1:1 and is positioned so the spider's centre lands on
 * g.spider_x/g.spider_y. Everything is drawn in body-local space and rotated by
 * the heading, exactly as before -- only the output changed.
 */
#pragma once

#include "lvgl.h"

#include <stdbool.h>
#include <stdint.h>

/* Build the canvas. Call once, after the terrarium exists. */
void cspider_init(lv_obj_t *parent);

/* Draw the spider for this frame. Replaces spider_render(). */
void cspider_render(uint32_t now_ms);

/* The canvas object, for z-order and hit-testing. */
lv_obj_t *cspider_obj(void);

/* True if the point is on the spider's body. Used for the pet/tap test. */
bool cspider_hit(int32_t x, int32_t y);

/* Emote, matching the old spider_set_emote() contract. */
typedef enum {
    CSP_EMOTE_NONE = 0,
    CSP_EMOTE_HAPPY,
    CSP_EMOTE_SAD,
    CSP_EMOTE_ALERT,
    CSP_EMOTE_SLEEPY,
} cspider_emote_t;

void cspider_set_emote(cspider_emote_t e);
