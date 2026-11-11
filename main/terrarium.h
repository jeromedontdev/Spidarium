/*
 * Spidarium — terrarium background.
 *
 * Eight authored pixel-art images in a reusable PSRAM RGB565 canvas.
 * No mandatory props, large web overlay or randomly moving dust.
 */
#pragma once

#include "game.h"
#include "lvgl.h"

/* Build the static background into `parent`. Call once after the screen
 * exists and the playfield is known. */
void terrarium_init(lv_obj_t *parent);

/* Rebuild the gift layer (after giving or replacing the gift). */
void terrarium_refresh_gift(void);

/* Rebuild the decor layer (after placing, removing or toggling an item). */
void terrarium_refresh_decor(void);

/* Rebuild the background after a theme change. */
void terrarium_rebuild(void);
bool terrarium_test(void);

/* Draw a poop blob at playfield coords. */
void terrarium_add_poop(int32_t x, int32_t y);

/* Draw a small pale speckle left by a bug that has been in the tank too long.
 * Worth less dirt than a spider dropping. */
void terrarium_add_bug_poop(int32_t x, int32_t y);

/* Remove all poop blobs. */
void terrarium_clear_poop(void);

/* Number of poop blobs currently in the tank. */
int terrarium_poop_count(void);

/* Total dirt points in the tank. Clean pays in proportion to this, so the
 * reward tracks the mess actually removed rather than the tap. */
int terrarium_dirt(void);

/* Placement preview: a faint ring at the tap point showing where the gift will
 * go before it is committed. The ring object is created once and only
 * shown/hidden -- never created or deleted per frame. */
void terrarium_preview_show(int32_t x, int32_t y, int32_t radius);
void terrarium_preview_hide(void);

/* Decompose care-related waste. The background does not animate. */
void terrarium_tick(uint32_t now_ms);
