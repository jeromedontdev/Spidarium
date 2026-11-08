#include "ui_hud.h"

#include "assets.h"
#include "behaviour.h"
#include "spider.h"
#include "cspider.h"
#include "game_config.h"
#include "gift.h"
#include "needs.h"
#include "sprites.h"
#include "ui_menu.h"
#include "species.h"
#include "power.h"
#include "prey.h"
#include "ui_style.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COL_PANEL 0x101c2c
#define COL_TEXT 0xe8efda
#define COL_DIM 0x9aaeb5

static lv_obj_t *s_stats[NUM_STATS], *s_values[NUM_STATS], *s_bars[NUM_STATS];
static lv_obj_t *s_level, *s_progress, *s_resources, *s_hint, *s_bubble, *s_bubble_label;
static lv_obj_t *s_care_button, *s_care_label;
static lv_obj_t *s_layer;
static lv_obj_t *s_identity, *s_battery, *s_battery_fill, *s_battery_text, *s_charge;
static uint32_t s_battery_key = UINT32_MAX;
static uint32_t s_bubble_until, s_last_refresh;
static bool s_top_visible, s_bot_visible;
void ui_hud_capture_hide(bool hidden)
{
    static bool was_hidden[4];
    lv_obj_t *objects[] = {s_layer, s_battery, s_care_button, s_bubble};
    for (unsigned i = 0; i < 4; i++) {
        if (hidden) was_hidden[i] = lv_obj_is_hidden(objects[i]);
        lv_obj_set_hidden(objects[i], hidden || was_hidden[i]);
    }
}
static ui_menu_item_t s_gifts[GIFT_KINDS];
static char s_gift_detail[GIFT_KINDS][112];
static char s_gift_name[GIFT_KINDS][32];
static const uint32_t k_colors[] = {0xf5c86b, 0x76d6b1, 0x99b4ff, 0x68d5df};
static const char *const k_names[] = {"Food", "Mood", "Energy", "Clean"};

static void on_care(lv_event_t *e)
{
    (void)e;
    if (g.gift_mode) { game_on_gift(NULL); return; }
    behaviour_aim_cancel();
    ui_hud_set_chrome(!ui_hud_chrome_visible());
}

static void on_battery(lv_event_t *e)
{
    (void)e;
    behaviour_aim_cancel();
    game_open_menu("power");
}

static void battery_refresh(void)
{
    power_reading_t reading = power_reading();
    uint32_t key = reading.percent | (reading.valid << 8) | (reading.present << 9) |
                   (reading.usb << 10) | (reading.charging << 11);
    if (key == s_battery_key) return;
    s_battery_key = key;
    bool known = reading.valid && reading.present && reading.percent <= 100;
    uint32_t color = reading.charging ? 0xf5c86b :
                     known && reading.percent <= 10 ? 0xff7777 : 0x76d6b1;
    lv_bar_set_value(s_battery_fill, known ? reading.percent : 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_battery_fill, lv_color_hex(color), LV_PART_INDICATOR);
    char text[12];
    if (known) snprintf(text, sizeof text, "%u%%", reading.percent);
    else snprintf(text, sizeof text, "%s", reading.valid && reading.usb ? "USB" : "?");
    lv_label_set_text(s_battery_text, text);
    lv_obj_set_hidden(s_charge, !reading.charging);
}

static void care_fold_draw(lv_event_t *e)
{
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target_obj(e), &area);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_draw_triangle_dsc_t triangle;
    lv_draw_triangle_dsc_init(&triangle);
    triangle.color = lv_color_hex(0x000000);
    triangle.opa = LV_OPA_60;
    triangle.p[0] = (lv_point_precise_t){area.x2, area.y2};
    triangle.p[1] = (lv_point_precise_t){area.x2, area.y1};
    triangle.p[2] = (lv_point_precise_t){area.x1, area.y2};
    lv_draw_triangle(layer, &triangle);
    triangle.color = lv_color_hex(0x203044);
    triangle.opa = LV_OPA_60;
    triangle.p[1].y += 4;
    triangle.p[2].x += 4;
    lv_draw_triangle(layer, &triangle);
    triangle.color = lv_color_hex(COL_DIM);
    triangle.opa = LV_OPA_20;
    triangle.p[1] = (lv_point_precise_t){area.x2 - 18, area.y2 - 18};
    lv_draw_triangle(layer, &triangle);
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                            uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_clickable(obj, false);
    return obj;
}

