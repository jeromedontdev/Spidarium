/*
 * Spidarium — first-start setup, implementation.
 *
 * A single screen with a name field and a row of colour swatches. Deliberately
 * simple: this is the first thing the player sees, so it must not feel like a
 * form.
 */
#include "ui_setup.h"

#include "game.h"
#include "game_config.h"
#include "spider.h"

#include <stdio.h>
#include <string.h>

#define COL_BG      0x04050a
#define COL_PANEL   0x15181f
#define COL_BORDER  0x2a2f3a
#define COL_TEXT    0xdfe7ff
#define COL_DIM     0x8b93a7
#define COL_ACCENT  0xf5b342

static lv_obj_t *s_screen;
static lv_obj_t *s_name_ta;
static lv_obj_t *s_kb;

/* Four colour rows: body, pupils, legs, fangs. Each row is an independent
 * picker, so the player can recolour any part without touching the others. */
#define SETUP_ROWS 4
#define SETUP_SWATCH_MAX 9

static lv_obj_t *s_swatch[SETUP_ROWS][SETUP_SWATCH_MAX];
static uint8_t   s_picked[SETUP_ROWS];

/* Which game_state_t field each row writes to. */
static uint8_t *row_field(int row)
{
    switch (row) {
    case 0:  return &g.colour;
    case 1:  return &g.pupil_colour;
    case 2:  return &g.leg_colour;
    default: return &g.fang_colour;
    }
}

/* How many swatches a row has, and what colour each one is. */
static int row_count(int row)
{
    return (row == 0) ? (int)spider_colour_count() : (int)spider_accent_count();
}

static uint32_t row_colour(int row, int i)
{
    return (row == 0) ? spider_colour_swatch((uint8_t)i)
                      : spider_accent_colour((uint8_t)i);
}

/* ------------------------------------------------------------------ */
/* Live preview                                                        */
/* ------------------------------------------------------------------ */
/* The preview is the REAL spider, not a drawing of one. spider.c builds it with
 * the same anatomy_build() it uses for the live spider, so the two cannot drift
 * apart. The previous version hand-placed a handful of circles and looked
 * nothing like the creature it was supposed to be previewing. */

/* The preview sits in the top-right corner, above the colour rows. The anatomy
 * spans about 89px, so a 104px box holds it with a little margin. */
#define PV_BOX   104
#define PV_X     (ui.W - UI_CORNER - PV_BOX)
#define PV_Y     72

/* Keyboard height. The stock 200px layout put four rows of small keys in the
 * bottom fifth of the screen; 300px with one fewer row roughly doubles the area
 * of each key. */
#define KB_H     300

/* Three maps: lower, upper and symbols.
 *
 * The mode-switch buttons must use the EXACT strings the keyboard's own handler
 * compares against -- "ABC", "abc" and "1#" -- because those macros live in
 * lv_keyboard.c and are not exported. Using the same literals makes the stock
 * handler switch modes for us, and because we register our maps at the
 * TEXT_LOWER / TEXT_UPPER / SPECIAL slots, it switches to OUR maps rather than
 * the stock ones.
 *
 * Still no cursor keys, no keyboard-hide key, no separate symbol row: a name is
 * at most 15 characters. */
static const char *const kb_map_lower[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
    "ABC", "z", "x", "c", "v", "b", "n", "m", LV_SYMBOL_BACKSPACE, "\n",
    "1#", " ", LV_SYMBOL_OK, ""
};

static const lv_buttonmatrix_ctrl_t kb_ctrl_lower[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 1, 1, 1, 1, 1, 1, 1, 2,
    2, 6, 2
};

static const char *const kb_map_upper[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
    "abc", "Z", "X", "C", "V", "B", "N", "M", LV_SYMBOL_BACKSPACE, "\n",
    "1#", " ", LV_SYMBOL_OK, ""
};

static const lv_buttonmatrix_ctrl_t kb_ctrl_upper[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 1, 1, 1, 1, 1, 1, 1, 2,
    2, 6, 2
};

static const char *const kb_map_special[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "-", "_", "=", "+", "(", ")", "[", "]", "{", "}", "\n",
    "abc", ".", ",", ":", ";", "!", "?", "/", LV_SYMBOL_BACKSPACE, "\n",
    " ", LV_SYMBOL_OK, ""
};

static const lv_buttonmatrix_ctrl_t kb_ctrl_special[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 1, 1, 1, 1, 1, 1, 1, 2,
    8, 2
};

static lv_obj_t *s_pv_box;

