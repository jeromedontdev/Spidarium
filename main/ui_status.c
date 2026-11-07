/*
 * Spidarium — status page implementation.
 */
#include "ui_status.h"

#include "assets.h"
#include "audio.h"
#include "game_config.h"
#include "gift.h"
#include "needs.h"
#include "prey.h"
#include "sprites.h"

#include <stdio.h>

#define COL_BG        0x04050a
#define COL_PANEL     0x15181f
#define COL_BORDER    0x2a2f3a
#define COL_TEXT      0xdfe7ff
#define COL_DIM       0x8b93a7
#define COL_HUNGER    0xf5b342
#define COL_MOOD      0x58d68d
#define COL_ENERGY    0x7c9cff
#define COL_HYGIENE   0x4fd1c5

static lv_obj_t *s_screen;
static bool      s_visible;

static lv_obj_t *s_bar[NUM_STATS];
static lv_obj_t *s_bar_val[NUM_STATS];
static lv_obj_t *s_stage;
static lv_obj_t *s_age;
static lv_obj_t *s_level;
static lv_obj_t *s_xp;
static lv_obj_t *s_persona;
static lv_obj_t *s_streak;
static lv_obj_t *s_unlock;
static lv_obj_t *s_gift;
static lv_obj_t *s_ach[10];
static lv_obj_t *s_mute_btn;
static lv_obj_t *s_mute_label;

static const uint32_t k_colors[NUM_STATS] = {
    COL_HUNGER, COL_MOOD, COL_ENERGY, COL_HYGIENE,
};

static const char *k_names[NUM_STATS] = {
    "hunger", "mood", "energy", "hygiene",
};

/* "first pet" is gone: the pet mechanic was removed, so the achievement could
 * never be earned again. */
#define ACH_SHOWN 9

static const char *k_ach_names[ACH_SHOWN] = {
    "first meal", "first clean", "first play", "first hunt",
    "first molt", "ten meals", "adulthood", "level 5",
    "survived sickness",
};

static const uint32_t k_ach_bits[ACH_SHOWN] = {
    ACH_FIRST_MEAL, ACH_FIRST_CLEAN, ACH_FIRST_PLAY,
    ACH_FIRST_HUNT, ACH_FIRST_MOLT, ACH_TEN_MEALS, ACH_ADULT,
    ACH_LEVEL_5, ACH_SURVIVED_SICK,
};

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static lv_obj_t *make_label(lv_obj_t *parent, int32_t x, int32_t y,
                            const char *text, uint16_t font, uint32_t col)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    if (font <= 12)      lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    else if (font <= 14) lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    else                 lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_clickable(l, false);
    return l;
}

