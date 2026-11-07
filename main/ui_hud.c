/*
 * Spidarium — terrarium HUD implementation.
 */
#include "ui_hud.h"

#include "assets.h"
#include "audio.h"
#include "behaviour.h"
#include "cspider.h"
#include "game_config.h"
#include "gift.h"
#include "mind.h"
#include "needs.h"
#include "spider.h"
#include "sprites.h"
#include "theme.h"

#include <stdio.h>

#define COL_PANEL     0x15181f
#define COL_BORDER    0x2a2f3a
#define COL_TEXT      0xdfe7ff
#define COL_DIM       0x8b93a7
#define COL_HUNGER    0xf5b342
#define COL_MOOD      0x58d68d
#define COL_ENERGY    0x7c9cff
#define COL_HYGIENE   0x4fd1c5

static lv_obj_t *s_ring[NUM_STATS];
static lv_obj_t *s_ring_label[NUM_STATS];
static lv_obj_t *s_bubble;
static lv_obj_t *s_bubble_label;
static uint32_t  s_bubble_until;
static lv_obj_t *s_gift_hint;
static lv_obj_t *s_level_label;

/* Auto-hiding chrome, in TWO independent groups.
 *
 * The top band reveals the STATS (rings, level, stage, age); the bottom band
 * reveals the ACTIONS (the button tray). They are separate so reaching for one
 * never pops the other -- the old single group showed everything at once, which
 * covered most of the tank for a gesture that only meant "show me the buttons".
 */
#define CHROME_MAX 24
static lv_obj_t *s_chrome_top[CHROME_MAX];
static int       s_chrome_top_n;
static lv_obj_t *s_chrome_bot[CHROME_MAX];
static int       s_chrome_bot_n;

static bool      s_top_visible;
static bool      s_bot_visible;
static uint32_t  s_top_until;
static uint32_t  s_bot_until;

/* The always-visible menu button. Not part of either chrome group, so it stays
 * on screen when they auto-hide. */
static lv_obj_t *s_hamburger;

/* Tapping it toggles the chrome. A toggle rather than a reveal, because with no
 * edge bands there is otherwise no way to dismiss the tray without waiting out
 * the hold timer. */
static void on_hamburger(lv_event_t *e)
{
    (void)e;
    bool show = !ui_hud_chrome_visible();
    ui_hud_set_top(show);
    ui_hud_set_bot(show);
}

/* Tapping anywhere that is NOT the chrome dismisses it.
 *
 * Called from the terrarium tap handler. The chrome is a floating overlay, so
 * "anywhere else" is simply "the tank was tapped while the chrome was up".
 * Without this the tray could only be dismissed by waiting out the 5s hold or
 * by finding the hamburger again, which is a lot of ceremony for a menu you
 * opened by accident. */
void ui_hud_dismiss_if_visible(void)
{
    if (!ui_hud_chrome_visible()) return;
    ui_hud_set_top(false);
    ui_hud_set_bot(false);
}

static void chrome_add_top(lv_obj_t *o)
{
    if (o && s_chrome_top_n < CHROME_MAX) s_chrome_top[s_chrome_top_n++] = o;
}

static void chrome_add_bot(lv_obj_t *o)
{
    if (o && s_chrome_bot_n < CHROME_MAX) s_chrome_bot[s_chrome_bot_n++] = o;
}

/* Gift picker. Built once at init; shown/hidden on demand. */
static lv_obj_t *s_gift_menu;
static lv_obj_t *s_gift_menu_btn[GIFT_KINDS];
static lv_obj_t *s_gift_menu_lbl[GIFT_KINDS];

static const uint32_t k_stat_colors[NUM_STATS] = {
    COL_HUNGER, COL_MOOD, COL_ENERGY, COL_HYGIENE,
};

/* ------------------------------------------------------------------ */
/* Widget helpers                                                      */
/* ------------------------------------------------------------------ */
static lv_obj_t *make_icon_button(lv_obj_t *parent, int32_t x, int32_t y,
                                  int32_t w, const char *icon,
                                  const char *fallback, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, w, BTN_H);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_80, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x2a2f3a), LV_STATE_PRESSED);
    lv_obj_set_scrollable(b, false);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);

    int idx = sprite_icon_index(icon);
    const lv_image_dsc_t *dsc = (idx >= 0) ? assets_image(idx) : NULL;

    if (dsc) {
        lv_obj_t *img = lv_image_create(b);
        lv_image_set_src(img, dsc);
        lv_image_set_scale(img, 128);   /* 48px icon -> 24px */
        lv_obj_center(img);
    } else {
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, fallback);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(COL_TEXT), 0);
        lv_obj_center(l);
    }
    return b;
}

