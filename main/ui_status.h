/*
 * Spidarium — status page.
 *
 * Big bars, stage, age, XP/level, achievements and settings. A separate LVGL
 * screen, switched by the status button (gesture recognition is disabled).
 */
#pragma once

#include "game.h"
#include "lvgl.h"

/* Build the status screen (not loaded). */
void ui_status_init(void);

/* Load the status screen. */
void ui_status_show(void);

/* Return to the terrarium. */
void ui_status_hide(void);

/* True if the status screen is currently active. */
bool ui_status_visible(void);

/* Refresh values. Called from the render tick while visible. */
void ui_status_tick(uint32_t now_ms);