static lv_obj_t *make_bar(lv_obj_t *parent, int32_t x, int32_t y,
                          int32_t w, uint32_t col)
{
    lv_obj_t *b = lv_bar_create(parent);
    lv_obj_set_size(b, w, 12);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_style_radius(b, 6, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(b, lv_color_hex(col), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_clickable(b, false);
    lv_obj_set_scrollable(b, false);
    lv_bar_set_range(b, 0, 100);
    lv_bar_set_value(b, 60, LV_ANIM_OFF);
    return b;
}

static void on_back(lv_event_t *e)
{
    (void)e;
    ui_status_hide();
}

static void on_mute(lv_event_t *e)
{
    (void)e;
    audio_set_muted(!audio_muted());
    game_save_now();
}

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */
void ui_status_init(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, ui.W, ui.H);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_scrollable(s_screen, false);

    /* --- header ------------------------------------------------------ */
    make_label(s_screen, UI_CORNER, 12, "STATUS", 24, COL_TEXT);

    lv_obj_t *back = lv_button_create(s_screen);
    lv_obj_set_size(back, 44, 34);
    lv_obj_align(back, LV_ALIGN_TOP_RIGHT, -UI_CORNER, 10);
    lv_obj_set_style_bg_color(back, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_border_width(back, 1, 0);
    lv_obj_set_style_border_color(back, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_radius(back, 8, 0);
    lv_obj_add_event_cb(back, on_back, LV_EVENT_CLICKED, NULL);

    int idx = sprite_icon_index("back");
    const lv_image_dsc_t *dsc = (idx >= 0) ? assets_image(idx) : NULL;
    if (dsc) {
        lv_obj_t *img = lv_image_create(back);
        lv_image_set_src(img, dsc);
        lv_image_set_scale(img, 128);
        lv_obj_center(img);
    } else {
        lv_obj_t *l = lv_label_create(back);
        lv_label_set_text(l, "<");
        lv_obj_set_style_text_color(l, lv_color_hex(COL_TEXT), 0);
        lv_obj_center(l);
    }

    /* --- stat bars --------------------------------------------------- */
    int32_t y = 60;
    int32_t bar_x = UI_CORNER + 76;
    int32_t bar_w = ui.W - 2 * UI_CORNER - 76 - 44;

    for (int i = 0; i < NUM_STATS; i++) {
        make_label(s_screen, UI_CORNER, y + 1, k_names[i], 12, COL_DIM);
        s_bar[i] = make_bar(s_screen, bar_x, y, bar_w, k_colors[i]);

        s_bar_val[i] = make_label(s_screen, bar_x + bar_w + 6, y + 1, "0",
                                  12, k_colors[i]);
        y += 22;
    }

    /* --- life cycle --------------------------------------------------
     * Spacing is deliberately tight: the panel is 480 px tall and the mute
     * button sits at y=400. Adding the personality lines without compacting
     * pushed the achievement block underneath the button. */
    y += 8;
    s_stage = make_label(s_screen, UI_CORNER, y, "stage: spiderling", 14, COL_TEXT);
    y += 18;
    s_age = make_label(s_screen, UI_CORNER, y, "age: day 0", 14, COL_TEXT);
    y += 18;
    s_level = make_label(s_screen, UI_CORNER, y, "level 1", 14, COL_TEXT);
    y += 18;
    s_xp = make_label(s_screen, UI_CORNER, y, "xp 0 / 50", 14, COL_DIM);
    y += 18;
    s_persona = make_label(s_screen, UI_CORNER, y, "bold  bond 0", 14, COL_DIM);
    y += 18;
    s_streak = make_label(s_screen, UI_CORNER, y, "streak 0 / best 0", 14, COL_DIM);
    y += 18;
    s_unlock = make_label(s_screen, UI_CORNER, y, "next: beetle at level 6", 14, COL_DIM);
    y += 18;
    s_gift = make_label(s_screen, UI_CORNER, y, "gift: none", 14, COL_DIM);

    /* --- achievements ------------------------------------------------- */
    y += 22;
    make_label(s_screen, UI_CORNER, y, "ACHIEVEMENTS", 12, COL_DIM);
    y += 16;

    for (int i = 0; i < ACH_SHOWN; i++) {
        int col = i % 2;
        int row = i / 2;
        s_ach[i] = make_label(s_screen,
                              UI_CORNER + col * ((ui.W - 2 * UI_CORNER) / 2),
                              y + row * 14,
                              k_ach_names[i], 12, COL_DIM);
    }

    /* --- settings ----------------------------------------------------- */
    int32_t sy = ui.H - UI_CORNER - 40;

    s_mute_btn = lv_button_create(s_screen);
    lv_obj_set_size(s_mute_btn, 120, 34);
    lv_obj_set_pos(s_mute_btn, UI_CORNER, sy);
    lv_obj_set_style_bg_color(s_mute_btn, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_border_width(s_mute_btn, 1, 0);
    lv_obj_set_style_border_color(s_mute_btn, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_radius(s_mute_btn, 8, 0);
    lv_obj_add_event_cb(s_mute_btn, on_mute, LV_EVENT_CLICKED, NULL);

    s_mute_label = lv_label_create(s_mute_btn);
    lv_label_set_text(s_mute_label, "sound: on");
    lv_obj_set_style_text_font(s_mute_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_mute_label, lv_color_hex(COL_TEXT), 0);
    lv_obj_center(s_mute_label);
}

/* ------------------------------------------------------------------ */
/* Screen switching                                                    */
/* ------------------------------------------------------------------ */
void ui_status_show(void)
{
    if (!s_screen) return;
    lv_screen_load(s_screen);
    s_visible = true;
}

void ui_status_hide(void)
{
    if (!ui.screen) return;
    lv_screen_load(ui.screen);
    s_visible = false;
}

bool ui_status_visible(void)
{
    return s_visible;
}

/* ------------------------------------------------------------------ */
/* Tick                                                                */
/* ------------------------------------------------------------------ */
void ui_status_tick(uint32_t now_ms)
{
    (void)now_ms;
    if (!s_visible) return;

    const uint8_t values[NUM_STATS] = {
        g.hunger, g.mood, g.energy, g.hygiene,
    };

    for (int i = 0; i < NUM_STATS; i++) {
        if (s_bar[i]) lv_bar_set_value(s_bar[i], values[i], LV_ANIM_OFF);
        if (s_bar_val[i]) {
            char buf[8];
            snprintf(buf, sizeof buf, "%u", (unsigned)values[i]);
            lv_label_set_text(s_bar_val[i], buf);
        }
    }

    if (s_stage) {
        char buf[48];
        snprintf(buf, sizeof buf, "stage: %s%s", needs_stage_name(),
                 g.sick ? " (sick)" : "");
        lv_label_set_text(s_stage, buf);
    }

    if (s_age) {
        char buf[48];
        snprintf(buf, sizeof buf, "age: day %lu", (unsigned long)needs_days_lived());
        lv_label_set_text(s_age, buf);
    }

    if (s_level) {
        char buf[32];
        snprintf(buf, sizeof buf, "level %lu", (unsigned long)g.level);
        lv_label_set_text(s_level, buf);
    }

    if (s_xp) {
        char buf[48];
        uint32_t into = g.xp % XP_PER_LEVEL;
        snprintf(buf, sizeof buf, "xp %lu / %u", (unsigned long)into, XP_PER_LEVEL);
        lv_label_set_text(s_xp, buf);
    }

    if (s_persona) {
        char buf[48];
        snprintf(buf, sizeof buf, "%s  bond %u",
                 game_temperament()->name, (unsigned)g.bond);
        lv_label_set_text(s_persona, buf);
    }

    if (s_streak) {
        char buf[48];
        snprintf(buf, sizeof buf, "streak %u / best %u",
                 (unsigned)g.streak, (unsigned)g.best_streak);
        lv_label_set_text(s_streak, buf);
    }

    if (s_unlock) {
        char buf[56];
        /* The next species the player has NOT unlocked yet.
         *
         * The old code used prey_unlocked_species(), which returns a COUNT of
         * unlocked species, not an index of the next one. At level 1 that is 1,
         * so it reported "next: cricket at level 2" -- and once every species
         * was unlocked the count equalled PREY_SPECIES_COUNT and it claimed
         * "all species unlocked" even when only one existed. */
        int next = prey_next_locked_species();
        if (next >= 0) {
            snprintf(buf, sizeof buf, "next: %s at level %u",
                     prey_species_name((prey_species_t)next),
                     (unsigned)prey_unlock_level((prey_species_t)next));
        } else {
            snprintf(buf, sizeof buf, "all species unlocked");
        }
        lv_label_set_text(s_unlock, buf);
    }

    if (s_gift) {
        char buf[64];
        if (!gift_present()) {
            snprintf(buf, sizeof buf, "gift: none");
        } else {
            /* Plain words, not multipliers. "eR1.50 hD1.00 aW1.00" told the
             * player nothing. */
            snprintf(buf, sizeof buf, "gift: %s (%s)",
                     gift_kind_name(gift_kind()),
                     gift_kind_effect(gift_kind()));
        }
        lv_label_set_text(s_gift, buf);
    }

    for (int i = 0; i < ACH_SHOWN; i++) {
        if (!s_ach[i]) continue;
        bool got = (g.achievements & k_ach_bits[i]) != 0;
        lv_obj_set_style_text_color(s_ach[i],
                                    lv_color_hex(got ? COL_MOOD : COL_DIM), 0);
    }

    if (s_mute_label) {
        lv_label_set_text(s_mute_label, audio_muted() ? "sound: off" : "sound: on");
    }
}
