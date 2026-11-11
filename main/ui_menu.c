#include "ui_menu.h"

#include "game.h"
#include "game_config.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "esp_random.h"
#include "assets.h"
#include "sprites.h"
#include "species.h"
#include "power.h"
#include "ui_style.h"

#define COL_PANEL  0x16283c
#define COL_CARD   0x24405a
#define COL_TEXT   0xe8efda
#define COL_DIM    0x9aaeb5
#define COL_ACCENT 0x76d6b1
#define COL_CARD_SELECTED 0x2b6a5f
#define COL_RULE      0x76d6b1
#define COL_GOLD      0xb99b67
#define PAGE_ROWS 3
#define MENU_WIDTH (ui.W - 2 * UI_CORNER)
#define MENU_HEIGHT (ui.H - 2 * UI_CORNER)
#define PLAY_ROUNDS 8
#define MENU_ITEMS 40

static lv_obj_t *s_menu, *s_backdrop, *s_page_label, *s_prev, *s_next;
static lv_obj_t *s_row[PAGE_ROWS], *s_label[PAGE_ROWS], *s_detail[PAGE_ROWS];
static lv_obj_t *s_marker[PAGE_ROWS];
static lv_obj_t *s_accent[PAGE_ROWS];
static lv_obj_t *s_icons[PAGE_ROWS];
static lv_obj_t *s_checks[PAGE_ROWS];
static lv_obj_t *s_sliders[PAGE_ROWS];
static ui_menu_slider_cb s_slider_cb[MENU_ITEMS];
static bool s_check_items[MENU_ITEMS];
static int s_icon_indices[MENU_ITEMS];
static ui_menu_pick_cb s_cb;
static ui_menu_back_cb s_back;
static const ui_menu_item_t *s_items;
static int s_count, s_page;
static int s_destructive_id = -1;
static lv_obj_t *s_input, *s_input_error, *s_keyboard, *s_visibility_label;
static lv_obj_t *s_visibility_button;
static ui_menu_input_cb s_submit;
static bool s_password;
static int s_key_page;
static lv_timer_t *s_play_timer;
static lv_obj_t *s_play_prompt, *s_play_tiles[4], *s_play_text[4];
static lv_obj_t *s_play_images[4], *s_play_bar;
static lv_obj_t *s_play_target;
static ui_minigame_reward_cb s_play_reward;
static uint8_t s_play_kind, s_play_round, s_play_score, s_play_answer;
static bool s_play_ready, s_play_pending;
static uint32_t s_play_deadline;
static uint32_t s_play_duration, s_play_shift_at;
static bool s_play_preview;
static uint8_t s_play_bug, s_play_bugs[4], s_play_colors[4];
static uint8_t s_play_feints;
static uint8_t s_play_sequence[3], s_play_length, s_play_step, s_play_preview_step;
static uint32_t s_play_input_guard;
static const char *const k_play_colors[] = {"Coral", "Mint", "Gold", "Blue"};
static const uint32_t k_play_rgb[] = {0xb94646, 0x296e55, 0x846421, 0x315c94};

static int menu_icon(const char *text)
{
    if (!text) return sprite_icon_index("status");
    static const struct { const char *label, *icon; } icons[] = {
        {"Daily","daily"}, {"Check In","daily"}, {"Treats","treats"}, {"Backgrounds","backgrounds"},
        {"Achievements","achievements"}, {"Prey Species","prey"}, {"Decor","decor"},
        {"Settings","settings"}, {"WiFi","wifi"}, {"SD Card","sd"}, {"About","about"},
        {"Catch 3 Prey","feed"}, {"Clean 2 Times","clean"}, {"Play 3 Times","play"},
        {"Pet 4 Times","mood"}, {"Battery","battery"}, {"Runtime","battery"}, {"Display","display"},
        {"Power Mode","energy"}, {"Screen Timeout","sleep"}, {"Deep Sleep After","sleep"},
        {"RTC Wake Interval","energy"}, {"Deep Sleep Now","sleep"}, {"Save and Sleep","sleep"},
        {"Volume","volume"}, {"Sound","sound"},
        {"Background Music","music"},
        {"Brightness","energy"}, {"Auto Feed","auto_feed"}, {"Move","decor"}, {"Move Gift","decor"},
        {"Blocking","lock"}, {"Put Away","back"}, {"Put Gift Away","back"},
        {"Wipe Spider","reset"}, {"Save Screenshot","screenshot"},
    };
    for (size_t i = 0; i < sizeof icons / sizeof icons[0]; i++)
        if (!strcmp(text, icons[i].label)) return sprite_icon_index(icons[i].icon);
    return sprite_icon_index("status");
}

static const char *const k_lower[] = {
    "q","w","e","r","t","y","u","i","o","p","\n",
    "a","s","d","f","g","h","j","k","l","\n",
    "z","x","c","v","b","n","m",LV_SYMBOL_BACKSPACE,"\n",
    "Aa","123","Space",""
};
static const char *const k_upper[] = {
    "Q","W","E","R","T","Y","U","I","O","P","\n",
    "A","S","D","F","G","H","J","K","L","\n",
    "Z","X","C","V","B","N","M",LV_SYMBOL_BACKSPACE,"\n",
    "Aa","123","Space",""
};
static const char *const k_symbols[] = {
    "1","2","3","4","5","6","\n","7","8","9","0","!","@","\n",
    "#","$","%","^","&","*","\n","(",")","-","_","=","+","\n",
    "ABC","More","Space",LV_SYMBOL_BACKSPACE,""
};
static const char *const k_more[] = {
    "[","]","{","}","\\","|","\n",":",";","\"","'","<",">","\n",
    "?","/",".",",","`","~","\n","ABC","123","Space",LV_SYMBOL_BACKSPACE,""
};

void ui_menu_format_name(char *dst, size_t size, const char *name)
{
    snprintf(dst, size, "%s", name);
    bool start = true;
    for (char *p = dst; *p; p++) {
        if (start) *p = (char)toupper((unsigned char)*p);
        start = *p == ' ';
    }
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_clickable(obj, false);
    return obj;
}

static lv_obj_t *button(lv_obj_t *parent, int x, int y, int w, int h,
                        const char *text, lv_event_cb_t cb, void *data)
{
    lv_obj_t *obj = lv_button_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    ui_style_card(obj, 0x76b9ad, false);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x335568), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x1b2530), LV_STATE_DISABLED);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x39434c), LV_STATE_DISABLED);
    lv_obj_set_style_text_color(obj, lv_color_hex(0x64717b), LV_STATE_DISABLED);
    lv_obj_set_scrollable(obj, false);
    if (cb) lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, data);
    if (text) lv_obj_center(label(obj, text, &lv_font_montserrat_14));
    return obj;
}

void ui_menu_set_back(ui_menu_back_cb cb) { s_back = cb; }

static void on_back(lv_event_t *e)
{
    (void)e;
    ui_menu_back_cb cb = s_back;
    ui_menu_close();
    if (cb) cb();
}

