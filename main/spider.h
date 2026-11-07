/*
 * Spidarium — spider rendering.
 *
 * Draws the spider procedurally: a body container plus eight jointed
 * two-segment legs as lv_line polylines, animated with a tetrapod gait.
 * Position and heading come from the behaviour module; this module only turns
 * them into pixels.
 */
#pragma once

#include "game.h"
#include "lvgl.h"

#include <stdbool.h>

/* Create the spider objects. */
void spider_init(lv_obj_t *parent);

/* Push the current state to the display. Called from the render tick. */
void spider_render(uint32_t now_ms);

/* Play a one-shot clip (pounce, molt). Ignored if a one-shot is already
 * running, unless `force` is set. */
void spider_play_once(spider_anim_t anim, bool force);

/* True while a one-shot clip is still playing. */
bool spider_oneshot_active(void);

/* Vertical offset applied by the hop/pounce arc, in px (negative = up). */
int32_t spider_render_dy(void);

/* Start a ballistic arc (used by the pounce). */
void spider_launch_pounce(int32_t height, uint32_t ms);

/* Emotional expression through the LEGS.
 *
 * The spider used to narrate itself with text bubbles ("hungry", "sleepy"),
 * which read as noise. It now emotes physically: the legs wave, droop, splay or
 * tuck. This is additive on top of the gait, so it never breaks movement. */
typedef enum {
    EMOTE_NONE = 0,
    EMOTE_HAPPY,     /* front legs lift and wave */
    EMOTE_SAD,       /* all legs droop */
    EMOTE_ALERT,     /* legs splay, stiff */
    EMOTE_SLEEPY,    /* legs tucked in */
    EMOTE_COUNT,
} spider_emote_t;

/* Set the current emote. EMOTE_NONE clears it. */
void spider_set_emote(spider_emote_t emote);

/* The current emote. */
spider_emote_t spider_emote(void);

/* Body colour palettes. The player picks one at first start. */
uint8_t     spider_colour_count(void);
const char *spider_colour_name(uint8_t idx);

/* A representative colour for a palette, for drawing a swatch. */
uint32_t    spider_colour_swatch(uint8_t idx);

/* Accent colours: pupils, legs and fangs. One shared list, so the setup screen
 * can drive all three pickers from the same code. */
uint8_t     spider_accent_count(void);
const char *spider_accent_name(uint8_t idx);
uint32_t    spider_accent_colour(uint8_t idx);

/* Re-apply the stored accent colours to the live objects. Call after the player
 * changes a picker, so the change is visible without a restart. */
void        spider_refresh_colours(void);

/* A second, static spider for the setup screen's preview. Built by the same
 * anatomy builder as the live spider, so it is the real creature rather than an
 * approximation. Call spider_preview_init() once, then spider_preview_refresh()
 * whenever a colour changes. */
void        spider_preview_init(lv_obj_t *parent);
void        spider_preview_refresh(void);