/* A stat ring: a circular arc plus a numeric label. */
static lv_obj_t *make_ring(lv_obj_t *parent, int32_t x, int32_t y,
                           int32_t size, uint32_t color)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, size, size);
    lv_obj_set_pos(arc, x, y);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arc, 0, 0);
    lv_obj_set_style_arc_width(arc, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(COL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    lv_obj_set_clickable(arc, false);
    lv_obj_set_scrollable(arc, false);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_value(arc, 60);
    return arc;
}

/* ------------------------------------------------------------------ */
/* Gift picker                                                         */
/* ------------------------------------------------------------------ */

/* One handler for every kind: the kind travels in the button's user data, so
 * there is no need for five near-identical callbacks. */
static void on_gift_pick(lv_event_t *e)
{
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    uint8_t kind = (uint8_t)(uintptr_t)lv_obj_get_user_data(btn);

    game_gift_chosen(kind);
}

static void build_gift_menu(lv_obj_t *parent)
{
    /* A centred panel. Sized to the safe area: the panel is a squircle, so
     * nothing may sit inside UI_CORNER of a corner. */
    const int32_t w = ui.W - 2 * UI_CORNER;
    const int32_t row_h = 52;
    const int32_t gap = 8;
    /* Title band + one row per kind + the panel's own padding. The old 40 px
     * title band left the last row 2 px past the content area. */
    const int32_t h = 26 + GIFT_KINDS * (row_h + gap) + 24;

    s_gift_menu = lv_obj_create(parent);
    lv_obj_remove_style_all(s_gift_menu);
    lv_obj_set_size(s_gift_menu, w, h);
    lv_obj_center(s_gift_menu);
    lv_obj_set_style_bg_color(s_gift_menu, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_bg_opa(s_gift_menu, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_gift_menu, 14, 0);
    lv_obj_set_style_border_width(s_gift_menu, 1, 0);
    lv_obj_set_style_border_color(s_gift_menu, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_pad_all(s_gift_menu, 12, 0);
    lv_obj_set_scrollable(s_gift_menu, false);
    /* Swallow taps so they do not fall through to the tank behind. */
    lv_obj_set_clickable(s_gift_menu, true);

    lv_obj_t *title = lv_label_create(s_gift_menu);
    lv_label_set_text(title, "give a gift");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_pos(title, 4, 0);

    for (int i = 0; i < GIFT_KINDS; i++) {
        lv_obj_t *b = lv_button_create(s_gift_menu);
        lv_obj_set_size(b, w - 24, row_h);
        lv_obj_set_pos(b, 0, 26 + i * (row_h + gap));
        lv_obj_set_style_bg_color(b, lv_color_hex(0x1e232b), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(COL_BORDER), 0);
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x2a2f3a), LV_STATE_PRESSED);
        lv_obj_set_scrollable(b, false);
        lv_obj_set_user_data(b, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(b, on_gift_pick, LV_EVENT_CLICKED, NULL);

        s_gift_menu_lbl[i] = lv_label_create(b);
        lv_label_set_text(s_gift_menu_lbl[i], gift_kind_name((uint8_t)i));
        lv_obj_set_style_text_font(s_gift_menu_lbl[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_gift_menu_lbl[i],
                                    lv_color_hex(COL_TEXT), 0);
        lv_obj_align(s_gift_menu_lbl[i], LV_ALIGN_LEFT_MID, 14, 0);

        s_gift_menu_btn[i] = b;
    }

    lv_obj_set_hidden(s_gift_menu, true);
}

void ui_hud_gift_menu_show(void)
{
    if (!s_gift_menu) return;

    /* Refresh lock state and the current selection every time it opens, so the
     * menu never shows stale information. */
    for (int i = 0; i < GIFT_KINDS; i++) {
        bool unlocked = gift_unlocked((uint8_t)i);
        bool current  = gift_present() && gift_kind() == (uint8_t)i;

        char buf[48];
        if (!unlocked) {
            snprintf(buf, sizeof buf, "%s  (lv %u)",
                     gift_kind_name((uint8_t)i),
                     (unsigned)gift_unlock_level((uint8_t)i));
        } else if (current) {
            snprintf(buf, sizeof buf, "%s  (in tank)", gift_kind_name((uint8_t)i));
        } else {
            snprintf(buf, sizeof buf, "%s", gift_kind_name((uint8_t)i));
        }
        lv_label_set_text(s_gift_menu_lbl[i], buf);

        lv_obj_set_style_text_color(s_gift_menu_lbl[i],
                                    lv_color_hex(unlocked ? COL_TEXT : COL_DIM), 0);
        lv_obj_set_style_border_color(s_gift_menu_btn[i],
                                      lv_color_hex(current ? COL_HUNGER : COL_BORDER), 0);
        lv_obj_set_style_border_width(s_gift_menu_btn[i], current ? 2 : 1, 0);
    }

    lv_obj_set_hidden(s_gift_menu, false);
    lv_obj_move_foreground(s_gift_menu);
}

void ui_hud_gift_menu_hide(void)
{
    if (s_gift_menu) lv_obj_set_hidden(s_gift_menu, true);
}

bool ui_hud_gift_menu_visible(void)
{
    return s_gift_menu && !lv_obj_is_hidden(s_gift_menu);
}

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */
void ui_hud_init(lv_obj_t *parent)
{
    /* --- stat rings, top-left ---------------------------------------- */
    const int32_t ring_size = RING_SIZE;
    const int32_t ring_gap  = 6;
    /* Start the rings to the RIGHT of the hamburger's touch area, which
     * occupies HAMBURGER_INSET..HAMBURGER_INSET+HAMBURGER_TOUCH. Without this
     * the first ring sat underneath the button. */
    int32_t rx = HAMBURGER_INSET + HAMBURGER_TOUCH + 8;
    int32_t ry = 8;

    for (int i = 0; i < NUM_STATS; i++) {
        s_ring[i] = make_ring(parent, rx + i * (ring_size + ring_gap), ry,
                              ring_size, k_stat_colors[i]);

        s_ring_label[i] = lv_label_create(parent);
        lv_label_set_text(s_ring_label[i], "0");
        lv_obj_set_style_text_font(s_ring_label[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(s_ring_label[i],
                                    lv_color_hex(k_stat_colors[i]), 0);
        /* Centre the number in the ring. The old +11/+10 offsets were tuned
         * for a 34 px ring; at 40 px the label sat visibly high and left. */
        lv_obj_set_pos(s_ring_label[i],
                       rx + i * (ring_size + ring_gap) + (ring_size / 2) - 7,
                       ry + (ring_size / 2) - 7);
        lv_obj_set_clickable(s_ring_label[i], false);
    }

    /* --- level / stage, top-right ------------------------------------ */
    s_level_label = lv_label_create(parent);
    lv_label_set_text(s_level_label, "Lv 1");
    lv_obj_set_style_text_font(s_level_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_level_label, lv_color_hex(COL_TEXT), 0);
    lv_obj_align(s_level_label, LV_ALIGN_TOP_RIGHT, -UI_CORNER, 10);

    ui.stage_label = lv_label_create(parent);
    lv_label_set_text(ui.stage_label, "spiderling");
    lv_obj_set_style_text_font(ui.stage_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ui.stage_label, lv_color_hex(COL_DIM), 0);
    lv_obj_align(ui.stage_label, LV_ALIGN_TOP_RIGHT, -UI_CORNER, 30);

    ui.age_label = lv_label_create(parent);
    lv_label_set_text(ui.age_label, "day 0");
    lv_obj_set_style_text_font(ui.age_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ui.age_label, lv_color_hex(COL_DIM), 0);
    lv_obj_align(ui.age_label, LV_ALIGN_TOP_RIGHT, -UI_CORNER, 48);

    /* --- action tray, bottom -----------------------------------------
     * TWO rows of THREE. Six actions, evenly split, so every button is the
     * same size and the grid is symmetric.
     *
     * It was 4+2, which left two empty slots in the bottom row and made the
     * buttons narrower than they needed to be. Three across gives ~124 px per
     * button instead of ~94 px -- noticeably easier to hit on a 2.16" panel. */
    const int32_t n_col = 3;
    const int32_t gap   = 8;
    int32_t bw = (ui.W - 2 * UI_CORNER - (n_col - 1) * gap) / n_col;

    /* Bottom row sits on the corner-safe line; the top row stacks above it. */
    int32_t by_bot = ui.H - UI_CORNER - BTN_H;
    int32_t by_top = by_bot - BTN_H - gap;

    ui.btn_feed   = make_icon_button(parent, UI_CORNER + 0 * (bw + gap), by_top,
                                     bw, "feed",   "F", game_on_feed);
    ui.btn_clean  = make_icon_button(parent, UI_CORNER + 1 * (bw + gap), by_top,
                                     bw, "clean",  "C", game_on_clean);
    ui.btn_play   = make_icon_button(parent, UI_CORNER + 2 * (bw + gap), by_top,
                                     bw, "play",   "P", game_on_play);
    ui.btn_sleep  = make_icon_button(parent, UI_CORNER + 0 * (bw + gap), by_bot,
                                     bw, "sleep",  "Z", game_on_sleep);
    ui.btn_decor  = make_icon_button(parent, UI_CORNER + 1 * (bw + gap), by_bot,
                                     bw, "decor",  "G", game_on_gift);
    /* The menu button uses the STATUS icon. The status page is gone -- its
     * content is all reachable from the menu now -- so the icon was free, and
     * it reads as "info" better than a generic hamburger did. */
    ui.btn_theme  = make_icon_button(parent, UI_CORNER + 2 * (bw + gap), by_bot,
                                     bw, "status", "M", game_on_menu);
    ui.btn_status = NULL;

    /* Register the two groups. The bubble and the gift menu are deliberately
     * NOT chrome: they are transient feedback and a modal. */
    for (int i = 0; i < NUM_STATS; i++) {
        chrome_add_top(s_ring[i]);
        chrome_add_top(s_ring_label[i]);
    }
    chrome_add_top(s_level_label);
    chrome_add_top(ui.stage_label);
    chrome_add_top(ui.age_label);

    chrome_add_bot(ui.btn_feed);
    chrome_add_bot(ui.btn_clean);
    chrome_add_bot(ui.btn_play);
    chrome_add_bot(ui.btn_sleep);
    chrome_add_bot(ui.btn_decor);
    chrome_add_bot(ui.btn_theme);

    /* Start hidden: the tank is the point, the chrome is on demand. */
    s_top_visible = false;
    s_bot_visible = false;
    for (int i = 0; i < s_chrome_top_n; i++) lv_obj_set_hidden(s_chrome_top[i], true);
    for (int i = 0; i < s_chrome_bot_n; i++) lv_obj_set_hidden(s_chrome_bot[i], true);

    /* --- hamburger ----------------------------------------------------
     *
     * A small translucent dot in the top-left corner, instead of the old
     * invisible 56px touch bands along the top and bottom edges.
     *
     * The bands were the problem: they covered a strip of the tank, so a drag
     * that started near an edge revealed the chrome instead of pulling the
     * slingshot. That made the hunting gesture fail in exactly the places a
     * thumb naturally rests.
     *
     * It is NOT part of the chrome groups, so it stays visible when they hide.
     *
     * The visible dot is deliberately small and faint so it does not compete
     * with the tank, but the TOUCH target is larger than the dot: the button is
     * HAMBURGER_TOUCH square with a transparent background, and the dot is a
     * child centred inside it. A 36px target was too small to hit reliably. */
    s_hamburger = lv_button_create(parent);
    lv_obj_set_size(s_hamburger, HAMBURGER_TOUCH, HAMBURGER_TOUCH);
    lv_obj_set_pos(s_hamburger, HAMBURGER_INSET, HAMBURGER_INSET);
    lv_obj_set_style_bg_opa(s_hamburger, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_hamburger, 0, 0);
    lv_obj_set_style_shadow_width(s_hamburger, 0, 0);
    lv_obj_set_style_pad_all(s_hamburger, 0, 0);
    lv_obj_add_event_cb(s_hamburger, on_hamburger, LV_EVENT_CLICKED, NULL);

    /* The visible dot. Partially transparent so it reads as part of the glass
     * rather than a button sitting on top of the tank. */
    lv_obj_t *dot = lv_obj_create(s_hamburger);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, HAMBURGER_DOT, HAMBURGER_DOT);
    lv_obj_center(dot);
    lv_obj_set_style_radius(dot, HAMBURGER_DOT / 2, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_20, 0);
    lv_obj_set_clickable(dot, false);
    lv_obj_set_scrollable(dot, false);

    /* Three stacked bars inside the dot. */
    for (int i = 0; i < 3; i++) {
        lv_obj_t *bar = lv_obj_create(dot);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, 12, 2);
        lv_obj_set_pos(bar, (HAMBURGER_DOT - 12) / 2, 7 + i * 5);
        lv_obj_set_style_bg_color(bar, lv_color_hex(COL_TEXT), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_70, 0);
        lv_obj_set_style_radius(bar, 1, 0);
        lv_obj_set_clickable(bar, false);
        lv_obj_set_scrollable(bar, false);
    }

    /* --- bubble ------------------------------------------------------ */
    s_bubble = lv_obj_create(parent);
    lv_obj_remove_style_all(s_bubble);
    lv_obj_set_size(s_bubble, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(s_bubble, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_bg_opa(s_bubble, LV_OPA_90, 0);
    lv_obj_set_style_radius(s_bubble, 8, 0);
    lv_obj_set_style_pad_all(s_bubble, 5, 0);
    lv_obj_set_style_border_width(s_bubble, 1, 0);
    lv_obj_set_style_border_color(s_bubble, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_clickable(s_bubble, false);
    lv_obj_set_scrollable(s_bubble, false);

    s_bubble_label = lv_label_create(s_bubble);
    lv_label_set_text(s_bubble_label, "");
    lv_obj_set_style_text_font(s_bubble_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_bubble_label, lv_color_hex(COL_TEXT), 0);
    lv_obj_center(s_bubble_label);

    lv_obj_set_hidden(s_bubble, true);

    /* --- gift placement hint ----------------------------------------- */
    s_gift_hint = lv_label_create(parent);
    lv_label_set_text(s_gift_hint, "tap the tank to give");
    lv_obj_set_style_text_font(s_gift_hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_gift_hint, lv_color_hex(COL_HUNGER), 0);
    /* Sits just above the two-row tray. */
    lv_obj_align(s_gift_hint, LV_ALIGN_BOTTOM_MID, 0, -HUD_BOTTOM_H - 4);
    lv_obj_set_hidden(s_gift_hint, true);

    /* --- gift picker -------------------------------------------------- */
    build_gift_menu(parent);
}

/* ------------------------------------------------------------------ */
/* Bubble                                                              */
/* ------------------------------------------------------------------ */
void ui_hud_bubble(const char *text, uint32_t color, uint32_t ms)
{
    if (!s_bubble || !s_bubble_label) return;

    lv_label_set_text(s_bubble_label, text);
    lv_obj_set_style_text_color(s_bubble_label, lv_color_hex(color), 0);
    lv_obj_set_hidden(s_bubble, false);
    s_bubble_until = lv_tick_get() + ms;
}

void ui_hud_set_gift_mode(bool on)
{
    if (s_gift_hint) lv_obj_set_hidden(s_gift_hint, !on);
}

/* ------------------------------------------------------------------ */
/* Auto-hiding chrome                                                  */
/* ------------------------------------------------------------------ */
void ui_hud_set_chrome(bool on)
{
    /* Kept for callers that want everything at once (the gift picker). */
    ui_hud_set_top(on);
    ui_hud_set_bot(on);
}

void ui_hud_set_top(bool on)
{
    s_top_visible = on;
    for (int i = 0; i < s_chrome_top_n; i++) {
        lv_obj_set_hidden(s_chrome_top[i], !on);
    }
    if (on) s_top_until = lv_tick_get() + CHROME_HOLD_MS;
}

void ui_hud_set_bot(bool on)
{
    s_bot_visible = on;
    for (int i = 0; i < s_chrome_bot_n; i++) {
        lv_obj_set_hidden(s_chrome_bot[i], !on);
    }
    if (on) s_bot_until = lv_tick_get() + CHROME_HOLD_MS;
}

void ui_hud_reveal_top(void)
{
    ui_hud_set_top(true);
}

void ui_hud_reveal_bot(void)
{
    ui_hud_set_bot(true);
}

bool ui_hud_chrome_visible(void)
{
    return s_top_visible || s_bot_visible;
}

/* ------------------------------------------------------------------ */
/* Tick                                                                */
/* ------------------------------------------------------------------ */
void ui_hud_tick(uint32_t now_ms)
{
    const uint8_t values[NUM_STATS] = {
        g.hunger, g.mood, g.energy, g.hygiene,
    };

    for (int i = 0; i < NUM_STATS; i++) {
        if (s_ring[i]) lv_arc_set_value(s_ring[i], values[i]);

        if (s_ring_label[i]) {
            char buf[8];
            snprintf(buf, sizeof buf, "%u", (unsigned)values[i]);
            lv_label_set_text(s_ring_label[i], buf);
        }
    }

    if (s_level_label) {
        char buf[16];
        snprintf(buf, sizeof buf, "Lv %lu", (unsigned long)g.level);
        lv_label_set_text(s_level_label, buf);
    }

    if (ui.stage_label) {
        lv_label_set_text(ui.stage_label, needs_stage_name());
    }

    if (ui.age_label) {
        char buf[24];
        snprintf(buf, sizeof buf, "day %lu", (unsigned long)needs_days_lived());
        lv_label_set_text(ui.age_label, buf);
    }

    /* Auto-hide each group independently once its hold expires. The gift
     * picker keeps the bottom group up while open, because the player is
     * mid-action. */
    if (s_top_visible && (int32_t)(now_ms - s_top_until) >= 0) {
        ui_hud_set_top(false);
    }
    if (s_bot_visible && !ui_hud_gift_menu_visible() &&
        (int32_t)(now_ms - s_bot_until) >= 0) {
        ui_hud_set_bot(false);
    }

    /* No aim prompt. The focus ring and the crouch pose already say "act now",
     * and the player asked for the on-screen instructions to go. */

    /* EMOTION IS EXPRESSED BY THE BODY, NOT BY TEXT.
     *
     * The old code narrated the spider with bubbles ("hungry", "sleepy",
     * "watching you"). That read as noise: a constant stream of words that the
     * player learns to ignore. The spider now emotes through its legs, and the
     * bubble is reserved for things the player must ACT on (the aim prompt) or
     * explicit feedback from a button press.
     *
     * The emote is derived from state here rather than pushed from behaviour,
     * so there is one place that decides how the spider looks. It is sent to
     * the canvas renderer, which expresses it through the legs. */
    cspider_emote_t emote = CSP_EMOTE_NONE;

    if (g.state == SPIDER_SLEEP) {
        emote = CSP_EMOTE_SLEEPY;
    } else if (g.state == SPIDER_SICK) {
        emote = CSP_EMOTE_SAD;
    } else if (g.state == SPIDER_AIM || g.state == SPIDER_STALK) {
        emote = CSP_EMOTE_ALERT;
    } else if (g.state == SPIDER_EAT || g.state == SPIDER_PLAY) {
        emote = CSP_EMOTE_HAPPY;
    } else if (g.mood < MOOD_REFUSE_HUNT) {
        emote = CSP_EMOTE_SAD;
    } else if (g.mood > 70 && g.bond > 40) {
        emote = CSP_EMOTE_HAPPY;
    }
    cspider_set_emote(emote);

    /* NO INSTRUCTION BUBBLES.
     *
     * The player asked for these to go: a constant stream of "tap to aim",
     * "hunting", "hungry" is noise that trains you to ignore the bubble
     * entirely. The slingshot is discoverable by touch -- you press and the
     * band appears -- and the spider's state is now shown by its legs.
     *
     * The bubble remains available for explicit button feedback (ui_hud_bubble
     * is still called from the care actions), but nothing auto-drives it. */

    /* Bubble follows the spider and expires. */
    if (s_bubble) {
        if (now_ms < s_bubble_until) {
            lv_obj_set_pos(s_bubble, g.spider_x - 24, g.spider_y - 56);
        } else {
            lv_obj_set_hidden(s_bubble, true);
        }
    }
}
