/*
 * Spidarium — sprite table.
 *
 * Maps (view, anim, frame) to the generated asset enum. Views are n/e/s only;
 * west is the east sprite mirrored at runtime, because LV_USE_MATRIX is off
 * and lv_image_set_rotation is therefore unavailable.
 */
#pragma once

#include "game.h"
#include "lvgl.h"

#include <stdbool.h>

/* Cardinal view derived from a continuous heading. */
typedef enum {
    VIEW_N = 0,
    VIEW_E,
    VIEW_S,
    VIEW_W,      /* rendered as VIEW_E mirrored */
} sprite_view_t;

/* Snap a heading in radians to the nearest cardinal view. */
sprite_view_t sprite_view_for_heading(float heading_rad);

/* Number of frames in a clip. */
int sprite_frame_count(spider_anim_t anim);

/* Frame period in ms for a clip. */
uint32_t sprite_frame_ms(spider_anim_t anim);

/* True if the clip loops. */
bool sprite_anim_loops(spider_anim_t anim);

/* Resolve a spider frame to an asset index, or -1 if unavailable. */
int sprite_spider_index(sprite_view_t view, spider_anim_t anim, int frame);

/* Resolve a prey frame to an asset index, or -1 if unavailable. */
int sprite_prey_index(prey_species_t species, bool fleeing, bool dead, int frame);

/* Resolve a UI icon to an asset index, or -1 if unavailable. */
int sprite_icon_index(const char *name);

/* Resolve one ACH_* bit to its dedicated badge, or -1 for an invalid bit. */
int sprite_achievement_index(uint32_t bit);

/* Resolve a gift kind or named terrarium prop to its asset index. */
int sprite_gift_index(uint8_t kind);
int sprite_decor_index(const char *name);
int sprite_background_index(uint8_t scene);