static void preview_build(lv_obj_t *parent)
{
    s_pv_box = lv_obj_create(parent);
    lv_obj_remove_style_all(s_pv_box);
    lv_obj_set_size(s_pv_box, PV_BOX, PV_BOX);
    lv_obj_set_pos(s_pv_box, PV_X, PV_Y);
    lv_obj_set_style_bg_color(s_pv_box, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_bg_opa(s_pv_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_pv_box, 14, 0);
    lv_obj_set_style_border_width(s_pv_box, 1, 0);
    lv_obj_set_style_border_color(s_pv_box, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_scrollable(s_pv_box, false);
    lv_obj_set_clickable(s_pv_box, false);

    /* The real anatomy, built into the box and centred in it. */
    spider_preview_init(s_pv_box);
}

/* Keyboard events: hide it when the player confirms or cancels. */
static void on_kb_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_set_hidden(s_kb, true);
        lv_obj_remove_state(s_name_ta, LV_STATE_FOCUSED);
    }
}

/* Show the keyboard when the name field is focused, hide it otherwise. */
static void on_ta_focus(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(s_kb, s_name_ta);
        lv_obj_set_hidden(s_kb, false);
        lv_obj_move_foreground(s_kb);
    } else if (code == LV_EVENT_DEFOCUSED) {
        lv_obj_set_hidden(s_kb, true);
    }
}

bool ui_setup_needed(void)
{
    return g.name[0] == '\0';
}

/* A swatch carries its row in the high byte and its index in the low byte, so a
 * single callback can serve every row. */
static void on_swatch(lv_event_t *e)
{
    lv_obj_t *o = (lv_obj_t *)lv_event_get_target(e);
    uintptr_t packed = (uintptr_t)lv_obj_get_user_data(o);
    int row = (int)(packed >> 8);
    int idx = (int)(packed & 0xFF);

    s_picked[row] = (uint8_t)idx;

    /* Highlight the chosen swatch in this row only. */
    for (int i = 0; i < row_count(row) && i < SETUP_SWATCH_MAX; i++) {
        if (!s_swatch[row][i]) continue;
        lv_obj_set_style_border_width(s_swatch[row][i], (i == idx) ? 3 : 1, 0);
        lv_obj_set_style_border_color(s_swatch[row][i],
            lv_color_hex((i == idx) ? COL_ACCENT : COL_BORDER), 0);
    }

    /* Apply immediately so the player can see the effect on the live spider and
     * on the preview beside the pickers. */
    *row_field(row) = (uint8_t)idx;
    spider_refresh_colours();
    spider_preview_refresh();
}

static void on_start(lv_event_t *e)
{
    (void)e;

    const char *txt = lv_textarea_get_text(s_name_ta);
    if (txt && txt[0]) {
        strncpy(g.name, txt, sizeof g.name - 1);
        g.name[sizeof g.name - 1] = '\0';
    } else {
        strcpy(g.name, "spider");
    }

    /* All four pickers are already written into g by on_swatch, but write them
     * again here so a player who never taps a swatch still gets the defaults
     * that were seeded in ui_setup_init(). */
    g.colour       = s_picked[0];
    g.pupil_colour = s_picked[1];
    g.leg_colour   = s_picked[2];
    g.fang_colour  = s_picked[3];
    spider_refresh_colours();
    game_save_now();

    /* Hand over to the tank. */
    lv_screen_load(ui.screen);
}

