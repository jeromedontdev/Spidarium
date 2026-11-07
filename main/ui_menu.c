/*
 * Spidarium — generic list menu, implementation.
 */
#include "ui_menu.h"

#include "game.h"
#include "game_config.h"

#include <stdio.h>

#define COL_PANEL   0x15181f
#define COL_BORDER  0x2a2f3a
#define COL_TEXT    0xdfe7ff
#define COL_DIM     0x8b93a7
#define COL_ACCENT  0xf5b342

#define MENU_ROW_H  52
#define MENU_GAP    8

static lv_obj_t        *s_menu;
static lv_obj_t        *s_backdrop;
static lv_obj_t        *s_list;      /* scrollable row container */
static ui_menu_pick_cb  s_cb;
static ui_menu_back_cb  s_back;
static const ui_menu_item_t *s_items;
static int              s_count;

/* The panel never grows past this. The achievements list is 10 rows, which at
 * 52+8 px per row is 650 px -- taller than the 480 px screen, so it used to
 * overflow off both edges with no way to reach the bottom rows. */
#define MENU_MAX_H   (ui.H - 2 * UI_CORNER)

void ui_menu_set_back(ui_menu_back_cb cb)
{
    s_back = cb;
}

static void on_back(lv_event_t *e)
{
    (void)e;
    ui_menu_back_cb cb = s_back;
    ui_menu_close();
    if (cb) cb();
}

/* Tapping anywhere outside the panel dismisses the menu. The backdrop is a
 * full-screen transparent object created BEFORE the panel, so the panel sits on
 * top of it and keeps receiving its own taps. */
static void on_backdrop(lv_event_t *e)
{
    (void)e;
    on_back(NULL);
}

static void on_row(lv_event_t *e)
{
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    uint8_t id = (uint8_t)(uintptr_t)lv_obj_get_user_data(btn);

    /* Capture the callback BEFORE closing. ui_menu_close() clears s_cb, so the
     * old order (close, then call s_cb) called NULL and every menu row did
     * nothing -- which is why picking a theme appeared to be ignored. */
    ui_menu_pick_cb cb = s_cb;

    /* Close first, then notify: the callback may open another menu. */
    ui_menu_close();
    if (cb) cb(id);
}

void ui_menu_close(void)
{
    if (s_menu) {
        lv_obj_del(s_menu);
        s_menu = NULL;
    }
    if (s_backdrop) {
        /* Deferred: the backdrop's own CLICKED handler is what usually gets
         * here, and deleting the object whose event is being dispatched is the
         * one delete LVGL does not tolerate. */
        lv_obj_del_async(s_backdrop);
        s_backdrop = NULL;
    }
    s_list = NULL;   /* owned by s_menu, freed with it */
    s_cb = NULL;
    s_back = NULL;
    s_items = NULL;
    s_count = 0;
}

bool ui_menu_visible(void)
{
    return s_menu != NULL;
}