void ui_menu_close(void)
{
    if (s_play_timer) lv_timer_delete(s_play_timer);
    s_play_timer = NULL;
    s_play_reward = NULL;
    if (s_menu) lv_obj_delete(s_menu);
    if (s_backdrop) {
        lv_obj_set_hidden(s_backdrop, true);
        lv_obj_delete_async(s_backdrop);
    }
    s_menu = s_backdrop = s_page_label = s_prev = s_next = NULL;
    s_input = s_input_error = s_keyboard = s_visibility_label = NULL;
    s_visibility_button = NULL;
    s_cb = NULL;
    s_back = NULL;
    s_items = NULL;
    s_submit = NULL;
    s_count = s_page = 0;
    s_destructive_id = -1;
    s_password = false;
}

bool ui_menu_visible(void) { return s_menu != NULL; }
bool ui_menu_showing(const ui_menu_item_t *items) { return s_menu && s_items == items; }

static void open_panel(const char *title)
{
    ui_menu_close();
    /* Menus must live inside the rotation wrapper, or they stay upright while
     * the playfield turns. */
    lv_obj_t *screen = power_rotation_layer(lv_screen_active());
    s_backdrop = lv_obj_create(screen);
    lv_obj_remove_style_all(s_backdrop);
    lv_obj_set_size(s_backdrop, ui.W, ui.H);
    lv_obj_set_style_bg_color(s_backdrop, lv_color_hex(0x080e18), 0);
    lv_obj_set_style_bg_opa(s_backdrop, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(s_backdrop, false);
    lv_obj_set_clickable(s_backdrop, true);

    s_menu = lv_obj_create(screen);
    lv_obj_remove_style_all(s_menu);
    lv_obj_set_size(s_menu, MENU_WIDTH, MENU_HEIGHT);
    lv_obj_center(s_menu);
    lv_obj_set_style_bg_color(s_menu, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_bg_opa(s_menu, LV_OPA_COVER, 0);
    ui_style_card(s_menu, COL_GOLD, false);
    lv_obj_set_scrollable(s_menu, false);
    lv_obj_set_clickable(s_menu, true);

    /* The corner Back button is the only Back control. A full-width Back row at
     * the bottom of a short list was redundant and pushed the real rows up. */
    lv_obj_t *back = button(s_menu, 12, 12, 96, 56, LV_SYMBOL_LEFT, on_back, NULL);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x1a2b3d), 0);
    lv_obj_set_style_border_color(back, lv_color_hex(COL_GOLD), 0);
    lv_obj_set_style_border_opa(back, LV_OPA_COVER, 0);

    lv_obj_t *heading = label(s_menu, title ? title : "", &lv_font_montserrat_18);
    lv_obj_set_pos(heading, 120, 26);
    lv_obj_set_width(heading, MENU_WIDTH - 132);
    lv_label_set_long_mode(heading, LV_LABEL_LONG_DOT);

    lv_obj_t *rule = lv_obj_create(s_menu);
    lv_obj_remove_style_all(rule);
    lv_obj_set_pos(rule, 120, 60);
    lv_obj_set_size(rule, MENU_WIDTH - 136, 4);
    lv_obj_set_style_bg_color(rule, lv_color_hex(COL_GOLD), 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
    lv_obj_set_clickable(rule, false);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *rivet = lv_obj_create(s_menu);
        lv_obj_remove_style_all(rivet);
        lv_obj_set_pos(rivet, i & 1 ? MENU_WIDTH - 8 : 4,
                      i & 2 ? MENU_HEIGHT - 8 : 4);
        lv_obj_set_size(rivet, 4, 4);
        lv_obj_set_style_bg_color(rivet, lv_color_hex(COL_GOLD), 0);
        lv_obj_set_style_bg_opa(rivet, LV_OPA_COVER, 0);
        lv_obj_set_clickable(rivet, false);
    }
}

static void refresh_page(void);
/* Rebuild the current page from the item table. Public so a caller that changes
 * a value behind the menu's back (the physical volume buttons) can refresh the
 * slider and its readout without closing and reopening the menu. */
void ui_menu_refresh(void)
{
    if (!s_items || !s_count) return;
    refresh_page();
}

static void refresh_page(void)
{
    int pages = (s_count + PAGE_ROWS - 1) / PAGE_ROWS;
    for (int i = 0; i < PAGE_ROWS; i++) {
        int index = s_page * PAGE_ROWS + i;
        lv_obj_set_hidden(s_row[i], index >= s_count);
        if (index >= s_count) continue;
        const ui_menu_item_t *it = &s_items[index];
        const lv_image_dsc_t *icon = s_icon_indices[index] >= 0 ?
            assets_image(s_icon_indices[index]) : NULL;
        lv_obj_set_hidden(s_icons[i], !icon);
        if (icon) {
            lv_image_set_src(s_icons[i], icon);
            lv_obj_set_size(s_icons[i], 48, 48);
            lv_image_set_inner_align(s_icons[i], LV_IMAGE_ALIGN_CONTAIN);
            lv_image_set_antialias(s_icons[i], false);
        }
        int text_x = icon ? 72 : 14;
        lv_obj_set_width(s_label[i], MENU_WIDTH - text_x - 68);
        lv_obj_set_width(s_detail[i], MENU_WIDTH - text_x - 68);
        lv_obj_set_x(s_detail[i], text_x);
        lv_label_set_text(s_label[i], it->label ? it->label : "");
        /* A slider row's readout is its value, not the caller's detail string.
         * The physical volume buttons change the value without rebuilding the
         * menu, so the percentage has to come from the same field the slider
         * position does, or the two disagree. */
        bool slider = s_slider_cb[index] != NULL;
        char detail[256];
        if (slider) snprintf(detail, sizeof detail, "%u%%", (unsigned)it->id);
        else snprintf(detail, sizeof detail, "%s", it->detail ? it->detail : "");
        if (detail[0] && !slider) detail[0] = (char)toupper((unsigned char)detail[0]);
        lv_label_set_text(s_detail[i], detail);
        bool multiline = strchr(detail, '\n') != NULL;
        lv_label_set_long_mode(s_detail[i], multiline ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_DOT);
        lv_obj_set_height(s_detail[i], multiline ? 45 : LV_SIZE_CONTENT);
        lv_obj_set_y(s_detail[i], multiline ? 26 : 48);
        lv_obj_set_style_text_color(s_label[i],
            lv_color_hex(it->id == s_destructive_id ? 0xff7777 : it->enabled ? COL_TEXT : COL_DIM), 0);
        lv_obj_set_style_bg_color(s_row[i],
            lv_color_hex(it->selected ? COL_CARD_SELECTED : COL_CARD), 0);
        lv_obj_set_style_border_color(s_row[i],
            lv_color_hex(it->selected ? COL_ACCENT : 0x2c4459), 0);
        lv_obj_set_style_border_opa(s_row[i], LV_OPA_COVER, 0);
        lv_obj_set_hidden(s_accent[i], !it->selected);
        lv_obj_set_clickable(s_row[i], it->enabled);
        lv_obj_set_hidden(s_checks[i], !s_check_items[index]);
        if (s_check_items[index]) {
            if (it->selected) lv_obj_add_state(s_checks[i], LV_STATE_CHECKED);
            else lv_obj_remove_state(s_checks[i], LV_STATE_CHECKED);
        }
        lv_obj_set_hidden(s_sliders[i], !slider);
        if (slider) lv_slider_set_value(s_sliders[i], it->id, LV_ANIM_OFF);
        lv_label_set_text(s_marker[i], s_check_items[index] || slider ? "" : it->selected ? LV_SYMBOL_OK :
            it->enabled && it->is_submenu ? LV_SYMBOL_RIGHT : "");
        lv_obj_set_hidden(s_detail[i], !detail[0]);
        lv_obj_set_pos(s_label[i], text_x, detail[0] ? multiline ? 2 : 16 : 30);
    }
    char text[32];
    snprintf(text, sizeof text, "%d / %d", s_page + 1, pages);
    lv_label_set_text(s_page_label, text);
    lv_obj_set_hidden(s_prev, false);
    lv_obj_set_hidden(s_next, false);
    if (s_page == 0) lv_obj_add_state(s_prev, LV_STATE_DISABLED);
    else lv_obj_remove_state(s_prev, LV_STATE_DISABLED);
    if (s_page + 1 == pages) lv_obj_add_state(s_next, LV_STATE_DISABLED);
    else lv_obj_remove_state(s_next, LV_STATE_DISABLED);
    lv_obj_set_style_text_color(lv_obj_get_child(s_prev, 0),
        lv_color_hex(s_page == 0 ? 0x64717b : COL_TEXT), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(s_next, 0),
        lv_color_hex(s_page + 1 == pages ? 0x64717b : COL_TEXT), 0);
}

