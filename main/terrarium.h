/*
 * Spidarium — terrarium background.
 *
 * Procedural, drawn once into a canvas: substrate gradient, glass vignette,
 * web, decor shapes and dust motes. Redrawn only when the decor set changes.
 */
#pragma once

#include "game.h"
#include "lvgl.h"

/* Build the static background into `parent`. Call once after the screen
 * exists and the playfield is known. */
void terrarium_init(lv_obj_t *parent);

/* Rebuild the gift layer (after giving or replacing the gift). */
void terrarium_refresh_gift(void);

/* Rebuild the background after a theme change. */
void terrarium_rebuild(void);

/* Draw a poop blob at playfield coords. */
void terrarium_add_poop(int32_t x, int32_t y);

/* Remove all poop blobs. */
void terrarium_clear_poop(void);

/* Number of poop blobs currently in the tank. */
int terrarium_poop_count(void);

/* Placement preview: a faint ring at the tap point showing where the gift will
 * go before it is committed. The ring object is created once and only
 * shown/hidden -- never created or deleted per frame. */
void terrarium_preview_show(int32_t x, int32_t y, int32_t radius);
void terrarium_preview_hide(void);

/* Animate the dust motes. Called from the render tick. */
void terrarium_tick(uint32_t now_ms);
