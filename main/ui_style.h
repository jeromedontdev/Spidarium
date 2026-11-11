#pragma once

#include "lvgl.h"

#define UI_PIXEL_RADIUS 2
#define UI_PIXEL_INK 0x0a1220

static inline void ui_style_card(lv_obj_t *obj, uint32_t accent, bool translucent)
{
    lv_obj_set_style_radius(obj, UI_PIXEL_RADIUS, 0);
    lv_obj_set_style_border_width(obj, 2, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(accent), 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 2, 0);
    lv_obj_set_style_outline_color(obj, lv_color_hex(UI_PIXEL_INK), 0);
    lv_obj_set_style_outline_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_outline_pad(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, translucent ? LV_OPA_70 : LV_OPA_COVER, 0);
    lv_obj_set_style_outline_width(obj, 2, LV_STATE_PRESSED);
    lv_obj_set_style_outline_color(obj, lv_color_hex(0xf5c86b), LV_STATE_PRESSED);
    lv_obj_set_style_outline_opa(obj, LV_OPA_COVER, LV_STATE_PRESSED);
}