void ui_menu_set_item_icon(uint8_t item, int asset_index)
{
    if (!s_items || item >= s_count || item >= MENU_ITEMS) {
        ESP_LOGE("spidarium.menu", "item icon rejected: invalid row %u", item);
        return;
    }

    s_icon_indices[item] = asset_index;
    refresh_page();
}

void ui_menu_set_item_checkbox(uint8_t item)
{
    if (!s_items || item >= s_count || item >= MENU_ITEMS) {
        ESP_LOGE("spidarium.menu", "checkbox rejected: invalid row %u", item);
        return;
    }
    s_check_items[item] = true;
    refresh_page();
}

void ui_menu_set_item_slider(uint8_t item, uint8_t value, ui_menu_slider_cb cb)
{
    if (!s_items || item >= s_count || item >= MENU_ITEMS || !cb) {
        ESP_LOGE("spidarium.menu", "slider rejected: invalid row %u", item);
        return;
    }
    s_slider_cb[item] = cb;
    /* The row's id carries the current value so refresh_page() can show it. */
    ((ui_menu_item_t *)s_items)[item].id = value > 100 ? 100 : value;
    refresh_page();
}

void ui_menu_set_slider_value(uint8_t item, uint8_t value)
{
    if (!s_items || item >= s_count || item >= MENU_ITEMS || !s_slider_cb[item]) return;
    ((ui_menu_item_t *)s_items)[item].id = value > 100 ? 100 : value;
    refresh_page();
}

int ui_menu_page(void) { return s_page; }

void ui_menu_capture_hide(bool hidden)
{
    static bool backdrop_hidden, menu_hidden;
    if (hidden) {
        backdrop_hidden = s_backdrop && lv_obj_is_hidden(s_backdrop);
        menu_hidden = s_menu && lv_obj_is_hidden(s_menu);
    }
    if (s_backdrop) lv_obj_set_hidden(s_backdrop, hidden || backdrop_hidden);
    if (s_menu) lv_obj_set_hidden(s_menu, hidden || menu_hidden);
}

void ui_menu_set_page(int page)
{
    if (!s_items || page < 0 || page >= (s_count + PAGE_ROWS - 1) / PAGE_ROWS) {
        ESP_LOGE("spidarium.menu", "page rejected: %d", page);
        return;
    }
    s_page = page;
    refresh_page();
}

void ui_menu_set_destructive(uint8_t id)
{
    s_destructive_id = id;
    if (s_items) refresh_page();
}

static void play_round_start(void)
{
    char text[96];
    s_play_answer = esp_random() % 4;
    s_play_ready = s_play_kind != 2;
    s_play_pending = false;
    s_play_preview = s_play_kind == 1;
    s_play_duration = s_play_kind == 2 ? 620 - s_play_round * 45 :
                      2000 - s_play_round * 110;
    s_play_feints = 0;
    s_play_length = 1 + s_play_round / 3;
    s_play_step = s_play_preview_step = 0;
    s_play_input_guard = 0;
    s_play_sequence[0] = s_play_answer;
    for (int i = 1; i < s_play_length; i++) s_play_sequence[i] = esp_random() % 4;
    s_play_bug = esp_random() % PREY_SPECIES_COUNT;
    for (int i = 0; i < 4; i++) {
        s_play_bugs[i] = (s_play_bug + i + 1) % PREY_SPECIES_COUNT;
        s_play_colors[i] = i;
    }
    s_play_bugs[s_play_answer] = s_play_bug;
    /* Shuffle the decoy bugs too, so the correct one is not always the only
     * species that appears once. */
    for (int i = 3; i > 0; i--) {
        int j = esp_random() % (i + 1);
        uint8_t bug = s_play_bugs[i];
        s_play_bugs[i] = s_play_bugs[j];
        s_play_bugs[j] = bug;
    }
    for (int i = 0; i < 4; i++)
        if (s_play_bugs[i] == s_play_bug) s_play_answer = (uint8_t)i;
    for (int i = 3; i > 0; i--) {
        int j = esp_random() % (i + 1);
        uint8_t color = s_play_colors[i];
        s_play_colors[i] = s_play_colors[j];
        s_play_colors[j] = color;
    }
    s_play_deadline = lv_tick_get() + (s_play_preview ? 750 :
        s_play_kind == 2 ? 900 + esp_random() % 1600 : s_play_duration);
    /* Bugs shuffle far less often than they used to: the old 700 ms ramp made
     * the target move before it could be read. */
    s_play_shift_at = lv_tick_get() + 1500 - s_play_round * 60;
    snprintf(text, sizeof text, "Round %u/%u / Score %u\n%s%s",
             s_play_round + 1, PLAY_ROUNDS, s_play_score,
             s_play_kind == 0 ? "Find: " : s_play_kind == 1 ? "Remember: " : "",
             s_play_kind == 0 ? species_get(s_play_bug)->name :
             s_play_kind == 1 ? k_play_colors[s_play_colors[s_play_answer]] : "Only Tap GREEN");
    if (s_play_kind == 1)
        snprintf(text, sizeof text, "Round %u/%u / Score %u\nRemember 1/%u: %s",
                 s_play_round + 1, PLAY_ROUNDS, s_play_score, s_play_length,
                 k_play_colors[s_play_colors[s_play_answer]]);
    if (s_play_kind == 0) {
        const lv_image_dsc_t *image = assets_image(sprite_prey_index(s_play_bug, false, false, 0));
        if (image) lv_image_set_src(s_play_target, image);
    }
    lv_label_set_text(s_play_prompt, text);
    lv_bar_set_value(s_play_bar, 100, LV_ANIM_OFF);
    for (int i = 0; i < 4; i++) {
        lv_obj_set_style_bg_color(s_play_tiles[i],
            lv_color_hex(s_play_kind == 1 ? k_play_rgb[s_play_colors[i]] : COL_CARD), 0);
        lv_label_set_text(s_play_text[i], s_play_kind == 0 ? "" :
                          s_play_kind == 1 ? "..." : "WAIT");
        if (s_play_kind == 0) {
            const lv_image_dsc_t *image = assets_image(sprite_prey_index(s_play_bugs[i], false, false, 0));
            if (image) lv_image_set_src(s_play_images[i], image);
            else lv_label_set_text(s_play_text[i], species_get(s_play_bugs[i])->name);
        }
    }
}

