/*
 * Spidarium — first-start setup.
 *
 * Shown once, when the spider has no name yet. The player names it and picks a
 * body colour. Both are stored in the save, so this never appears again unless
 * the save is erased.
 */
#pragma once

#include "lvgl.h"

#include <stdbool.h>

/* Build the setup screen. Call once at init. */
void ui_setup_init(void);

/* Show the setup screen. */
void ui_setup_show(void);

/* True if the player still needs to complete setup. */
bool ui_setup_needed(void);
