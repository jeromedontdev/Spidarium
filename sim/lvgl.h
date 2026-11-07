/*
 * Minimal LVGL stub for the headless simulator.
 *
 * game.h includes <lvgl.h> for its widget handles, so the simulator needs
 * SOMETHING called lvgl.h on the include path. This provides just enough for
 * the logic modules to compile: opaque object pointers, the handful of style
 * constants they reference, and the tick function.
 *
 * Nothing here draws. The simulator never creates a widget; it only needs the
 * types to exist so the shared headers parse.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Opaque widget handle. The simulator never dereferences one. */
typedef struct _lv_obj_t lv_obj_t;

/* Opaque timer handle, referenced by game.h's tick prototypes. */
typedef struct _lv_timer_t lv_timer_t;

typedef struct {
    int32_t x;
    int32_t y;
} lv_point_t;

typedef struct {
    int32_t x;
    int32_t y;
} lv_point_precise_t;

typedef int32_t lv_value_precise_t;

typedef struct {
    uint8_t blue;
    uint8_t green;
    uint8_t red;
} lv_color_t;

typedef uint8_t lv_opa_t;

/* Opacity constants used by the logic modules. */
#define LV_OPA_TRANSP 0
#define LV_OPA_COVER  255

/* Style properties referenced in shared headers. */
typedef enum {
    LV_PART_MAIN = 0,
    LV_PART_INDICATOR,
    LV_PART_KNOB,
} lv_part_t;

/* Event codes referenced by shared headers. */
typedef enum {
    LV_EVENT_CLICKED = 0,
    LV_EVENT_PRESSED,
    LV_EVENT_PRESSING,
    LV_EVENT_RELEASED,
    LV_EVENT_ALL,
} lv_event_code_t;

typedef struct _lv_event_t lv_event_t;

/* Functions the logic modules call. Defined in sim_main.c. */
uint32_t lv_tick_get(void);

static inline lv_color_t lv_color_hex(uint32_t c)
{
    lv_color_t col;
    col.red   = (uint8_t)((c >> 16) & 0xFF);
    col.green = (uint8_t)((c >> 8) & 0xFF);
    col.blue  = (uint8_t)(c & 0xFF);
    return col;
}

/* Widget calls: no-ops on the host. */
static inline lv_obj_t *lv_obj_create(lv_obj_t *parent) { (void)parent; return NULL; }
static inline void lv_obj_set_pos(lv_obj_t *o, int32_t x, int32_t y) { (void)o; (void)x; (void)y; }
static inline void lv_obj_set_size(lv_obj_t *o, int32_t w, int32_t h) { (void)o; (void)w; (void)h; }
static inline void lv_obj_set_hidden(lv_obj_t *o, bool h) { (void)o; (void)h; }
static inline void lv_obj_set_clickable(lv_obj_t *o, bool c) { (void)o; (void)c; }
static inline void lv_obj_set_scrollable(lv_obj_t *o, bool s) { (void)o; (void)s; }
static inline void lv_obj_remove_style_all(lv_obj_t *o) { (void)o; }
static inline void lv_obj_clean(lv_obj_t *o) { (void)o; }
static inline void lv_obj_del(lv_obj_t *o) { (void)o; }
static inline void lv_obj_del_async(lv_obj_t *o) { (void)o; }
static inline void lv_obj_invalidate(lv_obj_t *o) { (void)o; }
static inline void lv_obj_center(lv_obj_t *o) { (void)o; }
static inline void lv_obj_align(lv_obj_t *o, int a, int32_t x, int32_t y) { (void)o; (void)a; (void)x; (void)y; }
static inline void lv_obj_move_foreground(lv_obj_t *o) { (void)o; }
static inline void lv_obj_move_background(lv_obj_t *o) { (void)o; }
static inline void lv_obj_set_style_bg_color(lv_obj_t *o, lv_color_t c, int p) { (void)o; (void)c; (void)p; }
static inline void lv_obj_set_style_bg_opa(lv_obj_t *o, lv_opa_t v, int p) { (void)o; (void)v; (void)p; }
static inline void lv_obj_set_style_border_width(lv_obj_t *o, int32_t w, int p) { (void)o; (void)w; (void)p; }
static inline void lv_obj_set_style_radius(lv_obj_t *o, int32_t r, int p) { (void)o; (void)r; (void)p; }
