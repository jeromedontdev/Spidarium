/*
 * Spidarium — physical side buttons.
 *
 * The Waveshare ESP32-S3-Touch-AMOLED-2.16 exposes three buttons on the board
 * edge. The BSP declares BSP_CAPS_BUTTONS 0 and never configures them, so this
 * module owns the GPIO setup.
 *
 *   top middle  -> lock / unlock the screen
 *   left        -> volume down
 *   right       -> volume up
 *
 * Buttons are polled from the existing 20 Hz logic tick rather than an ISR:
 * the actions they trigger (LVGL sleep overlay, codec volume) must not run in
 * interrupt context, and a 50 ms poll is far below human double-tap speed.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Configure the button GPIOs. Safe to call once after the display is up. */
bool buttons_init(void);

/* Poll for edges. Call from the logic tick; cheap when nothing changed. */
void buttons_tick(uint32_t now_ms);

/* True while the screen is locked by the top button. */
bool buttons_locked(void);

/* Lock or unlock from code (serial console, tests). */
void buttons_set_locked(bool locked);

/* Report the raw and debounced level of every button, plus the polarity the
 * driver assumes. Lets the console prove the GPIO16 wiring without a press. */
void buttons_report(void);