void ui_menu_open(const char *title, const ui_menu_item_t *items, int count,
                  ui_menu_pick_cb cb)
{
    if (!items || count <= 0) return;

    /* Replace any menu already open. */
    ui_menu_close();

    s_items = items;
    s_count = count;
    s_cb    = cb;

    const int32_t w = ui.W - 2 * UI_CORNER;
    int32_t h = 26 + count * (MENU_ROW_H + MENU_GAP) + 24;
    if (h > MENU_MAX_H) h = MENU_MAX_H;

    /* Full-screen backdrop, created first so the panel stacks above it. It is
     * clickable and swallows taps, which both dims the tank and gives the player
     * an obvious way out: tap anywhere outside the panel.
     *
     * The backdrop is OPAQUE, not 50% black. A translucent full-screen layer
     * forces LVGL to blend the entire 480x480 framebuffer every frame, and the
     * tank keeps animating behind it -- which is why opening a menu made the
     * whole UI crawl. An opaque backdrop lets LVGL skip the tank entirely. */
    s_backdrop = lv_obj_create(ui.screen);
    lv_obj_remove_style_all(s_backdrop);
    lv_obj_set_size(s_backdrop, ui.W, ui.H);
    lv_obj_set_pos(s_backdrop, 0, 0);
    lv_obj_set_style_bg_color(s_backdrop, lv_color_hex(0x04050a), 0);
    lv_obj_set_style_bg_opa(s_backdrop, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(s_backdrop, false);
    lv_obj_set_clickable(s_backdrop, true);
    lv_obj_add_event_cb(s_backdrop, on_backdrop, LV_EVENT_CLICKED, NULL);

    s_menu = lv_obj_create(ui.screen);
    lv_obj_remove_style_all(s_menu);
    lv_obj_set_size(s_menu, w, h);
    lv_obj_center(s_menu);
    lv_obj_set_style_bg_color(s_menu, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_bg_opa(s_menu, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_menu, 14, 0);
    lv_obj_set_style_border_width(s_menu, 1, 0);
    lv_obj_set_style_border_color(s_menu, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_pad_all(s_menu, 12, 0);
    lv_obj_set_scrollable(s_menu, false);
    /* Swallow taps so they do not fall through to the tank behind. */
    lv_obj_set_clickable(s_menu, true);

    lv_obj_t *t = lv_label_create(s_menu);
    lv_label_set_text(t, title ? title : "");
    lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_pos(t, 4, 0);

    /* BACK button, top-right. Without this there was no way out of the menu at
     * all -- it covered the tank and swallowed every tap. */
    lv_obj_t *back = lv_button_create(s_menu);
    lv_obj_set_size(back, 64, 30);
    lv_obj_align(back, LV_ALIGN_TOP_RIGHT, 0, -4);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x1e232b), 0);
    lv_obj_set_style_border_width(back, 1, 0);
    lv_obj_set_style_border_color(back, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_radius(back, 8, 0);
    lv_obj_add_event_cb(back, on_back, LV_EVENT_CLICKED, NULL);

    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, "back");
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(bl, lv_color_hex(COL_TEXT), 0);
    lv_obj_center(bl);

    /* Scrollable row container.
     *
     * The rows used to be children of the panel itself, which was sized to fit
     * them. A list taller than the screen therefore overflowed off both edges
     * with no way to reach the bottom rows -- the achievements list (10 rows,
     * 650 px) was completely unreachable below row 7.
     *
     * The panel is now capped at MENU_MAX_H and the rows live in a container
     * that fills the space below the title and scrolls. */
    const int32_t list_y = 30;
    const int32_t list_h = h - list_y - 12;

    s_list = lv_obj_create(s_menu);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_size(s_list, w - 24, list_h);
    lv_obj_set_pos(s_list, 0, list_y);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_set_style_pad_row(s_list, MENU_GAP, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_clickable(s_list, true);

    for (int i = 0; i < count; i++) {
        const ui_menu_item_t *it = &items[i];

        lv_obj_t *b = lv_button_create(s_list);
        lv_obj_set_size(b, w - 24, MENU_ROW_H);
        /* Flex handles the vertical position; no explicit y needed. */
        lv_obj_set_style_bg_color(b, lv_color_hex(0x1e232b), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(b, it->selected ? 2 : 1, 0);
        lv_obj_set_style_border_color(b,
            lv_color_hex(it->selected ? COL_ACCENT : COL_BORDER), 0);
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x2a2f3a), LV_STATE_PRESSED);
        lv_obj_set_scrollable(b, false);

        if (it->enabled) {
            lv_obj_set_user_data(b, (void *)(uintptr_t)it->id);
            lv_obj_add_event_cb(b, on_row, LV_EVENT_CLICKED, NULL);
        } else {
            /* Disabled rows are visible but inert, so the player can see what
             * is coming without being able to pick it. */
            lv_obj_set_clickable(b, false);
            lv_obj_set_style_bg_opa(b, LV_OPA_50, 0);
        }

        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, it->label ? it->label : "");
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(l,
            lv_color_hex(it->enabled ? COL_TEXT : COL_DIM), 0);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 14, it->detail ? -8 : 0);

        if (it->detail) {
            lv_obj_t *d = lv_label_create(b);
            lv_label_set_text(d, it->detail);
            lv_obj_set_style_text_font(d, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(d, lv_color_hex(COL_DIM), 0);
            lv_obj_align(d, LV_ALIGN_LEFT_MID, 14, 10);
        }

        /* Chevron marks a row that opens another menu. */
        if (it->is_submenu) {
            lv_obj_t *c = lv_label_create(b);
            lv_label_set_text(c, ">");
            lv_obj_set_style_text_font(c, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(c, lv_color_hex(COL_DIM), 0);
            lv_obj_align(c, LV_ALIGN_RIGHT_MID, -14, 0);
        }
    }

    lv_obj_move_foreground(s_menu);
}
