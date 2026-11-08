#pragma once

#include "lvgl.h"

static inline void ui_style_card(lv_obj_t *obj, uint32_t accent, bool translucent)
{
    lv_obj_set_style_radius(obj, 14, 0);
    lv_obj_set_style_border_width(obj, 2, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(accent), 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_60, 0);
    lv_obj_set_style_bg_opa(obj, translucent ? LV_OPA_70 : LV_OPA_COVER, 0);
    lv_obj_set_style_outline_width(obj, 1, LV_STATE_PRESSED);
    lv_obj_set_style_outline_color(obj, lv_color_hex(0xf5c86b), LV_STATE_PRESSED);
    lv_obj_set_style_outline_opa(obj, LV_OPA_60, LV_STATE_PRESSED);
}