static void play_result_back(lv_event_t *e)
{
    on_back(e);
}

static void play_round_end(bool correct)
{
    if (correct) s_play_score++;
    s_play_round++;
    if (s_play_round < PLAY_ROUNDS) {
        s_play_pending = true;
        s_play_deadline = lv_tick_get() + 600;
        lv_label_set_text(s_play_prompt, correct ? "Nice!" : "Try Again");
        return;
    }
    uint8_t score = s_play_score;
    ui_minigame_reward_cb reward = s_play_reward;
    ui_menu_close();
    uint32_t xp = reward ? reward(score) : 0;
    open_panel("Play Complete");
    char text[112];
    snprintf(text, sizeof text, "%u / %u\n+%lu XP / +%u Silk\nMood +%u / Energy -%u",
             score, PLAY_ROUNDS, (unsigned long)xp, score * 3,
             PLAY_MOOD_GAIN, PLAY_ENERGY_COST);
    lv_obj_t *result = label(s_menu, text, &lv_font_montserrat_18);
    lv_obj_set_style_text_align(result, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(result, LV_ALIGN_TOP_MID, 0, 128);
    button(s_menu, 60, MENU_HEIGHT - 92, MENU_WIDTH - 120, 64,
           "Done", play_result_back, NULL);
}

static void play_pick(lv_event_t *e)
{
    if (!s_play_timer || s_play_pending || s_play_preview) return;
    uint32_t now = lv_tick_get();
    if (s_play_input_guard && (int32_t)(now - s_play_input_guard) < 0) return;
    bool in_time = (int32_t)(s_play_deadline - now) > 0;
    int index = (int)(uintptr_t)lv_event_get_user_data(e);
    if (s_play_kind == 1 && index == s_play_answer && in_time) {
        s_play_step++;
        if (s_play_step < s_play_length) {
            s_play_answer = s_play_sequence[s_play_step];
            s_play_input_guard = now + 150;
            lv_label_set_text(s_play_prompt, "Good / Next Color...");
            return;
        }
    }
    play_round_end(s_play_kind == 2 ? s_play_ready && in_time :
                   index == s_play_answer && in_time);
}

static void play_tick(lv_timer_t *timer)
{
    (void)timer;
    uint32_t now = lv_tick_get();
    if (!s_play_pending && !s_play_preview && s_play_ready) {
        int32_t left = (int32_t)(s_play_deadline - now);
        lv_bar_set_value(s_play_bar, left > 0 ? left * 100 / s_play_duration : 0, LV_ANIM_OFF);
    }
    if (s_play_kind == 0 && !s_play_pending && (int32_t)(now - s_play_shift_at) >= 0 &&
        (int32_t)(now - s_play_deadline) < 0) {
        int other = (s_play_answer + 1 + esp_random() % 3) % 4;
        uint8_t bug = s_play_bugs[other];
        s_play_bugs[other] = s_play_bug;
        s_play_bugs[s_play_answer] = bug;
        int old = s_play_answer;
        s_play_answer = other;
        const lv_image_dsc_t *a = assets_image(sprite_prey_index(bug, false, false, 0));
        const lv_image_dsc_t *b = assets_image(sprite_prey_index(s_play_bug, false, false, 0));
        if (a && b) {
            lv_image_set_src(s_play_images[old], a);
            lv_image_set_src(s_play_images[other], b);
        }
        s_play_shift_at = now + 1500 - s_play_round * 60;
    }
    if ((int32_t)(now - s_play_deadline) < 0) return;
    if (s_play_pending) {
        play_round_start();
    } else if (s_play_preview) {
        s_play_preview_step++;
        if (s_play_preview_step < s_play_length) {
            char prompt[64];
            snprintf(prompt, sizeof prompt, "Remember %u/%u: %s",
                     s_play_preview_step + 1, s_play_length,
                     k_play_colors[s_play_colors[s_play_sequence[s_play_preview_step]]]);
            lv_label_set_text(s_play_prompt, prompt);
            s_play_deadline = now + 750;
            return;
        }
        s_play_preview = false;
        s_play_deadline = now + s_play_duration;
        lv_label_set_text(s_play_prompt, s_play_length > 1 ?
                          "Repeat the Colors in Order" : "Match the Color You Remembered");
        for (int i = 0; i < 4; i++)
            lv_label_set_text(s_play_text[i], k_play_colors[s_play_colors[i]]);
    } else if (s_play_kind == 2 && !s_play_ready) {
        if (s_play_feints < (s_play_round >= 2 ? 2 : 1)) {
            s_play_feints++;
            s_play_deadline = now + 400 + esp_random() % 700;
            lv_label_set_text(s_play_text[0], s_play_feints == 1 ? "HOLD" : "NOT YET");
            lv_obj_set_style_bg_color(s_play_tiles[0],
                                     lv_color_hex(s_play_feints == 1 ? 0x846421 : 0xb94646), 0);
            return;
        }
        s_play_ready = true;
        s_play_deadline = now + s_play_duration;
        lv_label_set_text(s_play_prompt, "Tap Now!");
        lv_label_set_text(s_play_text[0], "Tap!");
        lv_obj_set_style_bg_color(s_play_tiles[0], lv_color_hex(0x296e55), 0);
    } else {
        play_round_end(false);
    }
}

static void play_open(uint8_t kind, ui_minigame_reward_cb reward)
{
    static const char *const names[] = {"Bug Hunt", "Color Match", "Quick Tap"};
    open_panel(names[kind]);
    s_play_kind = kind;
    s_play_round = s_play_score = 0;
    s_play_reward = reward;
    s_play_prompt = label(s_menu, "", &lv_font_montserrat_18);
    lv_obj_set_style_text_align(s_play_prompt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_play_prompt, MENU_WIDTH - 24);
    lv_obj_set_pos(s_play_prompt, 12, 84);
    s_play_target = lv_image_create(s_menu);
    lv_obj_set_pos(s_play_target, 12, 80);
    lv_obj_set_clickable(s_play_target, false);
    lv_obj_set_hidden(s_play_target, kind != 0);
    if (kind == 0) {
        lv_obj_set_pos(s_play_prompt, 72, 84);
        lv_obj_set_width(s_play_prompt, MENU_WIDTH - 84);
    }
    s_play_bar = lv_bar_create(s_menu);
    lv_obj_set_size(s_play_bar, MENU_WIDTH - 24, 8);
    lv_obj_set_pos(s_play_bar, 12, 132);
    lv_obj_set_clickable(s_play_bar, false);
    lv_obj_set_style_bg_color(s_play_bar, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_play_bar, 0, 0);
    lv_obj_set_style_radius(s_play_bar, 0, LV_PART_INDICATOR);
    for (int i = 0; i < 4; i++) {
        int w = (MENU_WIDTH - 36) / 2;
        s_play_tiles[i] = button(s_menu, 12 + (i % 2) * (w + 12),
                                148 + (i / 2) * 112, w, 100,
                                NULL, play_pick, (void *)(uintptr_t)i);
        s_play_text[i] = label(s_play_tiles[i], "", &lv_font_montserrat_24);
        lv_obj_center(s_play_text[i]);
        lv_obj_set_style_border_width(s_play_tiles[i], 2, 0);
        lv_obj_set_style_border_color(s_play_tiles[i], lv_color_hex(0x4b6072), 0);
        s_play_images[i] = lv_image_create(s_play_tiles[i]);
        lv_obj_center(s_play_images[i]);
        lv_obj_set_clickable(s_play_images[i], false);
        lv_obj_set_hidden(s_play_images[i], kind != 0);
        if (kind == 2) lv_obj_set_hidden(s_play_tiles[i], i != 0);
    }
    if (kind == 2) {
        lv_obj_set_pos(s_play_tiles[0], 12, 148);
        lv_obj_set_size(s_play_tiles[0], MENU_WIDTH - 24, 212);
    }
    play_round_start();
    s_play_timer = lv_timer_create(play_tick, 50, NULL);
    ESP_LOGI("spidarium.menu", "minigame started: %s", names[kind]);
}

void ui_menu_open_minigame(ui_minigame_reward_cb reward)
{
    static int previous = -1;
    int kind = esp_random() % 3;
    if (kind == previous) kind = (kind + 1 + esp_random() % 2) % 3;
    previous = kind;
    play_open((uint8_t)kind, reward);
}

static void on_page(lv_event_t *e)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    int page = s_page + delta;
    if (page >= 0 && page * PAGE_ROWS < s_count) {
        s_page = page;
        refresh_page();
    }
}

