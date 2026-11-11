#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

bool power_init(void);
void power_tick(void);
bool power_sleeping(void);
/* True while the screen is locked by the middle button. Cleared by a touch on
 * the sleep overlay as well as by power_wake(), so this is the only copy of the
 * lock state that cannot go stale. */
bool power_locked(void);
/* Force the display into the sleep overlay (screen lock). */
void power_sleep(void);
bool power_writing(void);
typedef struct {
    bool valid, present, usb, charging;
    uint16_t millivolts;
    uint8_t percent; /* 127 means unknown. */
} power_reading_t;
power_reading_t power_reading(void);
void power_status(char *text, unsigned size);

/* Estimated remaining runtime on battery, e.g. "About 4 h 20 m Left". Reports
 * "Charging" / "On USB Power" / "Battery Status Unavailable" instead when the
 * estimate does not apply. This is a heuristic from the PMIC percentage and the
 * current brightness, not a coulomb count. */
void power_runtime_text(char *text, unsigned size);

/* Screen orientation. Rotation is an LVGL transform on the active screen, so it
 * works regardless of what the panel driver does with MADCTL. The panel is
 * square, so no resolution change is needed and taps stay aligned. */
uint8_t power_rotation(void);
const char *power_rotation_name(uint8_t rotation);
bool power_rotation_set(uint8_t rotation);
void power_rotation_cycle(void);
/* Re-apply the stored rotation to a freshly created screen. */
void power_rotation_apply(lv_obj_t *screen);

/* The object every UI layer should be parented to. */
lv_obj_t *power_rotation_layer(lv_obj_t *screen);

bool power_test(void);
bool power_ui_test(void);
void power_wake(void);
const char *power_mode_name(uint8_t value);
const char *power_timeout_name(uint8_t value);
const char *power_delay_name(uint8_t value);
const char *power_wake_name(uint8_t value);
/* LVGL task only: snapshot live state and freeze input until reboot/failure. */
bool power_request_deep_sleep(uint32_t seconds);
bool power_sleep_pending(void);
/* Internal-stack save worker only, after the snapshot is committed. */
bool power_enter_deep_sleep(uint32_t seconds);
void power_sleep_failed(void);
