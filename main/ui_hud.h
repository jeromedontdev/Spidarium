/*
 * Spidarium — terrarium HUD.
 *
 * Corner stat rings, the action tray, and tap-to-pet. The playfield itself is
 * full-bleed; the HUD floats over it in the safe areas.
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

/* Gift picker. Pressing GIFT opens a menu of the gift kinds; choosing one
 * closes the menu and enters placement mode. The menu is built once at init
 * and only shown/hidden -- never created or deleted per press. */
void ui_hud_gift_menu_show(void);
void ui_hud_gift_menu_hide(void);
bool ui_hud_gift_menu_visible(void);

/* Auto-hiding chrome, in two independent groups.
 *
 * The TOP band reveals the stats; the BOTTOM band reveals the action tray.
 * They are separate so reaching for one never pops the other. */
void ui_hud_reveal_top(void);      /* show the stats and restart their timer */
void ui_hud_reveal_bot(void);      /* show the actions and restart their timer */
void ui_hud_set_top(bool on);
void ui_hud_set_bot(bool on);
bool ui_hud_chrome_visible(void);  /* true while either group is showing */

/* Hide the chrome if it is showing. Called when the player taps the tank, so
 * the tray can be dismissed by tapping anywhere that is not the tray. */
void ui_hud_dismiss_if_visible(void);
void ui_hud_set_chrome(bool on);   /* force both (used by the picker) */
