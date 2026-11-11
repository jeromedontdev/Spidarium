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
 * Four navigation/control tiles or six catalogue tiles per page.
 * Open/close and all callbacks run exclusively on the LVGL task.
 */
#pragma once

#include "lvgl.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

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
void ui_menu_open_grid(const char *title, const ui_menu_item_t *items, int count,
                       ui_menu_pick_cb cb);
void ui_menu_open_detail(const char *title, const ui_menu_item_t *items, int count,
                         ui_menu_pick_cb cb);

/* Set the BACK handler for the currently open menu. Call after ui_menu_open.
 * Without this, BACK simply closes the menu. */
void ui_menu_set_back(ui_menu_back_cb cb);

/* Close and destroy the menu. */
void ui_menu_close(void);

/* True while a menu is open. */
bool ui_menu_visible(void);
bool ui_menu_showing(const ui_menu_item_t *items);

typedef bool (*ui_menu_input_cb)(const char *text);
/* Submit returning false leaves the editor open with a validation message. */
void ui_menu_open_input(const char *title, const char *initial, bool password,
                       uint32_t max_length, ui_menu_input_cb submit);

/* LVGL-task diagnostic exercising paging and password editing without credentials. */
bool ui_menu_test(void);
void ui_menu_format_name(char *dst, size_t size, const char *name);
void ui_menu_set_destructive(uint8_t id);
void ui_menu_set_item_icon(uint8_t item, int asset_index);
void ui_menu_set_item_checkbox(uint8_t item);

/* Turn a row into a 0..100 slider. The row's `selected` flag is ignored; the
 * slider shows `value` and reports every change through `cb` with the new
 * 0..100 value. The row's own pick callback is not used. */
typedef void (*ui_menu_slider_cb)(uint8_t id, uint8_t value);
void ui_menu_set_item_slider(uint8_t item, uint8_t value, ui_menu_slider_cb cb);

/* Move an already-registered slider row to a new value and redraw it. Used when
 * something outside the menu changes the value (the physical volume buttons).
 * Does nothing if the row is not a slider. */
void ui_menu_set_slider_value(uint8_t item, uint8_t value);

/* Rebuild the current page from the item table, so a caller that changed a
 * value behind the menu's back (the physical volume buttons) can refresh the
 * slider and its readout without closing and reopening the menu. */
void ui_menu_refresh(void);
int ui_menu_page(void);
void ui_menu_set_page(int page);
void ui_menu_capture_hide(bool hidden);
typedef uint32_t (*ui_minigame_reward_cb)(uint8_t score);
void ui_menu_open_minigame(ui_minigame_reward_cb reward);
