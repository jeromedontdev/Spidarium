/*
 * Spidarium — generic list menu.
 *
 * One reusable modal list, so every future menu (gifts, themes, settings,
 * species, whatever) is a few lines rather than a new hand-built panel. This is
 * the foundation for moving the action buttons and stats off the main screen.
 *
 * Usage:
 *   ui_menu_open("give a gift", items, count, on_pick);
 *
 * The menu is built on open and destroyed on close. That is safe because it is
 * only ever opened from an event callback, never from the render loop.
 */
#pragma once

#include "lvgl.h"

#include <stdbool.h>
#include <stdint.h>

/* One row. */
typedef struct {
    const char *label;     /* primary text */
    const char *detail;    /* secondary text, may be NULL */
    bool        enabled;   /* false = greyed and unselectable */
    bool        selected;  /* true = highlighted as the current choice */
    bool        is_submenu;/* true = show a chevron */
    uint8_t     id;        /* passed back to the callback */
} ui_menu_item_t;

/* Called when a row is tapped. `id` is the item's id. */
typedef void (*ui_menu_pick_cb)(uint8_t id);

/* Called when the player taps BACK. If NULL, BACK closes the menu. */
typedef void (*ui_menu_back_cb)(void);

/* Open the menu. Copies nothing: the caller's array must stay valid while the
 * menu is open (use a static array). */
void ui_menu_open(const char *title, const ui_menu_item_t *items, int count,
                  ui_menu_pick_cb cb);

/* Set the BACK handler for the currently open menu. Call after ui_menu_open.
 * Without this, BACK simply closes the menu. */
void ui_menu_set_back(ui_menu_back_cb cb);

/* Close and destroy the menu. */
void ui_menu_close(void);

/* True while a menu is open. */
bool ui_menu_visible(void);