void ui_setup_init(void)
{
    /* Seed the pickers from whatever is already stored, so a factory reset that
     * lands here shows the defaults rather than an arbitrary selection. */
    s_picked[0] = g.colour;
    s_picked[1] = g.pupil_colour;
    s_picked[2] = g.leg_colour;
    s_picked[3] = g.fang_colour;

    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, ui.W, ui.H);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_scrollable(s_screen, false);

    /* Title. */
    lv_obj_t *t = lv_label_create(s_screen);
    lv_label_set_text(t, "a new spider");
    lv_obj_set_style_text_font(t, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(COL_TEXT), 0);
    /* The title is centred, so it clears the squircle's corner wedges even
     * though it sits above the UI_CORNER inset. */
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 34);

    /* Name field. */
    lv_obj_t *lbl = lv_label_create(s_screen);
    lv_label_set_text(lbl, "name");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_DIM), 0);
    lv_obj_set_pos(lbl, UI_CORNER, 76);

    /* The name field stops short of the right edge to leave room for the
     * preview, which sits beside it. */
    s_name_ta = lv_textarea_create(s_screen);
    lv_obj_set_size(s_name_ta, 272, 48);
    lv_obj_set_pos(s_name_ta, UI_CORNER, 94);
    lv_textarea_set_one_line(s_name_ta, true);
    lv_textarea_set_max_length(s_name_ta, sizeof g.name - 1);
    lv_textarea_set_placeholder_text(s_name_ta, "spider");
    lv_obj_set_style_bg_color(s_name_ta, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_border_color(s_name_ta, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_text_color(s_name_ta, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(s_name_ta, &lv_font_montserrat_14, 0);

    /* On-screen keyboard.
     *
     * There is no physical keyboard on this device, so without this the name
     * field was simply untypable. The keyboard is created once and shown when
     * the field is focused, hidden otherwise, so it does not cover the colour
     * swatches while the player is choosing one. */
    s_kb = lv_keyboard_create(s_screen);
    lv_obj_set_size(s_kb, ui.W, KB_H);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(s_kb, on_kb_event, LV_EVENT_ALL, NULL);
    lv_obj_set_hidden(s_kb, true);

    /* Register our maps at the three mode slots the stock handler switches
     * between, so its own mode-switch logic drives our layouts. */
    lv_keyboard_set_map(s_kb, LV_KEYBOARD_MODE_TEXT_LOWER, kb_map_lower,
                        kb_ctrl_lower);
    lv_keyboard_set_map(s_kb, LV_KEYBOARD_MODE_TEXT_UPPER, kb_map_upper,
                        kb_ctrl_upper);
    lv_keyboard_set_map(s_kb, LV_KEYBOARD_MODE_SPECIAL, kb_map_special,
                        kb_ctrl_special);
    lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_TEXT_LOWER);

    lv_obj_add_event_cb(s_name_ta, on_ta_focus, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_name_ta, on_ta_focus, LV_EVENT_DEFOCUSED, NULL);

    preview_build(s_screen);

    /* Colour rows: body, pupils, legs, fangs.
     *
     * The label sits on the left and every swatch row starts at the same fixed
     * x, so the two share one left edge. Previously the label was pinned to the
     * corner inset while the swatches were centred, which left them visibly out
     * of line. */
    static const char *row_label[SETUP_ROWS] = { "body", "pupils", "legs", "fangs" };

    const int32_t sw    = 32;
    const int32_t gap   = 6;
    const int32_t row_h = 42;
    const int32_t y0    = 196;
    const int32_t sx0   = 100;   /* fixed left edge shared by every row */

    /* Vertical budget: the start button's top edge is at H - UI_CORNER - 66 =
     * 374. Four rows of row_h from y0 finish at 364, leaving 10px of clearance
     * so the last row of fang swatches stays tappable. */
    for (int r = 0; r < SETUP_ROWS; r++) {
        const int32_t ry = y0 + r * row_h;

        lv_obj_t *rl = lv_label_create(s_screen);
        lv_label_set_text(rl, row_label[r]);
        lv_obj_set_style_text_font(rl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(rl, lv_color_hex(COL_DIM), 0);
        /* Vertically centred on the swatch row, not on the row box. */
        lv_obj_set_pos(rl, UI_CORNER, ry + 13);

        const int n = row_count(r);

        for (int i = 0; i < n && i < SETUP_SWATCH_MAX; i++) {
            lv_obj_t *o = lv_obj_create(s_screen);
            lv_obj_remove_style_all(o);
            lv_obj_set_size(o, sw, sw);
            lv_obj_set_pos(o, sx0 + i * (sw + gap), ry + 5);
            lv_obj_set_style_radius(o, sw / 2, 0);
            lv_obj_set_style_bg_color(o, lv_color_hex(row_colour(r, i)), 0);
            lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);

            bool sel = (s_picked[r] == (uint8_t)i);
            lv_obj_set_style_border_width(o, sel ? 3 : 1, 0);
            lv_obj_set_style_border_color(o,
                lv_color_hex(sel ? COL_ACCENT : COL_BORDER), 0);
            lv_obj_set_clickable(o, true);
            lv_obj_set_user_data(o, (void *)(uintptr_t)((r << 8) | i));
            lv_obj_add_event_cb(o, on_swatch, LV_EVENT_CLICKED, NULL);

            s_swatch[r][i] = o;
        }
    }

    /* Start button. */
    lv_obj_t *b = lv_button_create(s_screen);
    lv_obj_set_size(b, ui.W - 2 * UI_CORNER, 56);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -UI_CORNER - 10);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_add_event_cb(b, on_start, LV_EVENT_CLICKED, NULL);

    lv_obj_t *bl = lv_label_create(b);
    lv_label_set_text(bl, "start");
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(bl, lv_color_hex(0x10131a), 0);
    lv_obj_center(bl);
}

void ui_setup_show(void)
{
    if (!s_screen) return;

    /* Re-seed the pickers from the current state. A factory reset lands here
     * with g already back at defaults, and the swatch highlights must follow,
     * otherwise the screen would show the previous spider's colours. */
    s_picked[0] = g.colour;
    s_picked[1] = g.pupil_colour;
    s_picked[2] = g.leg_colour;
    s_picked[3] = g.fang_colour;

    for (int r = 0; r < SETUP_ROWS; r++) {
        for (int i = 0; i < row_count(r) && i < SETUP_SWATCH_MAX; i++) {
            if (!s_swatch[r][i]) continue;
            bool sel = (s_picked[r] == (uint8_t)i);
            lv_obj_set_style_border_width(s_swatch[r][i], sel ? 3 : 1, 0);
            lv_obj_set_style_border_color(s_swatch[r][i],
                lv_color_hex(sel ? COL_ACCENT : COL_BORDER), 0);
        }
    }

    spider_preview_refresh();
    lv_screen_load(s_screen);
}
