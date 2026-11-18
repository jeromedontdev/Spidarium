/*
 * Spidarium — terrarium HUD.
 *
 * Top-left Care toggle reveals stats/actions; the hunting field stays clear.
 */
#pragma once

#include "game.h"
#include "lvgl.h"

/* Build the HUD onto the terrarium screen. */
void ui_hud_init(lv_obj_t *parent);

/* Refresh the rings and labels. Called from the render tick. */
void ui_hud_tick(uint32_t now_ms);

/* Show a transient bubble above the spider ("yum!", "happy!"). */
void ui_hud_bubble(const char *text, uint32_t color, uint32_t ms);

/* Show/hide the gift-placement hint and Done/Cancel controls. */
void ui_hud_set_gift_mode(bool on);

/* Shared placement controls. Passing NULL keeps the current hint text. */
void ui_hud_set_place_mode(bool on, const char *hint);

/* Re-read the staged placement state and refresh the layer row.
 *
 * Called as the item is dragged, because the layer count depends on which items
 * the item currently overlaps -- and that changes under the player's finger.
 * Without this the row only updated on a button press, so the control appeared
 * dead until Done was pressed and the item picked up again. */
void ui_hud_placement_changed(void);

/* The state of the top item selector, for the self-test.
 *
 * The selector must be VISIBLE whenever an existing item is being moved, even
 * with only one item placed, and its arrows must be DISABLED in that case. Both
 * halves matter: hiding the row read as the control having been removed, and
 * leaving the arrows live with nothing to step to read as a dead button. */
void ui_hud_placement_selector_state(bool *visible, bool *enabled);

/* The state of the bottom layer row, for the self-test.
 *
 * The row must be HIDDEN when the item being moved does not overlap anything in
 * its band. Draw order is only visible where two sprites cover the same pixels,
 * so offering the control against an item at the far end of the tank implied a
 * relationship that did not exist. */
void ui_hud_placement_layer_state(bool *visible, bool *enabled);

/* Gift picker uses the six-tile catalogue grid. Picking enters placement. */
void ui_hud_gift_menu_show(void);
void ui_hud_gift_menu_hide(void);
bool ui_hud_gift_menu_visible(void);

/* Explicit visibility controls for diagnostics; gameplay never auto-hides the HUD. */
void ui_hud_reveal_top(void);
void ui_hud_reveal_bot(void);
void ui_hud_set_top(bool on);
void ui_hud_set_bot(bool on);
bool ui_hud_chrome_visible(void);  /* true while either group is showing */

/* Hide the chrome if it is showing. Called when the player taps the tank, so
 * the tray can be dismissed by tapping anywhere that is not the tray. */
void ui_hud_dismiss_if_visible(void);
void ui_hud_set_chrome(bool on);   /* force both (used by the picker) */
bool ui_hud_test(void);
void ui_hud_capture_hide(bool hidden);