static lv_obj_t *make_action(lv_obj_t *parent, int x, int y, int w,
                             const char *icon, const char *name, lv_event_cb_t cb)
{
    lv_obj_t *obj = lv_button_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, w, BTN_H);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_bg_color(obj, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    ui_style_card(obj, 0x76b9ad, true);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x335568), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x1b2530), LV_STATE_DISABLED);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x39434c), LV_STATE_DISABLED);
    lv_obj_set_scrollable(obj, false);
    lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, NULL);
    int index = sprite_icon_index(icon);
    const lv_image_dsc_t *image = index >= 0 ? assets_image(index) : NULL;
    if (image) {
        lv_obj_t *img = lv_image_create(obj);
        lv_image_set_src(img, image);
        lv_obj_set_pos(img, 2, (BTN_H - 48) / 2);
        lv_obj_set_clickable(img, false);
    }
    lv_obj_t *text = make_label(obj, name, &lv_font_montserrat_14, COL_TEXT);
    lv_obj_align(text, LV_ALIGN_RIGHT_MID, -8, 0);
    return obj;
}

static void on_gift_pick(uint8_t id) { game_gift_chosen(id); }

void ui_hud_gift_menu_show(void)
{
    for (int i = 0; i < GIFT_KINDS; i++) {
        bool unlocked = gift_unlocked(i);
        bool current = gift_present() && gift_kind() == i;
        snprintf(s_gift_detail[i], sizeof s_gift_detail[i], "%s\n%s",
                 current ? "In Tank / Move Free" : gift_owned(i) ? "Owned / Place Free" : "",
                 gift_kind_effect(i));
        if (!gift_owned(i))
            snprintf(s_gift_detail[i], sizeof s_gift_detail[i], "%lu Silk / %s\n%s",
                     (unsigned long)gift_cost(i), g.silk >= gift_cost(i) ? "Buy" : "Earn More Silk",
                     gift_kind_effect(i));
        if (!unlocked)
            snprintf(s_gift_detail[i], sizeof s_gift_detail[i], "Level %lu / %lu Silk\n%s",
                     (unsigned long)gift_unlock_level(i), (unsigned long)gift_cost(i),
                     gift_kind_effect(i));
        ui_menu_format_name(s_gift_name[i], sizeof s_gift_name[i], gift_kind_name(i));
        s_gifts[i] = (ui_menu_item_t){s_gift_name[i], s_gift_detail[i],
                                     gift_can_place(i), current, false, i};
    }
    static char title[48];
    snprintf(title, sizeof title, "Gifts / %lu Silk", (unsigned long)g.silk);
    ui_menu_open(title, s_gifts, GIFT_KINDS, on_gift_pick);
}

void ui_hud_gift_menu_hide(void)
{
    if (ui_menu_showing(s_gifts)) ui_menu_close();
}

bool ui_hud_gift_menu_visible(void) { return ui_menu_showing(s_gifts); }