static void on_row(lv_event_t *e)
{
    int row = (int)(uintptr_t)lv_event_get_user_data(e);
    const ui_menu_item_t *it = &s_items[s_page * PAGE_ROWS + row];
    if (!it->enabled) return;
    uint8_t id = it->id;
    ui_menu_pick_cb cb = s_cb;
    ui_menu_close();
    if (cb) cb(id);
}

/* Slider rows report live; the menu stays open so the value can be dragged. */
static void on_slider(lv_event_t *e)
{
    int row = (int)(uintptr_t)lv_event_get_user_data(e);
    int index = s_page * PAGE_ROWS + row;
    if (index >= s_count || !s_slider_cb[index]) return;
    ui_menu_slider_cb cb = s_slider_cb[index];
    uint8_t id = s_items[index].id;
    uint8_t value = (uint8_t)lv_slider_get_value(s_sliders[row]);
    cb(id, value);
    /* Keep the row's stored value in step with the drag, so a later
     * refresh_page() (the physical volume buttons) does not snap the slider
     * back to the value the menu was opened with. */
    ((ui_menu_item_t *)s_items)[index].id = value;
    /* The detail label is the percentage readout. Without this it kept the
     * value the menu was opened with until the row was rebuilt. */
    if (s_detail[row]) {
        char text[24];
        snprintf(text, sizeof text, "%u%%", (unsigned)value);
        lv_label_set_text(s_detail[row], text);
    }
}

