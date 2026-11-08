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

/* Update the gift-placement hint. */
void ui_hud_set_gift_mode(bool on);

/* Gift picker uses the shared four-card paged menu. Picking enters placement. */
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