void ui_hud_init(lv_obj_t *parent)
{
    s_layer = lv_obj_create(parent);
    lv_obj_remove_style_all(s_layer);
    lv_obj_set_size(s_layer, ui.W, ui.H);
    lv_obj_set_clickable(s_layer, false);
    lv_obj_set_scrollable(s_layer, false);
    parent = s_layer;
    int top = 8;
    int w = (ui.W - 2 * (UI_CORNER + 40) - (NUM_STATS - 1) * 6) / NUM_STATS;
    int left = (ui.W - (NUM_STATS * w + (NUM_STATS - 1) * 6)) / 2;
    for (int i = 0; i < NUM_STATS; i++) {
        s_stats[i] = lv_obj_create(parent);
        lv_obj_remove_style_all(s_stats[i]);
        lv_obj_set_pos(s_stats[i], left + i * (w + 6), top);
        lv_obj_set_size(s_stats[i], w, 44);
        lv_obj_set_style_bg_color(s_stats[i], lv_color_hex(COL_PANEL), 0);
        lv_obj_set_style_bg_opa(s_stats[i], LV_OPA_COVER, 0);
        ui_style_card(s_stats[i], k_colors[i], true);
        lv_obj_set_clickable(s_stats[i], false);
        lv_obj_set_scrollable(s_stats[i], false);
        lv_obj_t *name = make_label(s_stats[i], k_names[i], &lv_font_montserrat_12, COL_DIM);
        lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 3);
        s_values[i] = make_label(s_stats[i], "100", &lv_font_montserrat_12, k_colors[i]);
        lv_obj_align(s_values[i], LV_ALIGN_TOP_MID, 0, 17);
        s_bars[i] = lv_bar_create(s_stats[i]);
        lv_obj_remove_style_all(s_bars[i]);
        lv_obj_set_size(s_bars[i], w - 8, 6);
        lv_obj_align(s_bars[i], LV_ALIGN_BOTTOM_MID, 0, -4);
        lv_obj_set_style_bg_color(s_bars[i], lv_color_hex(0x30404b), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_bars[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_bars[i], lv_color_hex(k_colors[i]), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(s_bars[i], LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_clickable(s_bars[i], false);
        lv_bar_set_range(s_bars[i], 0, 100);
        lv_bar_set_value(s_bars[i], 100, LV_ANIM_OFF);
    }
    s_identity = lv_obj_create(parent);
    lv_obj_remove_style_all(s_identity);
    lv_obj_set_size(s_identity, ui.W - 2 * (UI_CORNER + 20), 72);
    lv_obj_align(s_identity, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_style_bg_color(s_identity, lv_color_hex(COL_PANEL), 0);
    ui_style_card(s_identity, 0xb99b67, true);
    lv_obj_set_clickable(s_identity, false);
    lv_obj_set_scrollable(s_identity, false);
    s_level = make_label(s_identity, "", &lv_font_montserrat_12, COL_TEXT);
    lv_obj_set_width(s_level, ui.W - 2 * (UI_CORNER + 20) - 12);
    lv_obj_set_style_text_align(s_level, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_level, LV_LABEL_LONG_DOT);
    lv_obj_align(s_level, LV_ALIGN_TOP_MID, 0, 4);
    s_progress = make_label(s_identity, "", &lv_font_montserrat_12, COL_TEXT);
    lv_obj_set_width(s_progress, ui.W - 2 * (UI_CORNER + 20) - 12);
    lv_obj_set_style_text_align(s_progress, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_progress, LV_LABEL_LONG_DOT);
    lv_obj_align(s_progress, LV_ALIGN_TOP_MID, 0, 26);
    s_resources = make_label(s_identity, "", &lv_font_montserrat_12, COL_DIM);
    lv_obj_set_width(s_resources, ui.W - 2 * (UI_CORNER + 20) - 12);
    lv_obj_set_style_text_align(s_resources, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_resources, LV_LABEL_LONG_DOT);
    lv_obj_align(s_resources, LV_ALIGN_TOP_MID, 0, 48);
    s_battery = lv_button_create(parent);
    lv_obj_remove_style_all(s_battery);
    lv_obj_set_size(s_battery, 52, 44);
    lv_obj_set_pos(s_battery, ui.W - UI_CORNER - 28, 8);
    lv_obj_set_style_bg_opa(s_battery, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_battery, 0, 0);
    lv_obj_set_style_pad_all(s_battery, 0, 0);
    lv_obj_set_scrollable(s_battery, false);
    lv_obj_add_event_cb(s_battery, on_battery, LV_EVENT_CLICKED, NULL);
    s_battery_fill = lv_bar_create(s_battery);
    lv_obj_remove_style_all(s_battery_fill);
    lv_obj_set_size(s_battery_fill, 34, 17);
    lv_obj_set_pos(s_battery_fill, 6, 5);
    lv_obj_set_style_border_width(s_battery_fill, 1, 0);
    lv_obj_set_style_border_color(s_battery_fill, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_bg_color(s_battery_fill, lv_color_hex(0x30404b), 0);
    lv_obj_set_style_bg_opa(s_battery_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_battery_fill, 2, 0);
    lv_obj_set_style_bg_opa(s_battery_fill, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_bar_set_range(s_battery_fill, 0, 100);
    lv_obj_set_clickable(s_battery_fill, false);
    lv_obj_t *terminal = lv_obj_create(s_battery);
    lv_obj_remove_style_all(terminal);
    lv_obj_set_size(terminal, 3, 7);
    lv_obj_set_pos(terminal, 40, 10);
    lv_obj_set_style_bg_color(terminal, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_bg_opa(terminal, LV_OPA_COVER, 0);
    lv_obj_set_clickable(terminal, false);
    s_charge = make_label(s_battery, LV_SYMBOL_CHARGE, &lv_font_montserrat_12, COL_PANEL);
    lv_obj_set_pos(s_charge, 16, 5);
    s_battery_text = make_label(s_battery, "?", &lv_font_montserrat_12, COL_TEXT);
    lv_obj_align(s_battery_text, LV_ALIGN_BOTTOM_MID, 0, -1);
    int bw = (ui.W - 2 * UI_CORNER - 16) / 3;
    int y = ui.H - UI_CORNER - 2 * BTN_H - 8;
    ui.btn_feed = make_action(parent, UI_CORNER, y, bw, "feed", "Feed", game_on_feed);
    ui.btn_clean = make_action(parent, UI_CORNER + bw + 8, y, bw, "clean", "Clean", game_on_clean);
    ui.btn_play = make_action(parent, UI_CORNER + 2 * (bw + 8), y, bw, "play", "Play", game_on_play);
    y += BTN_H + 8;
    ui.btn_sleep = make_action(parent, UI_CORNER, y, bw, "sleep", "Sleep", game_on_sleep);
    ui.btn_decor = make_action(parent, UI_CORNER + bw + 8, y, bw, "decor", "Gift", game_on_gift);
    ui.btn_theme = make_action(parent, UI_CORNER + 2 * (bw + 8), y, bw, "status", "Menu", game_on_menu);

    s_care_button = lv_button_create(parent);
    lv_obj_remove_style_all(s_care_button);
    lv_obj_set_size(s_care_button, 44, 44);
    lv_obj_set_ext_click_area(s_care_button, 12);
    lv_obj_set_pos(s_care_button, -1, -1);
    lv_obj_set_style_bg_opa(s_care_button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_care_button, 0, 0);
    lv_obj_set_scrollable(s_care_button, false);
    lv_obj_add_event_cb(s_care_button, on_care, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_care_button, care_fold_draw, LV_EVENT_DRAW_MAIN, NULL);
    s_care_label = make_label(s_care_button, LV_SYMBOL_CLOSE, &lv_font_montserrat_14, COL_TEXT);
    lv_obj_align(s_care_label, LV_ALIGN_BOTTOM_RIGHT, -6, -6);
    ui_hud_set_chrome(false);
    s_hint = make_label(parent, "Tap to place; X cancels",
                         &lv_font_montserrat_12, 0xf5c86b);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -HUD_BOTTOM_H - 18);
    lv_obj_set_hidden(s_hint, true);
    s_bubble = lv_obj_create(parent);
    lv_obj_remove_style_all(s_bubble);
    lv_obj_set_size(s_bubble, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(s_bubble, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_bg_opa(s_bubble, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_bubble, 6, 0);
    ui_style_card(s_bubble, 0xb99b67, true);
    lv_obj_set_clickable(s_bubble, false);
    lv_obj_set_scrollable(s_bubble, false);
    s_bubble_label = make_label(s_bubble, "", &lv_font_montserrat_14, COL_TEXT);
    lv_obj_set_hidden(s_bubble, true);
    ui_hud_tick(lv_tick_get());
}

void ui_hud_bubble(const char *text, uint32_t color, uint32_t ms)
{
    lv_label_set_text(s_bubble_label, text);
    lv_obj_set_style_text_color(s_bubble_label, lv_color_hex(color), 0);
    lv_obj_set_hidden(s_bubble, false);
    s_bubble_until = lv_tick_get() + ms;
}

void ui_hud_set_gift_mode(bool on)
{
    lv_obj_set_hidden(s_hint, !on);
    lv_obj_set_hidden(s_care_label, !on);
}

void ui_hud_set_top(bool on)
{
    s_top_visible = on;
    for (int i = 0; i < NUM_STATS; i++) lv_obj_set_hidden(s_stats[i], !on);
    lv_obj_set_hidden(s_identity, !on);
    lv_obj_set_hidden(s_level, !on);
    lv_obj_set_hidden(s_progress, !on);
    lv_obj_set_hidden(s_resources, !on);
    lv_obj_set_hidden(s_battery, !on);
}

void ui_hud_set_bot(bool on)
{
    s_bot_visible = on;
    lv_obj_t *buttons[] = {ui.btn_feed, ui.btn_clean, ui.btn_play, ui.btn_sleep,
                           ui.btn_decor, ui.btn_theme};
    for (int i = 0; i < 6; i++) lv_obj_set_hidden(buttons[i], !on);
}

void ui_hud_set_chrome(bool on)
{
    if (on) {
        behaviour_aim_cancel();
        spider_render(lv_tick_get());
        lv_obj_move_foreground(s_layer);
    }
    ui_hud_set_top(on);
    ui_hud_set_bot(on);
    if (s_care_label) {
        lv_obj_set_hidden(s_care_label, !g.gift_mode);
    }
}
void ui_hud_reveal_top(void) { ui_hud_set_top(true); }
void ui_hud_reveal_bot(void) { ui_hud_set_bot(true); }
bool ui_hud_chrome_visible(void) { return s_top_visible || s_bot_visible; }
void ui_hud_dismiss_if_visible(void) { ui_hud_set_chrome(false); }

bool ui_hud_test(void)
{
    lv_obj_t *buttons[] = {ui.btn_feed, ui.btn_clean, ui.btn_play, ui.btn_sleep,
                           ui.btn_decor, ui.btn_theme};
    bool ok = true;
    ui_hud_set_chrome(false);
    ok = !lv_obj_is_hidden(s_care_button) && lv_obj_is_hidden(ui.btn_decor) &&
        lv_obj_is_hidden(s_care_label) && lv_obj_is_hidden(s_battery) &&
        lv_obj_get_style_bg_opa(s_care_button, 0) == LV_OPA_TRANSP &&
        lv_obj_get_child_count(s_care_button) == 1 &&
        lv_obj_get_x(s_care_button) <= 0 && lv_obj_get_y(s_care_button) <= 0;
    lv_obj_send_event(s_care_button, LV_EVENT_CLICKED, NULL);
    ok = ok && ui_hud_chrome_visible();
    ok = ok && ui.creature_layer && lv_obj_get_parent(ui.creature_layer) == lv_obj_get_parent(ui.tap_layer) &&
        lv_obj_get_child(lv_obj_get_parent(ui.tap_layer), -1) == s_layer;
    lv_obj_update_layout(ui.screen);
    for (int i = 0; i < 6; i++)
        ok = ok && buttons[i] && !lv_obj_is_hidden(buttons[i]) &&
             lv_obj_get_height(buttons[i]) >= 64;
    ok = ok && lv_obj_get_width(s_care_button) == 44 &&
        !strcmp(lv_label_get_text(s_care_label), LV_SYMBOL_CLOSE);
    int row_left = lv_obj_get_x(s_stats[0]);
    int row_right = lv_obj_get_x(s_stats[NUM_STATS - 1]) +
        lv_obj_get_width(s_stats[NUM_STATS - 1]);
    ok = ok && abs(row_left - (ui.W - row_right)) <= 1 &&
        row_left >= lv_obj_get_x(s_care_button) + lv_obj_get_width(s_care_button) + 6 &&
        lv_obj_get_style_bg_opa(s_care_button, 0) == LV_OPA_TRANSP &&
        lv_obj_is_hidden(s_care_label);
    ok = ok && lv_obj_get_x(s_battery) >= 0 &&
        lv_obj_get_x(s_battery) + lv_obj_get_width(s_battery) <= ui.W - 12 &&
        lv_obj_get_y(s_battery) == 8 && !lv_obj_is_hidden(s_battery) &&
        lv_obj_get_style_bg_opa(s_battery, 0) == LV_OPA_TRANSP &&
        lv_obj_get_style_border_width(s_battery, 0) == 0 &&
        lv_obj_get_style_bg_opa(s_identity, 0) == LV_OPA_70;
    for (int i = 0; i < NUM_STATS; i++)
        ok = ok && !lv_obj_is_hidden(s_stats[i]) &&
            lv_obj_get_y(s_stats[i]) == 8 &&
            lv_obj_get_height(s_stats[i]) == lv_obj_get_height(s_care_button) &&
            lv_obj_get_width(lv_obj_get_child(s_stats[i], 0)) + 8 <= lv_obj_get_width(s_stats[i]) &&
            lv_obj_get_width(s_values[i]) + 8 <= lv_obj_get_width(s_stats[i]);
    lv_obj_send_event(s_battery, LV_EVENT_CLICKED, NULL);
    ok = ok && ui_menu_visible();
    game_open_menu("close");
    uint32_t taps = g.taps;
    uint8_t mode = g.gift_mode;
    uint8_t selected = g.gift_selected;
    g.gift_mode = 0;
    lv_obj_send_event(ui.btn_decor, LV_EVENT_CLICKED, NULL);
    ok = ok && ui_hud_gift_menu_visible();
    ui_hud_gift_menu_hide();
    ui_hud_tick(lv_tick_get() + 10000);
    const uint8_t values[] = {g.hunger, g.mood, g.energy, g.hygiene};
    for (int i = 0; i < NUM_STATS; i++) ok = ok && lv_bar_get_value(s_bars[i]) == values[i];
    ok = ok && !lv_obj_is_hidden(ui.btn_decor) && !lv_obj_is_hidden(s_stats[0]);
    lv_obj_send_event(s_care_button, LV_EVENT_CLICKED, NULL);
    ok = ok && !ui_hud_chrome_visible() && !lv_obj_is_hidden(s_care_button);
    game_gift_chosen(0);
    ok = ok && g.gift_mode && !ui_hud_chrome_visible() && !lv_obj_is_hidden(s_hint) &&
        !lv_obj_is_hidden(s_care_label);
    lv_obj_send_event(s_care_button, LV_EVENT_CLICKED, NULL);
    ok = ok && !g.gift_mode && lv_obj_is_hidden(s_hint);
    g.taps = taps;
    g.gift_mode = mode;
    g.gift_selected = selected;
    ui_hud_set_gift_mode(mode != 0);
    return ok;
}

void ui_hud_tick(uint32_t now_ms)
{
    /* Labels only need a slow refresh; rewriting them at 30 Hz invalidates the HUD. */
    if (s_last_refresh == 0 || now_ms - s_last_refresh >= 250) {
        s_last_refresh = now_ms;
        const uint8_t values[] = {g.hunger, g.mood, g.energy, g.hygiene};
        for (int i = 0; i < NUM_STATS; i++) {
            char text[8];
            snprintf(text, sizeof text, "%u", values[i]);
            if (strcmp(lv_label_get_text(s_values[i]), text)) lv_label_set_text(s_values[i], text);
            if (lv_bar_get_value(s_bars[i]) != values[i])
                lv_bar_set_value(s_bars[i], values[i], LV_ANIM_OFF);
        }
        char text[96];
        snprintf(text, sizeof text, "%s / Lv %lu / %lu/%u XP", g.name[0] ? g.name : "Spider",
                 (unsigned long)g.level, (unsigned long)(g.xp % XP_PER_LEVEL), XP_PER_LEVEL);
        if (strcmp(lv_label_get_text(s_level), text)) lv_label_set_text(s_level, text);
        const char *next_name = NULL;
        uint32_t next_level = UINT32_MAX;
        for (int i = 0; i < GIFT_KINDS; i++) {
            uint32_t level = gift_unlock_level(i);
            if (level > g.level && level < next_level) {
                next_level = level;
                next_name = gift_kind_name(i);
            }
        }
        int next_species = species_next_locked();
        if (next_species >= 0 && k_species[next_species].unlock_level < next_level) {
            next_level = k_species[next_species].unlock_level;
            next_name = k_species[next_species].name;
        }
        if (next_name) {
            char name[40];
            ui_menu_format_name(name, sizeof name, next_name);
            snprintf(text, sizeof text, "Next: %s at Level %lu", name, (unsigned long)next_level);
        } else {
            snprintf(text, sizeof text, "All Gifts and Prey Unlocked");
        }
        if (strcmp(lv_label_get_text(s_progress), text)) lv_label_set_text(s_progress, text);
        snprintf(text, sizeof text, "%lu Silk", (unsigned long)g.silk);
        battery_refresh();
        bool full = prey_count() >= MAX_PREY;
        if (full != lv_obj_has_state(ui.btn_feed, LV_STATE_DISABLED)) {
            if (full) lv_obj_add_state(ui.btn_feed, LV_STATE_DISABLED);
            else lv_obj_remove_state(ui.btn_feed, LV_STATE_DISABLED);
            lv_obj_set_style_text_color(lv_obj_get_child(ui.btn_feed, -1),
                lv_color_hex(full ? 0x64717b : COL_TEXT), 0);
        }
        if (strcmp(lv_label_get_text(s_resources), text)) lv_label_set_text(s_resources, text);
    }
    cspider_emote_t emote = CSP_EMOTE_NONE;
    if (behaviour_gift_visiting()) emote = CSP_EMOTE_HAPPY;
    else if (g.state == SPIDER_SLEEP) emote = CSP_EMOTE_SLEEPY;
    else if (g.state == SPIDER_SICK || g.mood < MOOD_REFUSE_HUNT) emote = CSP_EMOTE_SAD;
    else if (g.state == SPIDER_AIM || g.state == SPIDER_STALK) emote = CSP_EMOTE_ALERT;
    else if (g.state == SPIDER_EAT || g.state == SPIDER_PLAY || (g.mood > 70 && g.bond > 40))
        emote = CSP_EMOTE_HAPPY;
    cspider_set_emote(emote);
    if (!lv_obj_is_hidden(s_bubble)) {
        if ((int32_t)(now_ms - s_bubble_until) >= 0) lv_obj_set_hidden(s_bubble, true);
        else lv_obj_set_pos(s_bubble, g.spider_x - 24, g.spider_y - 56);
    }
}