void ui_menu_open(const char *title, const ui_menu_item_t *items, int count,
                  ui_menu_pick_cb cb)
{
    if (!items || count <= 0 || count > MENU_ITEMS) {
        ESP_LOGE("spidarium.menu", "menu rejected: no items");
        return;
    }
    open_panel(title);
    s_items = items;
    s_count = count;
    for (int i = 0; i < MENU_ITEMS; i++) {
        s_icon_indices[i] = i < count ? menu_icon(items[i].label) : -1;
        s_check_items[i] = false;
        s_slider_cb[i] = NULL;
    }
    s_cb = cb;
    for (int i = 0; i < count; i++)
        if (items[i].selected) { s_page = i / PAGE_ROWS; break; }
    int w = MENU_WIDTH;
    for (int i = 0; i < PAGE_ROWS; i++) {
        s_row[i] = button(s_menu, 12, 76 + i * 84, w - 24, 76, NULL,
                          on_row, (void *)(uintptr_t)i);
        /* A lit bar down the left edge marks the selected row. It is a child of
         * the row so it moves and clips with it. */
        s_accent[i] = lv_obj_create(s_row[i]);
        lv_obj_remove_style_all(s_accent[i]);
        lv_obj_set_pos(s_accent[i], 0, 8);
        lv_obj_set_size(s_accent[i], 4, 60);
        lv_obj_set_style_bg_color(s_accent[i], lv_color_hex(COL_ACCENT), 0);
        lv_obj_set_style_bg_opa(s_accent[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_accent[i], 2, 0);
        lv_obj_set_clickable(s_accent[i], false);
        lv_obj_set_hidden(s_accent[i], true);
        s_icons[i] = lv_image_create(s_row[i]);
        lv_obj_set_pos(s_icons[i], 12, 12);
        lv_obj_set_clickable(s_icons[i], false);
        lv_obj_set_hidden(s_icons[i], true);
        s_label[i] = label(s_row[i], "", &lv_font_montserrat_18);
        lv_obj_set_width(s_label[i], w - 82);
        lv_label_set_long_mode(s_label[i], LV_LABEL_LONG_DOT);
        s_detail[i] = label(s_row[i], "", &lv_font_montserrat_12);
        lv_obj_set_pos(s_detail[i], 14, 48);
        lv_obj_set_width(s_detail[i], w - 82);
        lv_label_set_long_mode(s_detail[i], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(s_detail[i], lv_color_hex(COL_DIM), 0);
        s_marker[i] = label(s_row[i], "", &lv_font_montserrat_14);
        lv_obj_align(s_marker[i], LV_ALIGN_RIGHT_MID, -14, 0);
        s_checks[i] = lv_checkbox_create(s_row[i]);
        lv_checkbox_set_text(s_checks[i], "");
        lv_obj_set_style_radius(s_checks[i], UI_PIXEL_RADIUS, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(s_checks[i], lv_color_hex(COL_PANEL), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(s_checks[i], lv_color_hex(COL_ACCENT),
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_border_color(s_checks[i], lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
        lv_obj_align(s_checks[i], LV_ALIGN_RIGHT_MID, -14, 0);
        lv_obj_set_clickable(s_checks[i], false);
        lv_obj_set_hidden(s_checks[i], true);
        s_sliders[i] = lv_slider_create(s_row[i]);
        lv_obj_set_size(s_sliders[i], 132, 12);
        lv_obj_align(s_sliders[i], LV_ALIGN_RIGHT_MID, -14, 0);
        lv_slider_set_range(s_sliders[i], 0, 100);
        lv_obj_set_style_bg_color(s_sliders[i], lv_color_hex(0x30404b), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_sliders[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_sliders[i], lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(s_sliders[i], LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(s_sliders[i], lv_color_hex(COL_TEXT), LV_PART_KNOB);
        lv_obj_set_style_pad_all(s_sliders[i], 4, LV_PART_KNOB);
        lv_obj_set_style_radius(s_sliders[i], 0, LV_PART_MAIN);
        lv_obj_set_style_radius(s_sliders[i], 0, LV_PART_INDICATOR);
        lv_obj_set_style_radius(s_sliders[i], UI_PIXEL_RADIUS, LV_PART_KNOB);
        lv_obj_add_event_cb(s_sliders[i], on_slider, LV_EVENT_VALUE_CHANGED,
                            (void *)(uintptr_t)i);
        lv_obj_set_hidden(s_sliders[i], true);
    }
    /* The pager is two arrow buttons, matching the corner Back control. Text
     * labels ("Prev" / "Next") made the pager read as content rather than
     * chrome, and the words competed with the row labels above them. */
    s_prev = button(s_menu, 12, MENU_HEIGHT - 68, 96, 56, LV_SYMBOL_LEFT,
                    on_page, (void *)(intptr_t)-1);
    s_next = button(s_menu, w - 108, MENU_HEIGHT - 68, 96, 56, LV_SYMBOL_RIGHT,
                    on_page, (void *)(intptr_t)1);
    /* The pager is chrome, not content: a flatter fill keeps it from competing
     * with the rows above it. */
    for (int i = 0; i < 2; i++) {
        lv_obj_t *pager = i ? s_next : s_prev;
        lv_obj_set_style_bg_color(pager, lv_color_hex(0x16283a), 0);
        lv_obj_set_style_border_color(pager, lv_color_hex(0x2c4459), 0);
        lv_obj_set_style_border_opa(pager, LV_OPA_COVER, 0);
    }
    s_page_label = label(s_menu, "", &lv_font_montserrat_14);
    lv_obj_align(s_page_label, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_obj_set_style_text_color(s_page_label, lv_color_hex(COL_DIM), 0);
    refresh_page();
}

static void on_input_submit(lv_event_t *e)
{
    (void)e;
    char text[256];
    snprintf(text, sizeof text, "%s", lv_textarea_get_text(s_input));
    ui_menu_input_cb submit = s_submit;
    if (!submit || !submit(text)) {
        if (s_input_error) lv_label_set_text(s_input_error, "Invalid input or busy.\nCheck and retry.");
    }
    memset(text, 0, sizeof text);
}

static void on_visibility(lv_event_t *e)
{
    (void)e;
    bool hidden = lv_textarea_get_password_mode(s_input);
    lv_textarea_set_password_mode(s_input, !hidden);
    lv_label_set_text(s_visibility_label, hidden ? "Hide Password" : "Show Password");
}

static void set_key_page(int page)
{
    s_key_page = page;
    const char *const *maps[] = { k_lower, k_upper, k_symbols, k_more };
    lv_buttonmatrix_set_map(s_keyboard, maps[page]);
    lv_buttonmatrix_set_button_ctrl_all(s_keyboard, LV_BUTTONMATRIX_CTRL_CLICK_TRIG);
}

static void input_key(const char *key)
{
    if (!strcmp(key, "Aa")) set_key_page(s_key_page == 1 ? 0 : 1);
    else if (!strcmp(key, "123")) set_key_page(2);
    else if (!strcmp(key, "ABC")) set_key_page(0);
    else if (!strcmp(key, "More")) set_key_page(3);
    else if (!strcmp(key, LV_SYMBOL_BACKSPACE)) lv_textarea_delete_char(s_input);
    else lv_textarea_add_text(s_input, !strcmp(key, "Space") ? " " : key);
}

static void on_key(lv_event_t *e)
{
    (void)e;
    uint32_t selected = lv_buttonmatrix_get_selected_button(s_keyboard);
    if (selected == LV_BUTTONMATRIX_BUTTON_NONE) return;
    /* Changing the map invalidates its text pointer, so consume it once. */
    input_key(lv_buttonmatrix_get_button_text(s_keyboard, selected));
}

void ui_menu_open_input(const char *title, const char *initial, bool password,
                       uint32_t max_length, ui_menu_input_cb submit)
{
    open_panel(title);
    s_submit = submit;
    s_password = password;
    int w = MENU_WIDTH;
    lv_obj_t *heading = lv_obj_get_child(s_menu, 1);
    lv_obj_set_width(heading, w - 232);
    button(s_menu, w - 108, 12, 96, 56, "Done", on_input_submit, NULL);
    s_input = lv_textarea_create(s_menu);
    lv_obj_set_pos(s_input, 12, 72);
    lv_obj_set_size(s_input, w - 24, 40);
    lv_obj_set_style_text_font(s_input, &lv_font_montserrat_18, 0);
    lv_obj_set_style_bg_color(s_input, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_text_color(s_input, lv_color_hex(COL_TEXT), 0);
    ui_style_card(s_input, COL_ACCENT, false);
    lv_textarea_set_one_line(s_input, true);
    lv_textarea_set_password_show_time(s_input, 0);
    lv_textarea_set_password_mode(s_input, password);
    lv_textarea_set_max_length(s_input, max_length);
    lv_textarea_set_text(s_input, initial ? initial : "");

    if (password) {
        s_visibility_button = button(s_menu, 12, 116, 152, 40, NULL, on_visibility, NULL);
        s_visibility_label = label(s_visibility_button, "Show Password", &lv_font_montserrat_14);
        lv_obj_center(s_visibility_label);
    }
    s_input_error = label(s_menu, password ? "8-63 Characters;\nBlank for Open WiFi" :
        "Aa: case   123: symbols\nTouch text to move cursor", &lv_font_montserrat_12);
    lv_obj_set_pos(s_input_error, password ? 176 : 12, 120);
    lv_obj_set_width(s_input_error, password ? w - 188 : w - 24);
    s_keyboard = lv_buttonmatrix_create(s_menu);
    lv_obj_remove_style_all(s_keyboard);
    lv_obj_set_pos(s_keyboard, 12, 160);
    lv_obj_set_size(s_keyboard, w - 24, 228);
    lv_obj_set_style_pad_row(s_keyboard, 4, 0);
    lv_obj_set_style_pad_column(s_keyboard, 4, 0);
    lv_obj_set_style_bg_color(s_keyboard, lv_color_hex(COL_CARD), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_keyboard, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_radius(s_keyboard, UI_PIXEL_RADIUS, LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_keyboard, 2, LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_keyboard, lv_color_hex(0x76b9ad), LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_keyboard, lv_color_hex(COL_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_text_font(s_keyboard, &lv_font_montserrat_18, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_keyboard, lv_color_hex(0x335568),
                               LV_PART_ITEMS | LV_STATE_PRESSED);
    set_key_page(0);
    lv_obj_add_event_cb(s_keyboard, on_key, LV_EVENT_VALUE_CHANGED, NULL);
}

static int s_test_pick;
static void test_pick(uint8_t id) { s_test_pick = id; }
static int s_test_rewards;
static uint8_t s_test_score;
static uint32_t test_reward(uint8_t score)
{
    s_test_rewards++;
    s_test_score = score;
    return 6 + score * 4;
}

static bool test_minigames(void)
{
    bool ok = true;
    for (uint8_t kind = 0; kind < 3; kind++) {
        s_test_rewards = 0;
        play_open(kind, test_reward);
        lv_obj_update_layout(s_menu);
        ok = ok && lv_obj_get_width(s_play_tiles[0]) >= 180 &&
             lv_obj_get_height(s_play_tiles[0]) >= 100 &&
             lv_obj_get_style_radius(s_play_tiles[0], 0) == UI_PIXEL_RADIUS &&
             lv_obj_get_style_bg_grad_dir(s_play_tiles[0], 0) == LV_GRAD_DIR_NONE &&
             lv_obj_get_style_shadow_width(s_play_tiles[0], 0) == 0 &&
             lv_obj_get_style_radius(s_play_bar, 0) == 0 &&
             lv_obj_get_style_radius(s_play_bar, LV_PART_INDICATOR) == 0;
        for (int round = 0; round < PLAY_ROUNDS; round++) {
            ok = ok && s_play_duration == (kind == 2 ? 620u - round * 45u : 2000u - round * 110u);
            if (kind == 1) {
                ok = ok && s_play_length == 1 + round / 3;
                lv_obj_send_event(s_play_tiles[s_play_answer], LV_EVENT_CLICKED, NULL);
                ok = ok && s_play_round == round;
                for (int i = 0; i < s_play_length; i++) {
                    s_play_deadline = lv_tick_get();
                    play_tick(s_play_timer);
                }
                ok = ok && !s_play_preview;
            }
            if (kind == 0) {
                ok = ok && lv_image_get_src(s_play_target) ==
                    assets_image(sprite_prey_index(s_play_bug, false, false, 0));
                for (int tile = 0; tile < 4; tile++)
                    ok = ok && lv_image_get_src(s_play_images[tile]) ==
                        assets_image(sprite_prey_index(s_play_bugs[tile], false, false, 0));
                int old = s_play_answer;
                s_play_shift_at = lv_tick_get();
                play_tick(s_play_timer);
                ok = ok && s_play_answer != old;
            }
            if (kind == 2 && round != 0) {
                for (int i = 0; i < 3 && !s_play_ready; i++) {
                    s_play_deadline = lv_tick_get();
                    play_tick(s_play_timer);
                }
                ok = ok && s_play_ready;
            }
            int answer = kind == 2 ? 0 : s_play_answer;
            if (round == 0 && kind != 2) answer = (answer + 1) % 4;
            lv_obj_send_event(s_play_tiles[answer], LV_EVENT_CLICKED, NULL);
            if (kind == 1 && round != 0) {
                for (int i = 1; i < s_play_length; i++) {
                    uint8_t step = s_play_step;
                    s_play_input_guard = lv_tick_get() + 1000;
                    lv_obj_send_event(s_play_tiles[s_play_answer], LV_EVENT_CLICKED, NULL);
                    ok = ok && s_play_step == step && !s_play_pending;
                    s_play_input_guard = 0;
                    lv_obj_send_event(s_play_tiles[s_play_answer], LV_EVENT_CLICKED, NULL);
                }
            }
            if (round < PLAY_ROUNDS - 1) {
                ok = ok && s_play_pending;
                lv_obj_send_event(s_play_tiles[answer], LV_EVENT_CLICKED, NULL);
                ok = ok && s_play_round == round + 1;
                s_play_deadline = lv_tick_get();
                play_tick(s_play_timer);
            }
        }
        ok = ok && !s_play_timer && s_test_rewards == 1 && s_test_score == PLAY_ROUNDS - 1;
        ESP_LOGI("spidarium.menu", "minigame test kind=%u score=%u rewards=%d ok=%d",
                 kind, s_test_score, s_test_rewards, ok);
        lv_obj_send_event(s_backdrop, LV_EVENT_CLICKED, NULL);
        ok = ok && ui_menu_visible() && s_test_rewards == 1;
        ui_menu_close();
        s_test_rewards = 0;
        play_open(kind, test_reward);
        for (int i = 0; i < 5 && !s_play_pending; i++) {
            s_play_deadline = lv_tick_get();
            play_tick(s_play_timer);
        }
        ok = ok && s_play_pending && s_play_score == 0;
        lv_obj_send_event(lv_obj_get_child(s_menu, 0), LV_EVENT_CLICKED, NULL);
        ok = ok && !s_play_timer && !ui_menu_visible() && s_test_rewards == 0;
    }
    int previous = -1;
    for (int i = 0; i < 12; i++) {
        ui_menu_open_minigame(NULL);
        ok = ok && s_play_kind != previous;
        previous = s_play_kind;
    }
    ui_menu_close();
    return ok;
}

static void test_key(const char *text)
{
    for (uint32_t index = 0; ; index++) {
        const char *key = lv_buttonmatrix_get_button_text(s_keyboard, index);
        if (!key) return;
        if (strcmp(key, text)) continue;
        lv_buttonmatrix_set_selected_button(s_keyboard, index);
        lv_obj_send_event(s_keyboard, LV_EVENT_VALUE_CHANGED, NULL);
        return;
    }
}

bool ui_menu_test(void)
{
    static const ui_menu_item_t items[] = {
        {"One",NULL,true,false,false,0}, {"Two",NULL,true,false,false,1},
        {"Three",NULL,true,false,false,2}, {"Four",NULL,true,false,false,3},
        {"Five",NULL,true,false,false,4}, {"Six",NULL,true,false,false,5},
        {"Seven",NULL,true,false,false,6}, {"Eight",NULL,true,false,false,7},
        {"Nine",NULL,true,false,false,8}
    };
    ui_menu_open("Test pages", items, 9, test_pick);
    lv_obj_update_layout(s_menu);
    bool targets_ok = lv_obj_get_height(s_row[0]) >= 76 &&
        lv_obj_get_width(lv_obj_get_child(s_menu, 0)) >= 96 &&
        lv_obj_get_height(s_next) >= 56;
    targets_ok = targets_ok &&
        lv_obj_get_x(lv_obj_get_child(s_menu, 2)) >= 120 &&
        !lv_obj_is_hidden(s_icons[0]) &&
        lv_obj_get_style_radius(s_menu, 0) == UI_PIXEL_RADIUS &&
        lv_obj_get_style_radius(s_row[0], 0) == UI_PIXEL_RADIUS &&
        lv_obj_get_style_shadow_width(s_menu, 0) == 0 &&
        lv_obj_get_style_bg_grad_dir(s_menu, 0) == LV_GRAD_DIR_NONE &&
        lv_obj_get_style_bg_grad_dir(s_row[0], 0) == LV_GRAD_DIR_NONE &&
        lv_obj_get_style_border_opa(s_row[0], 0) == LV_OPA_COVER;
    targets_ok = targets_ok && lv_obj_has_state(s_prev, LV_STATE_DISABLED) &&
        lv_color_eq(lv_obj_get_style_bg_color(s_prev, 0), lv_color_hex(0x1b2530)) &&
        !lv_obj_has_state(s_next, LV_STATE_DISABLED);
    lv_obj_send_event(s_next, LV_EVENT_CLICKED, NULL);
    bool ok = targets_ok && s_page == 1 && !strcmp(lv_label_get_text(s_label[0]), "Four");
    lv_obj_send_event(s_next, LV_EVENT_CLICKED, NULL);
    ok = ok && s_page == 2 && !strcmp(lv_label_get_text(s_label[2]), "Nine");
    ok = ok && lv_obj_has_state(s_next, LV_STATE_DISABLED) &&
        lv_color_eq(lv_obj_get_style_text_color(lv_obj_get_child(s_next, 0), 0),
                    lv_color_hex(0x64717b));
    s_test_pick = -1;
    lv_obj_send_event(s_row[0], LV_EVENT_CLICKED, NULL);
    ok = ok && s_test_pick == 6 && !ui_menu_visible();
    ui_menu_open("Test pages", items, 9, NULL);
    lv_obj_send_event(s_backdrop, LV_EVENT_CLICKED, NULL);
    lv_obj_send_event(s_menu, LV_EVENT_CLICKED, NULL);
    ok = ok && ui_menu_visible() && s_page == 0;
    lv_obj_send_event(s_next, LV_EVENT_CLICKED, NULL);
    lv_obj_send_event(s_next, LV_EVENT_CLICKED, NULL);
    lv_obj_send_event(s_prev, LV_EVENT_CLICKED, NULL);
    ok = ok && s_page == 1 && !lv_obj_is_scrollable(s_menu);
    lv_obj_send_event(lv_obj_get_child(s_menu, 0), LV_EVENT_CLICKED, NULL);
    ok = ok && !ui_menu_visible();
    static const ui_menu_item_t disabled[] = {{"Read Only", NULL, false, false, false, 0}};
    ui_menu_open("Test Disabled", disabled, 1, NULL);
    ui_menu_set_item_checkbox(0);
    ok = ok && !lv_obj_is_hidden(s_checks[0]) &&
         !lv_obj_has_state(s_checks[0], LV_STATE_CHECKED);
    ui_menu_set_destructive(0);
    ok = ok && lv_color_eq(lv_obj_get_style_text_color(s_label[0], 0), lv_color_hex(0xff7777));
    ok = ok && !lv_label_get_text(s_marker[0])[0] && lv_obj_is_hidden(s_detail[0]);
    lv_obj_send_event(s_row[0], LV_EVENT_CLICKED, NULL);
    ok = ok && ui_menu_visible();
    static const ui_menu_item_t gift_detail[] = {
        {"Royal Perch", "Level 40 / 172 Silk\nA Habitat Care Bonus\nSpider Visits This Item",
         false, false, false, 0}
    };
    ui_menu_open("Test Gift Details", gift_detail, 1, NULL);
    lv_obj_update_layout(s_menu);
    lv_point_t last_letter;
    lv_label_get_letter_pos(s_detail[0], strlen(lv_label_get_text(s_detail[0])) - 1, &last_letter);
    ok = ok && lv_obj_get_height(s_detail[0]) >= 3 * lv_font_montserrat_12.line_height &&
         last_letter.y + lv_font_montserrat_12.line_height <= lv_obj_get_height(s_detail[0]) &&
         lv_obj_get_y(s_detail[0]) + lv_obj_get_height(s_detail[0]) <=
         lv_obj_get_content_height(s_row[0]);
    ui_menu_open_input("Test password", "", true, 63, NULL);
    ok = ok && s_password && lv_textarea_get_password_mode(s_input);
    ok = ok && lv_obj_get_style_radius(s_input, 0) == UI_PIXEL_RADIUS &&
        lv_obj_get_style_radius(s_keyboard, LV_PART_ITEMS) == UI_PIXEL_RADIUS;
    ok = ok && !strcmp(lv_buttonmatrix_get_button_text(s_keyboard, 0), "q") &&
        !strcmp(lv_buttonmatrix_get_button_text(s_keyboard, 9), "p");
    test_key("a"); test_key("Aa"); test_key("B"); test_key("123");
    test_key("9"); test_key("More"); test_key("\\"); test_key("Space"); test_key(LV_SYMBOL_BACKSPACE);
    ok = ok && !strcmp(lv_textarea_get_text(s_input), "aB9\\");
    lv_obj_send_event(s_visibility_button, LV_EVENT_CLICKED, NULL);
    ok = ok && !lv_textarea_get_password_mode(s_input);
    lv_obj_send_event(s_visibility_button, LV_EVENT_CLICKED, NULL);
    ok = ok && lv_textarea_get_password_mode(s_input);
    for (int i = 0; i < 70; i++) input_key("x");
    ok = ok && strlen(lv_textarea_get_text(s_input)) == 63;
    bool seen[95] = {0};
    seen[0] = true; /* Space is a named key. */
    const char *const *maps[] = {k_lower, k_upper, k_symbols, k_more};
    for (int p = 0; p < 4; p++)
        for (int i = 0; maps[p][i][0]; i++)
            if (strlen(maps[p][i]) == 1 && maps[p][i][0] >= ' ' && maps[p][i][0] <= '~')
                seen[(unsigned char)maps[p][i][0] - ' '] = true;
    for (int i = 0; i < 95; i++) ok = ok && seen[i];
    ok = ok && lv_obj_get_width(s_keyboard) >= 376 && lv_obj_get_height(s_keyboard) >= 228;
    ui_menu_close();
    ESP_LOGI("spidarium.menu", "menu targets/pages/keyboard before games: %d", ok);
    ok = test_minigames() && ok;
    ESP_LOGI("spidarium.menu", "larger targets, red action, minigames: %s", ok ? "PASS" : "FAIL");
    return ok;
}
