#include "ui_setup.h"
#include "ui_style.h"

#include "game.h"
#include "game_config.h"
#include "spider.h"
#include "ui_menu.h"

#include <stdio.h>
#include <string.h>

#define COL_PANEL 0x16283c
#define COL_BORDER 0x355167
#define COL_TEXT 0xe8efda
#define COL_ACCENT 0x76d6b1
#define SETUP_ROWS 4
#define SETUP_SWATCH_MAX 9

static lv_obj_t *s_screen, *s_name_label;
static lv_obj_t *s_swatch[SETUP_ROWS][SETUP_SWATCH_MAX];
static uint8_t s_picked[SETUP_ROWS];
static char s_name[sizeof g.name];

static uint8_t *row_field(int row)
{
    switch (row) {
    case 0: return &g.colour;
    case 1: return &g.pupil_colour;
    case 2: return &g.leg_colour;
    default: return &g.fang_colour;
    }
}

static int row_count(int row)
{
    return row == 0 ? (int)spider_colour_count() : (int)spider_accent_count();
}

static uint32_t row_colour(int row, int i)
{
    return row == 0 ? spider_colour_swatch(i) : spider_accent_colour(i);
}

static bool on_name_submit(const char *text)
{
    snprintf(s_name, sizeof s_name, "%s", text);
    lv_label_set_text(s_name_label, s_name[0] ? s_name : "Tap to name your spider");
    ui_menu_close();
    return true;
}

static void on_name(lv_event_t *e)
{
    (void)e;
    ui_menu_open_input("Spider Name", s_name, false, sizeof s_name - 1, on_name_submit);
}

static void on_swatch(lv_event_t *e)
{
    uintptr_t value = (uintptr_t)lv_event_get_user_data(e);
    int row = value >> 8, index = value & 0xff;
    s_picked[row] = index;
    for (int i = 0; i < row_count(row) && i < SETUP_SWATCH_MAX; i++) {
        lv_obj_set_style_border_width(s_swatch[row][i], i == index ? 3 : 1, 0);
        lv_obj_set_style_border_color(s_swatch[row][i],
            lv_color_hex(i == index ? COL_ACCENT : COL_BORDER), 0);
    }
    *row_field(row) = index;
    spider_refresh_colours();
    spider_preview_refresh();
}

static void on_start(lv_event_t *e)
{
    (void)e;
    snprintf(g.name, sizeof g.name, "%s", s_name[0] ? s_name : "Spider");
    for (int r = 0; r < SETUP_ROWS; r++) *row_field(r) = s_picked[r];
    spider_refresh_colours();
    game_save_now();
    lv_screen_load(ui.screen);
}

bool ui_setup_needed(void) { return !g.name[0]; }

void ui_setup_init(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, ui.W, ui.H);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x080e18), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(s_screen, false);
    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, "A New Spider");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COL_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_t *name = lv_button_create(s_screen);
    lv_obj_set_pos(name, UI_CORNER, 102);
    lv_obj_set_size(name, 264, 56);
    lv_obj_set_style_bg_color(name, lv_color_hex(COL_PANEL), 0);
    ui_style_card(name, COL_ACCENT, false);
    lv_obj_add_event_cb(name, on_name, LV_EVENT_CLICKED, NULL);
    s_name_label = lv_label_create(name);
    lv_obj_set_style_text_font(s_name_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_name_label, lv_color_hex(COL_TEXT), 0);
    lv_obj_center(s_name_label);

    lv_obj_t *preview = lv_obj_create(s_screen);
    lv_obj_remove_style_all(preview);
    lv_obj_set_pos(preview, ui.W - UI_CORNER - 104, 80);
    lv_obj_set_size(preview, 104, 104);
    lv_obj_set_style_bg_color(preview, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_bg_opa(preview, LV_OPA_COVER, 0);
    lv_obj_set_clickable(preview, false);
    lv_obj_set_scrollable(preview, false);
    spider_preview_init(preview);

    static const char *const names[] = {"Body", "Eyes", "Legs", "Fangs"};
    for (int r = 0; r < SETUP_ROWS; r++) {
        lv_obj_t *label = lv_label_create(s_screen);
        lv_label_set_text(label, names[r]);
        lv_obj_set_pos(label, UI_CORNER, 205 + r * 42);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(COL_TEXT), 0);
        for (int i = 0; i < row_count(r) && i < SETUP_SWATCH_MAX; i++) {
            lv_obj_t *swatch = lv_obj_create(s_screen);
            lv_obj_remove_style_all(swatch);
            lv_obj_set_size(swatch, 32, 32);
            lv_obj_set_pos(swatch, 100 + i * 38, 196 + r * 42);
            lv_obj_set_style_bg_color(swatch, lv_color_hex(row_colour(r, i)), 0);
            lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(swatch, 1, 0);
            lv_obj_set_scrollable(swatch, false);
            lv_obj_set_clickable(swatch, true);
            lv_obj_add_event_cb(swatch, on_swatch, LV_EVENT_CLICKED,
                                (void *)(uintptr_t)((r << 8) | i));
            s_swatch[r][i] = swatch;
        }
    }
    lv_obj_t *start = lv_button_create(s_screen);
    lv_obj_set_size(start, ui.W - 2 * UI_CORNER, 56);
    lv_obj_align(start, LV_ALIGN_BOTTOM_MID, 0, -UI_CORNER - 10);
    lv_obj_set_style_bg_color(start, lv_color_hex(COL_ACCENT), 0);
    ui_style_card(start, 0xb99b67, false);
    lv_obj_add_event_cb(start, on_start, LV_EVENT_CLICKED, NULL);
    lv_obj_t *text = lv_label_create(start);
    lv_label_set_text(text, "Start");
    lv_obj_set_style_text_font(text, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(text, lv_color_hex(COL_PANEL), 0);
    lv_obj_center(text);
}

void ui_setup_show(void)
{
    ui_menu_close();
    snprintf(s_name, sizeof s_name, "%s", g.name);
    lv_label_set_text(s_name_label, s_name[0] ? s_name : "Tap to name your spider");
    for (int r = 0; r < SETUP_ROWS; r++) {
        s_picked[r] = *row_field(r);
        for (int i = 0; i < row_count(r) && i < SETUP_SWATCH_MAX; i++) {
            bool selected = s_picked[r] == i;
            lv_obj_set_style_border_width(s_swatch[r][i], selected ? 3 : 1, 0);
            lv_obj_set_style_border_color(s_swatch[r][i],
                lv_color_hex(selected ? COL_ACCENT : COL_BORDER), 0);
        }
    }
    spider_preview_refresh();
    lv_screen_load(s_screen);
}
