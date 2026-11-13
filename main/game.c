/*
 * Spidarium — game orchestrator.
 *
 * Owns the authoritative state and the two timers. All actual work lives in
 * the engine modules: terrarium (background), spider (rendering), behaviour
 * (movement/FSM), prey (hunting), needs (care/life cycle), ui_hud
 * (screens), audio (SFX), save (persistence).
 */
#include "game.h"
#include "achievements.h"
#include "help_content.h"

#include "assets.h"
#include "audio.h"
#include "behaviour.h"
#include "buttons.h"
#include "cspider.h"
#include "decor.h"
#include "game_config.h"
#include "gift.h"
#include "power.h"
#include "sprites.h"
#include "meminfo.h"
#include "mind.h"
#include "needs.h"
#include "net.h"
#include "prey.h"
#include "rtc.h"
#include "save.h"
#include "species.h"
#include "sd.h"
#include "spider.h"
#include "test_console.h"
#include "terrarium.h"
#include "theme.h"
#include "ui_hud.h"
#include "ui_menu.h"
#include "ui_setup.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_app_desc.h"
#include "esp_lv_adapter.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

static const char *TAG = "spidarium";

/* The single authoritative state. */
game_state_t g;
game_ui_t    ui;
playfield_t  g_play;
uint8_t      g_decor_mode;
uint8_t      g_decor_selected;
int          g_decor_move_slot = -1;
static bool s_gift_moving;
static bool s_testing_placement;

static uint32_t s_last_logic;
static uint32_t s_last_save;
static bool game_save_screenshot(void);
static bool s_screenshot_failed;
static lv_draw_buf_t *s_sd_capture;
static const char *s_sd_message;

/* The two game timers, kept so they can be paused.
 *
 * WiFi work (init, scan, connect) must run as the ONLY thing happening. The
 * radio allocates internal DMA-capable RAM and drives its own high-priority
 * tasks, and running that concurrently with the 20 Hz logic tick and the 30 Hz
 * render tick starved the LCD SPI driver and crashed the device. Pausing both
 * timers while the radio is busy is the same approach used previously for WiFi
 * and OTA: stop the game, do the network thing, resume. */
static lv_timer_t *s_logic_timer;
static lv_timer_t *s_render_timer;
static bool        s_paused;

/* Defined once here. Declared extern in game_config.h so every translation
 * unit shares this single copy. */
const temperament_t k_temperaments[TEMPERAMENT_COUNT] = {
    /* name      aim    catch  speed  hunger  sleep */
    { "bold",   0.85f, 1.10f, 1.10f, 1.00f, 15 },
    { "timid",  1.25f, 0.95f, 0.90f, 0.90f, 20 },
    { "greedy", 1.00f, 1.05f, 1.00f, 1.25f, 15 },
    { "lazy",   1.15f, 0.90f, 0.80f, 0.85f, 30 },
};

const temperament_t *game_temperament(void)
{
    uint8_t t = g.temperament;
    if (t >= TEMPERAMENT_COUNT) t = 0;
    return &k_temperaments[t];
}

/* ------------------------------------------------------------------ */
/* Shared helpers                                                      */
/* ------------------------------------------------------------------ */
uint32_t game_now(void)
{
    return lv_tick_get();
}

void game_add_silk(uint32_t amount)
{
    g.silk = amount > UINT32_MAX - g.silk ? UINT32_MAX : g.silk + amount;
}
void game_add_xp(uint32_t amount)
{
    g.xp += amount;

    uint32_t level = g.xp / XP_PER_LEVEL + 1;
    if (level != g.level) {
        g.level = level;
        ESP_LOGI(TAG, "level up -> %lu", (unsigned long)level);
        if (level >= 5) game_award(ACH_LEVEL_5);
    }
}

void game_award(uint32_t achievement)
{
    if (g.achievements & achievement) return;

    /* Set the bit BEFORE granting XP. game_add_xp() can itself call
     * game_award() (level 5), and this ordering is what stops that from
     * recursing. */
    g.achievements |= achievement;
    ESP_LOGI(TAG, "achievement 0x%08lx", (unsigned long)achievement);

    /* Achievements used to be inert flags. They now pay XP, so progression
     * has teeth. */
    game_add_xp(XP_ACHIEVEMENT);
    game_add_silk(ACHIEVEMENT_SILK);
    ESP_LOGI(TAG, "achievement reward +%u XP +%u Silk", XP_ACHIEVEMENT, ACHIEVEMENT_SILK);
}

void game_check_achievements(void)
{
    uint32_t before = g.achievements;
    if (g.cleans >= 10) game_award(ACH_TEN_CLEANS);
    if (g.plays >= 10) game_award(ACH_TEN_PLAYS);
    if (g.meals >= 25) game_award(ACH_TWENTY_FIVE_MEALS);
    if (g.bond >= 50) game_award(ACH_BOND_50);
    if (g.daily_streak >= 3) game_award(ACH_STREAK_3);
    if (decor_placed_count() >= 3) game_award(ACH_DECOR_3);
    int backgrounds = 0;
    for (int i = 0; i < THEME_COUNT; i++) {
        if (theme_owned(i)) backgrounds++;
        if (border_owned(i)) game_award(ACH_FIRST_BORDER);
    }
    if (backgrounds >= 4) game_award(ACH_BACKGROUNDS_4);
    if (g.gift.used) game_award(ACH_FIRST_GIFT);
    if (g.achievements != before) game_save_now();
}

bool game_save_now(void)
{
    if (s_testing_placement) return true;
    bool accepted = save_request(&g, sizeof g);
    if (!accepted) ESP_LOGE(TAG, "game save not queued");
    return accepted;
}

/* ------------------------------------------------------------------ */
/* Persistence                                                         */
/* ------------------------------------------------------------------ */
static void state_defaults(void)
{
    memset(&g, 0, sizeof g);
    g.hunger  = 65;
    g.mood    = 60;
    g.energy  = 80;
    g.hygiene = 80;
    g.seed    = 0xC0FFEE;
    g.state   = SPIDER_IDLE;
    g.stage   = STAGE_SPIDERLING;
    g.level   = 1;
    g.muted   = 0;
    g.silk = 20;
    g.brightness = 50;
    g.volume = AUDIO_VOLUME;
    g.backgrounds_owned = 1u;
    g.border_id = 255;
    g.screen_timeout = 3;
    g.music_enabled = 1;
    g.music_volume = 100;

    /* Sentinel: rolled in game_init() AFTER srand(). Rolling here would give
     * every fresh boot the same temperament, because this runs before the RNG
     * is seeded. */
    g.temperament = 0xFF;
    g.bond        = 0;

    /* Accent defaults, chosen so a fresh spider looks exactly like the old
     * hardcoded one: black pupils, white fangs, grey legs. */
    g.pupil_colour = 0;   /* black */
    g.fang_colour  = 1;   /* white */
    g.leg_colour   = 2;   /* grey  */

    /* Sentinel: 0 is a real in-game day, so "never claimed" needs a value that
     * cannot collide with it. Without this a brand-new pet could never claim
     * its first check-in, because daily_day == 0 == today. */
    g.daily_day = DAILY_NEVER;
    g.daily_base_day = DAILY_NEVER;

    /* Every decor slot starts empty. kind 0 is a real item, so a zeroed struct
     * would otherwise place twelve small rocks on a brand-new pet. */
    decor_clear();
}

static void state_finish_load(void)
{
    /* Runtime-only fields never survive a reboot. */
    g.state        = SPIDER_IDLE;
    g.eat_until_ms = 0;
    g.blink_until  = 0;
    g.blink_window = 0;
    g.gift_mode    = 0;
    g_decor_mode   = 0;

    /* Guard against a corrupt-but-valid-header blob. */
    if (g.level == 0) g.level = 1;
    if (g.brightness < 10 || g.brightness > 100) g.brightness = 50;
    if (g.volume > 100) g.volume = AUDIO_VOLUME;
    if (g.power_mode > 2) g.power_mode = 0;
    if (g.screen_timeout > 4) g.screen_timeout = 3;
    if (g.deep_sleep_after > 4) g.deep_sleep_after = 0;
    if (g.rtc_wake > 2) g.rtc_wake = 0;
    theme_restore();
    if (g.gift.used && g.gift.kind < GIFT_KINDS)
        gift_mark_owned(g.gift.kind);
    if (g.stage > STAGE_ADULT) g.stage = STAGE_SPIDERLING;
    if (g.hunger > 100) g.hunger = 100;
    if (g.mood > 100) g.mood = 100;
    if (g.energy > 100) g.energy = 100;
    if (g.hygiene > 100) g.hygiene = 100;

    /* Drop any decor slot whose kind is out of range, so a corrupt blob cannot
     * index past the definition table. */
    for (int i = 0; i < DECOR_SLOTS; i++)
        if (g.decor[i].kind != 0xFF && g.decor[i].kind >= DECOR_COUNT)
            g.decor[i].kind = 0xFF;
}

static void state_load(void)
{
    state_defaults();
    if (!save_load(&g, sizeof g)) ESP_LOGI(TAG, "starting from defaults");
    state_finish_load();

    /* Catch up on the time the device was off. The RTC is the clock of record;
     * without it there is no trustworthy elapsed time, so nothing is applied
     * rather than inventing a delta from a 1970 epoch. */
    time_t now = 0;
    if (pet_rtc_read(&now) == ESP_OK && now > 0) {
        if (g.last_seen > 0 && (uint32_t)now > g.last_seen) {
            needs_apply_offline((uint32_t)now - g.last_seen);
        }
        g.last_seen = (uint32_t)now;
    }
}

/* ------------------------------------------------------------------ */
/* Playfield                                                           */
/* ------------------------------------------------------------------ */
static void keep_gift_in_playfield(void)
{
    if (!g.gift.used) return;
    if (g.gift.x < g_play.left) g.gift.x = g_play.left;
    if (g.gift.x > g_play.right) g.gift.x = g_play.right;
    if (g.gift.y < g_play.top) g.gift.y = g_play.top;
    if (g.gift.y > g_play.bottom) g.gift.y = g_play.bottom;
}

static void compute_playfield(void)
{
    /* Only the top-left Care button stays visible; hunting uses the full field. */
    g_play.left   = PLAY_MARGIN;
    g_play.right  = ui.W - PLAY_MARGIN;
    g_play.top    = PLAY_MARGIN;
    g_play.bottom = ui.H - PLAY_MARGIN;

    if (g_play.right - g_play.left < 60) {
        g_play.left = 10;
        g_play.right = ui.W - 10;
    }
    if (g_play.bottom - g_play.top < 60) {
        g_play.top = ui.H / 3;
        g_play.bottom = ui.H * 2 / 3;
    }

    /* Park the spider inside the field. */
    if (g.spider_x < g_play.left || g.spider_x > g_play.right ||
        g.spider_y < g_play.top  || g.spider_y > g_play.bottom) {
        g.spider_x = (int16_t)((g_play.left + g_play.right) / 2);
        g.spider_y = (int16_t)((g_play.top + g_play.bottom) / 2);
    }
    keep_gift_in_playfield();
}

/* ------------------------------------------------------------------ */
/* Event callbacks                                                     */
/* ------------------------------------------------------------------ */
void game_on_feed(lv_event_t *e)
{
    (void)e;
    if (prey_count() >= MAX_PREY) {
        ESP_LOGW(TAG, "feed rejected: tank full");
        ui_hud_bubble("Tank Full / Catch a Bug First", 0xf5c86b, 1800);
        return;
    }
    g.taps++;
    needs_feed();
    /* No bubble: the spider reacting IS the feedback. */
}

void game_on_clean(lv_event_t *e)
{
    (void)e;
    /* Clean always works. The reward scales with how much the tank improved, so
     * a spotless tank pays nothing and a filthy one pays the full amount. */
    int prey_before = prey_count();
    bool needed = needs_clean_needed();
    uint32_t xp = needs_clean();
    if (!needed) {
        ui_hud_bubble("Already Clean", 0xf5c86b, 1800);
        return;
    }
    /* A tank holding only bugs is cleaned for 0 XP, so say what actually
     * happened rather than showing a reward that was not paid. */
    if (xp == 0 && prey_before > 0 && prey_count() < prey_before) {
        ui_hud_bubble("Bugs Shooed Out", 0xf5c86b, 1800);
    }
    g.taps++;
    game_save_now();
}

static uint32_t play_reward(uint8_t score)
{
    uint32_t before = g.xp;
    needs_play();
    /* Score drives the payout: a clean sweep is worth roughly four times a
     * blank run, so playing well is visibly better than tapping through. */
    game_add_xp(2 + score * 4);
    game_add_silk(score * 3);
    game_save_now();
    return g.xp - before;
}

void game_on_play(lv_event_t *e)
{
    (void)e;
    g.taps++;
    behaviour_aim_cancel();
    ui_menu_open_minigame(play_reward);
}

void game_on_sleep(lv_event_t *e)
{
    (void)e;
    g.taps++;
    needs_sleep();
}

static int16_t s_place_x, s_place_y, s_place_dx, s_place_dy;
static bool s_place_dragging;
static uint8_t s_place_blocking;
static int s_place_layer;
static void placement_position(int32_t x, int32_t y);

uint8_t game_placement_blocking(void) { return s_place_blocking; }
int game_placement_layer(void) { return s_place_layer; }
int game_placement_layers(void) { return decor_layer_count(s_place_blocking, g_decor_move_slot) + 1; }
bool game_placement_existing(void)
{
    return (g_decor_mode && g_decor_move_slot >= 0) || (g.gift_mode && s_gift_moving);
}

void game_on_placement_blocking(lv_event_t *e)
{
    (void)e;
    if (!g_decor_mode) return;
    uint8_t previous = s_place_blocking;
    s_place_blocking = (s_place_blocking + 1) % DECOR_PATH_COUNT;
    if ((previous == DECOR_WALK_BEHIND) != (s_place_blocking == DECOR_WALK_BEHIND))
        s_place_layer = decor_layer_count(s_place_blocking, g_decor_move_slot);
    placement_position(s_place_x, s_place_y);
    ui_hud_set_place_mode(true, NULL);
}

void game_on_placement_path(lv_event_t *e)
{
    if (!g_decor_mode) return;
    uint8_t path = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (path == s_place_blocking) return;
    if ((path == DECOR_WALK_BEHIND) != (s_place_blocking == DECOR_WALK_BEHIND))
        s_place_layer = decor_layer_count(path, g_decor_move_slot);
    s_place_blocking = path;
    placement_position(s_place_x, s_place_y);
    ui_hud_set_place_mode(true, NULL);
}

void game_on_placement_layer(lv_event_t *e)
{
    if (!g_decor_mode) return;
    int step = (int)(intptr_t)lv_event_get_user_data(e);
    int next = s_place_layer + step;
    if (next < 0 || next >= game_placement_layers()) return;
    s_place_layer = next;
    placement_position(s_place_x, s_place_y);
    ui_hud_set_place_mode(true, NULL);
}
void game_on_placement_put_away(lv_event_t *e)
{
    (void)e;
    if (g_decor_mode && g_decor_move_slot >= 0) {
        if (!decor_remove(g_decor_move_slot)) {
            ESP_LOGW(TAG, "put away rejected: decor missing");
            ui_hud_bubble("Cannot Put Away", 0xf5c86b, 1800);
            return;
        }
        terrarium_refresh_decor();
    } else if (g.gift_mode && s_gift_moving) {
        g.gift.used = 0;
        terrarium_refresh_gift();
    } else return;
    game_on_placement_cancel(NULL);
    if (!game_save_now()) ui_hud_bubble("Save Not Queued / See Log", 0xf5c86b, 2200);
}

static void placement_position(int32_t x, int32_t y)
{
    uint8_t kind = g.gift_mode ? g.gift_selected : g_decor_selected;
    int radius = g.gift_mode ? 0 : decor_def(kind)->radius;
    int left = g_play.left + radius, right = g_play.right - radius;
    int top = g_play.top + radius, bottom = g_play.bottom - radius;
    s_place_x = x < left ? left : x > right ? right : x;
    s_place_y = y < top ? top : y > bottom ? bottom : y;
    terrarium_preview_item(g.gift_mode != 0, kind, g_decor_mode ? g_decor_move_slot : -1,
                           s_place_x, s_place_y, s_place_blocking, s_place_layer);
}

void game_on_placement_cancel(lv_event_t *e)
{
    (void)e;
    g.gift_mode = 0;
    g_decor_mode = 0;
    g_decor_move_slot = -1;
    s_gift_moving = false;
    s_place_dragging = false;
    terrarium_preview_hide();
    ui_hud_set_place_mode(false, NULL);
}

void game_on_placement_done(lv_event_t *e)
{
    (void)e;
    if (!g.gift_mode && !g_decor_mode) return;
    if (g_decor_mode) {
        int slot = g_decor_move_slot;
        bool ok;
        if (slot >= 0) ok = decor_move(slot, s_place_x, s_place_y);
        else { slot = decor_place(g_decor_selected, s_place_x, s_place_y); ok = slot >= 0; }
        if (!ok) {
            ESP_LOGW(TAG, "decor placement commit failed");
            ui_hud_bubble("Cannot Place / See Log", 0xf5c86b, 1800);
            return;
        }
        if (!decor_set_path(slot, s_place_blocking) || decor_set_layer(slot, s_place_layer) < 0) {
            ESP_LOGE(TAG, "placement path/layer commit failed");
            ui_hud_bubble("Cannot Set Layer / See Log", 0xf5c86b, 1800);
            return;
        }
        terrarium_preview_hide();
        terrarium_refresh_decor();
    } else {
        if (!s_gift_moving && !gift_purchase(g.gift_selected)) {
            ui_hud_bubble("Not Enough Silk", 0xf5c86b, 1800);
            return;
        }
        if (!s_gift_moving) {
            g.gift.kind = g.gift_selected;
            g.gift.variant = (uint8_t)(rand() % 3);
        }
        g.gift.used = 1;
        g.gift.x = s_place_x;
        g.gift.y = s_place_y;
        terrarium_refresh_gift();
    }
    game_on_placement_cancel(NULL);
    if (!game_save_now()) ui_hud_bubble("Save Not Queued / See Log", 0xf5c86b, 2200);
}

void game_on_gift(lv_event_t *e)
{
    (void)e;
    g.taps++;

    /* Pressing GIFT opens the picker. Choosing a kind there closes the menu and
     * enters placement mode, so the player picks WHAT to give before deciding
     * WHERE. The old version cycled kinds blindly on repeated presses, which
     * meant you could not see the options or go back to one you had passed. */
    if (g.gift_mode) {
        /* Cancel placement. */
        game_on_placement_cancel(NULL);
        return;
    }

    if (ui_hud_gift_menu_visible()) {
        ui_hud_gift_menu_hide();
        return;
    }

    ui_hud_gift_menu_show();
}

/* Cancel decor placement from the corner X. The item stays owned, so the
 * player can place it later from the Decor menu. */
void game_cancel_decor_placement(void)
{
    if (!g_decor_mode) return;
    game_on_placement_cancel(NULL);
    ESP_LOGI(TAG, "decor placement cancelled");
}

/* Called by the HUD when the player taps a kind in the picker. */
void game_gift_chosen(uint8_t kind)
{
    s_gift_moving = false;
    if (kind >= GIFT_KINDS) return;

    if (!gift_can_place(kind)) {
        ESP_LOGW(TAG, "gift selection rejected: locked or unaffordable");
        ui_hud_bubble("Earn Silk by Playing", 0xf5c86b, 1800);
        return;
    }

    g.gift_selected = kind;
    ui_hud_gift_menu_hide();
    ui_hud_set_chrome(false);

    g.gift_mode = 1;
    behaviour_aim_cancel();
    s_place_dragging = false;
    placement_position(ui.W / 2, ui.H / 2);
    ui_hud_set_gift_mode(true);

}

/* ------------------------------------------------------------------ */
/* Menu system                                                         */
/* ------------------------------------------------------------------ */
/* A small hierarchy: the main menu lists categories, and each category opens
 * its own list. This is where the action buttons and stats will eventually
 * move, so the structure matters more than the current contents. */

enum {
    MENU_MAIN = 0,
    MENU_THEME,
    MENU_SOUND,
    MENU_ACHIEVEMENTS,
    MENU_RESET,
    MENU_SETTINGS,
    MENU_WIFI,
    MENU_SD,
    MENU_WIFI_SAVED,
    MENU_WIFI_NETWORK,
    MENU_OTA,
    MENU_SPECIES,
    MENU_ABOUT,
    MENU_POWER,
    MENU_DISPLAY,
    MENU_DAILY,
    MENU_SHOP,
    MENU_DECOR,
    MENU_DECOR_ITEM,
    MENU_GIFT_ITEM,
    MENU_POWER_CONFIRM,
    MENU_SOUNDTRACK,
    MENU_BORDERS,
    MENU_WIFI_SCAN,
    MENU_HELP,
};

static void menu_open(int which);
static void back_to_main(void);

/* Which menu is currently on screen, or -1 when none is. Tracked here because
 * ui_menu only knows that *a* menu is open, not which one. */
static int s_open_menu = -1;
/* The row table handed to ui_menu_open(). File scope so a value changed
 * outside the menu (the physical volume buttons) can be pushed into the open
 * panel without rebuilding it. */
static ui_menu_item_t s_menu_items[DECOR_COUNT + 1];
/* Slot whose per-item decor menu is open. */
static int s_decor_item_slot = -1;
static struct { decor_edit_t operation; int value, x, y, count; } s_decor_edit;
static lv_obj_t *s_light_overlay;
static lv_timer_t *s_pet_tap_timer;
static lv_point_t s_pet_tap;
static lv_point_t s_tap_origin, s_last_tap;
static uint32_t s_last_tap_at;
static bool s_tap_valid, s_tap_pending;

#define ENCLOSURE_DOUBLE_TAP_MS 450
#define ENCLOSURE_TAP_SLOP 24
#define ENCLOSURE_DOUBLE_TAP_SLOP 48

static void cancel_enclosure_tap(void)
{
    s_tap_pending = false;
    if (s_pet_tap_timer) { lv_timer_delete(s_pet_tap_timer); s_pet_tap_timer = NULL; }
}

void game_cancel_pending_tap(void)
{
    s_tap_valid = false;
    cancel_enclosure_tap();
}

static void apply_enclosure_lights(void)
{
    if (s_light_overlay) lv_obj_set_hidden(s_light_overlay, !g.lights_dimmed);
}

static void delayed_pet(lv_timer_t *timer)
{
    (void)timer;
    s_pet_tap_timer = NULL;
    if (!ui_menu_visible() && !ui_hud_chrome_visible() && !g.gift_mode && !g_decor_mode &&
        needs_pet(s_pet_tap.x, s_pet_tap.y)) {
        g.taps++;
        game_save_now();
    }
}

static void enclosure_double_tap(void)
{
    cancel_enclosure_tap();
    behaviour_aim_cancel();
    g.lights_dimmed = !g.lights_dimmed;
    apply_enclosure_lights();
    game_save_now();
}
static int s_saved_network;
static char s_join_ssid[33];
static char s_wifi_message[80];

static void back_to_wifi(void) { menu_open(MENU_WIFI); }
static void back_to_settings(void) { menu_open(MENU_SETTINGS); }
static void back_to_sound(void) { menu_open(MENU_SOUND); ui_menu_set_page(1); }
static void back_to_about(void) { menu_open(MENU_ABOUT); }
static void back_to_ota(void) { menu_open(MENU_OTA); }
static uint8_t s_help_topic, s_achievement;
static void back_to_help(void) { menu_open(MENU_HELP); ui_menu_set_page(s_help_topic / 4); }
static void on_help_pick(uint8_t id)
{
    if (id >= HELP_TOPIC_COUNT) { ESP_LOGE(TAG, "invalid help topic %u", id); return; }
    s_help_topic = id;
    ui_menu_open_text(HELP_TOPICS[id].name, HELP_TOPICS[id].text);
    ui_menu_set_back(back_to_help);
}
static void back_to_achievements(void) { menu_open(MENU_ACHIEVEMENTS); ui_menu_set_page(s_achievement / 6); }
static void on_achievement_pick(uint8_t id)
{
    if (id >= ACHIEVEMENT_COUNT) { ESP_LOGE(TAG, "invalid achievement %u", id); return; }
    s_achievement = id;
    char text[256];
    snprintf(text, sizeof text, "%s.\n\nReward: +15 XP and +20 Silk, paid automatically once.\n\n%s",
             ACHIEVEMENTS[id].goal, g.achievements & ACHIEVEMENTS[id].bit ?
             "Earned. Existing earned achievements are not paid twice." : "Keep caring for your spider to unlock this milestone.");
    ui_menu_open_text(ACHIEVEMENTS[id].name, text);
    ui_menu_set_back(back_to_achievements);
}
static void on_about_pick(uint8_t id) { if (id == 3) menu_open(MENU_OTA); }

static bool on_password(const char *password)
{
    if (!net_connect_to(s_join_ssid, password)) return false;
    menu_open(MENU_WIFI);
    return true;
}

static bool on_ssid(const char *ssid)
{
    if (!ssid[0] || strlen(ssid) > 32) return false;
    snprintf(s_join_ssid, sizeof s_join_ssid, "%s", ssid);
    ui_menu_open_input("WiFi Password", "", true, 63, on_password);
    ui_menu_set_back(back_to_wifi);
    return true;
}

static bool on_ota_url(const char *url)
{
    if (!net_ota_start(url)) return false;
    menu_open(MENU_OTA);
    return true;
}

static void on_ota_pick(uint8_t id)
{
    if (id == 1) {
        ui_menu_open_input("Update URL (HTTPS)", "", false, 255, on_ota_url);
        ui_menu_set_back(back_to_ota);
        return;
    }
    if (!net_ota_latest()) ui_hud_bubble("Update Rejected; Save a Network", 0xf5c86b, 2200);
    menu_open(MENU_OTA);
}

static void on_saved_network(uint8_t id)
{
    s_saved_network = id;
    menu_open(MENU_WIFI_NETWORK);
}

static void on_network_action(uint8_t id)
{
    bool accepted = id == 0 ? net_sync_saved(s_saved_network) :
                             net_forget_saved(s_saved_network);
    if (!accepted) snprintf(s_wifi_message, sizeof s_wifi_message, "Request rejected; network missing or busy");
    menu_open(MENU_WIFI);
}

/* Factory reset: wipe the save, put the state back to defaults, and drop the
 * player into the new-tank screen so they can start over. This exists so new
 * tanks can be retested without reflashing the device. */
static void do_factory_reset(void)
{
    if (!save_request_erase()) {
        ESP_LOGE(TAG, "factory reset not queued");
        return;
    }
    state_defaults();
    audio_set_muted(false);

    /* The live objects still carry the old spider's colours, so push the new
     * defaults into them before showing the setup screen. */
    spider_refresh_colours();
    terrarium_rebuild();
    terrarium_refresh_gift();

    ui_setup_show();
}

static void on_theme_pick(uint8_t id)
{
    if (!theme_set(id)) {
        ui_hud_bubble("Not Enough Silk", 0xf5c86b, 1800);
        menu_open(MENU_THEME);
        ui_menu_set_page(id / 6);
        return;
    }
    terrarium_rebuild();
    menu_open(MENU_THEME);   /* stay in the submenu so the choice is visible */
}

static void on_border_pick(uint8_t id)
{
    if (border_set(id)) terrarium_rebuild();
    else ui_hud_bubble("Border Purchase Failed", 0xf5c86b, 1800);
    menu_open(MENU_BORDERS);
    ui_menu_set_page(id == 255 ? 0 : (id + 1) / 6);
}

static void on_sound_pick(uint8_t id)
{
    if (id == 4) { menu_open(MENU_SOUNDTRACK); return; }
    if (id == 1) audio_set_muted(!audio_muted());
    if (id == 2) {
        g.music_enabled = !g.music_enabled;
        audio_set_music(g.music_enabled);
    }
    game_save_now();
    menu_open(MENU_SOUND);
}

static void on_soundtrack_pick(uint8_t id)
{
    if (id >= AUDIO_MUSIC_TRACK_COUNT) {
        ESP_LOGW(TAG, "soundtrack rejected: invalid track %u", id);
        return;
    }
    g.music_track = id;
    audio_set_music_track(id);
    game_save_now();
    menu_open(MENU_SOUNDTRACK);
}

/* Slider rows report live while the menu stays open, so the value is applied
 * and persisted without rebuilding the panel under the player's finger. */
static void on_volume_slider(uint8_t id, uint8_t value)
{
    if (id == 3) {
        g.music_volume = value;
        audio_set_music_volume(value);
    } else audio_set_volume(value);
    game_save_now();
}

/* The physical volume buttons changed the level behind the menu's back. Push
 * the new value into the open Sound panel so its slider and percentage follow
 * immediately instead of waiting for the player to back out and re-enter.
 * Runs on the LVGL task (buttons_tick is called from the logic timer). */
void game_volume_changed(void)
{
    if (s_open_menu != MENU_SOUND || !ui_menu_showing(s_menu_items)) return;
    ui_menu_set_slider_value(0, audio_volume());
}

static void on_brightness_slider(uint8_t id, uint8_t value)
{
    (void)id;
    if (value < 10) value = 10;
    g.brightness = value;
    game_save_now();
}

static void on_main_pick(uint8_t id)
{
    switch (id) {
    case MENU_DAILY:        menu_open(MENU_DAILY);        break;
    case MENU_SHOP:         menu_open(MENU_SHOP);         break;
    case MENU_THEME:        menu_open(MENU_THEME);        break;
    case MENU_SOUND:        menu_open(MENU_SOUND);        break;
    case MENU_ACHIEVEMENTS: menu_open(MENU_ACHIEVEMENTS); break;
    case MENU_RESET:        menu_open(MENU_RESET);        break;
    case MENU_SETTINGS:     menu_open(MENU_SETTINGS);     break;
    case MENU_SPECIES:      menu_open(MENU_SPECIES);      break;
    case MENU_DECOR:        menu_open(MENU_DECOR);        break;
    case MENU_BORDERS:      menu_open(MENU_BORDERS);      break;
    case MENU_HELP:         menu_open(MENU_HELP);         break;
    case MENU_GIFT_ITEM:    menu_open(MENU_GIFT_ITEM);    break;
    default: break;
    }
}

/* Daily page. Row 0 claims the check-in, row 1 claims the task once it is done.
 * Both rows stay visible so the player can see what is on offer even after
 * claiming; the subtitle carries the state. */
static void on_daily_pick(uint8_t id)
{
    if (id == 0) {
        uint32_t xp = needs_daily_claim();
        if (xp == 0) ESP_LOGI(TAG, "daily: nothing to claim");
        game_save_now();
        menu_open(MENU_DAILY);
        return;
    }
    if (id == 1) {
        uint32_t xp = needs_daily_task_claim();
        if (xp == 0) ESP_LOGI(TAG, "daily: task not ready");
        game_save_now();
        menu_open(MENU_DAILY);
        return;
    }
    back_to_main();
}

static void on_shop_pick(uint8_t id)
{
    if (id < TREAT_COUNT) {
        /* A refusal is already logged by treat_buy; the menu re-renders with the
         * unchanged Silk balance so the player sees it did not happen. */
        treat_buy((treat_t)id);
        game_save_now();
        menu_open(MENU_SHOP);
        ui_menu_set_page(id / 6);
        return;
    }
    back_to_main();
}

/* Stage the visible position until the player explicitly commits it. */
static void decor_begin_placement(uint8_t kind)
{
    const decor_def_t *def = decor_def(kind);
    if (!def) return;
    g_decor_selected = kind;
    g_decor_mode = 1;
    g_decor_move_slot = -1;
    ui_menu_close();
    ui_hud_set_chrome(false);
    behaviour_aim_cancel();
    s_place_dragging = false;
    s_place_blocking = def->blocking;
    s_place_layer = decor_layer_count(s_place_blocking, -1);
    placement_position(ui.W / 2, ui.H / 2);
    ui_hud_set_place_mode(true, "Slide to position; Done saves");
}

/* Keep position, path and layer staged until Done. */
static void decor_begin_move(int slot)
{
    if (!decor_at(slot)) return;
    g_decor_move_slot = slot;
    s_place_blocking = decor_at(slot)->blocking;
    s_place_layer = decor_layer_index(slot);
    g_decor_selected = decor_at(slot)->kind;
    g_decor_mode = 1;
    ui_menu_close();
    ui_hud_set_chrome(false);
    behaviour_aim_cancel();
    s_place_dragging = false;
    placement_position(decor_at(slot)->x, decor_at(slot)->y);
    ui_hud_set_place_mode(true, "Slide to move; Done saves");
}

static void on_decor_pick(uint8_t id)
{
    if (id >= DECOR_COUNT) {
        ESP_LOGW(TAG, "decor menu rejected invalid id %u", id);
        menu_open(MENU_DECOR);
        return;
    }
    const decor_def_t *def = decor_def(id);
    if (!def) return;
    if (!decor_unlocked(id)) {
        ui_hud_bubble("Level Too Low", 0xf5c86b, 1800);
        menu_open(MENU_DECOR);
        ui_menu_set_page(id / 6);
        return;
    }
    /* Buying and placing are one action: the player picked the item because
     * they want it in the tank, so a second confirmation is just friction. */
    if (!decor_owned(id) && !decor_buy(id)) {
        ui_hud_bubble("Not Enough Silk", 0xf5c86b, 1800);
        menu_open(MENU_DECOR);
        ui_menu_set_page(id / 6);
        return;
    }
    game_save_now();
    decor_begin_placement(id);
}

static void on_gift_item_pick(uint8_t id)
{
    if (!g.gift.used) { menu_open(MENU_MAIN); return; }
    if (id == 0) {
        s_gift_moving = true;
        g.gift_mode = 1;
        g.gift_selected = g.gift.kind;
        ui_menu_close();
        ui_hud_set_chrome(false);
        behaviour_aim_cancel();
        s_place_dragging = false;
        placement_position(g.gift.x, g.gift.y);
        ui_hud_set_place_mode(true, "Slide to move; Done saves");
    } else {
        g.gift.used = 0;
        terrarium_refresh_gift();
        game_save_now();
        menu_open(MENU_MAIN);
    }
}

/* Settings submenu: connectivity and storage. Both are infrastructure for OTA
 * and backups rather than gameplay, so they live behind their own entry rather
 * than cluttering the main list. */
static void on_settings_pick(uint8_t id)
{
    if (id == 7) {
        int page = ui_menu_page();
        s_screenshot_failed = !game_save_screenshot();
        if (s_screenshot_failed) ESP_LOGW(TAG, "playfield screenshot not queued");
        menu_open(MENU_SETTINGS);
        ui_menu_set_page(page);
        return;
    }
    switch (id) {
    case 0:
        menu_open(MENU_WIFI);
        break;
    case 1:
        s_sd_message = sd_request(SD_JOB_MOUNT) ? NULL : "Card Check Rejected; Try Again";
        if (s_sd_message) ESP_LOGW(TAG, "SD probe not queued");
        menu_open(MENU_SD);
        break;
    case 2: menu_open(MENU_ABOUT); break;
    case 3: menu_open(MENU_POWER); break;
    case 4: menu_open(MENU_DISPLAY); break;
    case 5: menu_open(MENU_SOUND); break;
    case 6: {
        /* One switch drives both halves of unattended play: wild prey keeps
         * arriving and the spider hunts it without the player aiming. */
        int page = ui_menu_page();
        bool on = !g.auto_feed;
        g.auto_feed = on;
        g.auto_hunt = on;
        if (!on) behaviour_aim_cancel();
        game_save_now();
        menu_open(MENU_SETTINGS);
        ui_menu_set_page(page);
        break;
    }
    case 8: menu_open(MENU_RESET); break;
    default: break;
    }
}

static void on_wifi_pick(uint8_t id)
{
    s_wifi_message[0] = 0;
    if (id == 0) {
        if (!net_scan_start())
            snprintf(s_wifi_message, sizeof s_wifi_message, "Scan rejected: worker unavailable or busy");
        menu_open(MENU_WIFI_SCAN);
        return;
    }
    if (id == 1) {
        ui_menu_open_input("Network Name (SSID)", "", false, 32, on_ssid);
        ui_menu_set_back(back_to_wifi);
        return;
    }
    if (id == 2) { menu_open(MENU_WIFI_SAVED); return; }
    int i = (int)id - 4;
    if (i >= 0 && i < net_scan_count()) {
        snprintf(s_join_ssid, sizeof s_join_ssid, "%s", net_scan_ssid(i));
        if (net_scan_secure(i)) {
            ui_menu_open_input("WiFi Password", "", true, 63, on_password);
            ui_menu_set_back(back_to_wifi);
            return;
        }
        if (!net_connect_to(s_join_ssid, ""))
            snprintf(s_wifi_message, sizeof s_wifi_message, "Connection rejected; saved list may be full");
    }
    menu_open(MENU_WIFI);
}

/* SD actions: backup and restore the save. */
static void on_sd_pick(uint8_t id)
{
    bool accepted = false;
    if (id == 0) {
        accepted = sd_request(SD_JOB_BACKUP);
    } else if (id == 1) {
        accepted = sd_request(SD_JOB_RESTORE);
    }
    s_sd_message = accepted ? NULL : "Request Rejected; Try Again";
    if (!accepted) ESP_LOGW(TAG, "SD menu action %u not queued", id);
    menu_open(MENU_SD);
}

/* The reset submenu is a confirmation step: the destructive action is never one
 * tap away from the main list. */
static void on_reset_pick(uint8_t id)
{
    if (id == 0) do_factory_reset();
}

static void on_power_pick(uint8_t id)
{
    switch (id) {
    case 2: g.power_mode = (g.power_mode + 1) % 3; break;
    case 3: g.screen_timeout = (g.screen_timeout + 1) % 5; break;
    case 4: g.deep_sleep_after = (g.deep_sleep_after + 1) % 5; break;
    case 5: g.rtc_wake = (g.rtc_wake + 1) % 3; break;
    case 6: menu_open(MENU_POWER_CONFIRM); return;
    case 7:
        if (!power_request_deep_sleep(g.rtc_wake == 0 ? 900 : g.rtc_wake == 1 ? 3600 : 21600))
            ui_hud_bubble("Sleep Rejected; Worker Busy", 0xf5c86b, 2200);
        else ui_menu_close();
        return;
    default: break;
    }
    game_save_now();
    menu_open(MENU_POWER);
    if (id >= 4) ui_menu_set_page(1);
}

static void on_display_pick(uint8_t id)
{
    if (id == 1) power_rotation_cycle();
    menu_open(MENU_DISPLAY);
}
/* BACK from a submenu returns to the main menu; BACK from the main menu just
 * closes (ui_menu_set_back is not set there, so the menu closes itself). */
static void back_to_main(void)
{
    menu_open(MENU_MAIN);
}

static const uint8_t k_track_background[AUDIO_MUSIC_TRACK_COUNT] = {2,18,6,14,1,13,29,45,37,21};

static void menu_open(int which)
{
    cancel_enclosure_tap();
    ui_menu_item_t *items = s_menu_items;

    /* Remember which menu is on screen so the render tick can tell whether the
     * WiFi list is still the one being looked at. Without this, a scan that
     * finishes while the player has navigated elsewhere would yank them back. */
    s_open_menu = which;

    switch (which) {
    case MENU_MAIN: {
        /* Every row carries a subtext. A list where some rows have a second
         * line and others do not reads as unfinished, and it also makes the
         * rows different heights, so the eye cannot scan the column. */
        static char daily_sub[48], ach_sub[32], species_sub[32];
        if (needs_daily_available()) {
            snprintf(daily_sub, sizeof daily_sub, "Check In Ready");
        } else if (needs_daily_task_ready()) {
            snprintf(daily_sub, sizeof daily_sub, "Task Complete");
        } else {
            snprintf(daily_sub, sizeof daily_sub, "Streak %u", (unsigned)g.daily_streak);
        }
        int earned = 0;
        for (int i = 0; i < ACHIEVEMENT_COUNT; i++) {
            if (g.achievements & ACHIEVEMENTS[i].bit) earned++;
        }
        snprintf(ach_sub, sizeof ach_sub, "%d of %d Earned", earned, ACHIEVEMENT_COUNT);
        int unlocked = 0;
        for (int i = 0; i < PREY_SPECIES_COUNT; i++) {
            if (species_unlocked((prey_species_t)i)) unlocked++;
        }
        snprintf(species_sub, sizeof species_sub, "%d of %d Unlocked",
                 unlocked, PREY_SPECIES_COUNT);
        items[0] = (ui_menu_item_t){ "Daily", daily_sub, true, false, true, MENU_DAILY };
        items[1] = (ui_menu_item_t){ "Treats", "Spend Silk", true, false, true, MENU_SHOP };
        items[2] = (ui_menu_item_t){ "Backgrounds", "Enclosure Themes", true, false, true, MENU_THEME };
        items[3] = (ui_menu_item_t){ "Achievements", ach_sub, true, false, true, MENU_ACHIEVEMENTS };
        items[4] = (ui_menu_item_t){ "Prey Species", species_sub, true, false, true, MENU_SPECIES };
        static char decor_sub[48];
        snprintf(decor_sub, sizeof decor_sub, "%d Placed", decor_placed_count());
        items[5] = (ui_menu_item_t){ "Decor", decor_sub, true, false, true, MENU_DECOR };
        /* Settings is always the last row: it is the least-used entry and the
         * one the player reaches for least often, so it belongs at the bottom
         * of the list rather than in the middle. */
        items[6] = (ui_menu_item_t){ "Borders", "Mix and Match Frames", true, false, true, MENU_BORDERS };
        items[7] = (ui_menu_item_t){ "Help Guide", "All Features and Controls", true, false, true, MENU_HELP };
        items[8] = (ui_menu_item_t){ "Settings", "Sound, Display, WiFi", true, false, true, MENU_SETTINGS };
        ui_menu_open("Spidarium", items, 9, on_main_pick);
        ui_menu_set_item_icon(6, sprite_border_index(g.border_id == 255 ? g.background_id : g.border_id));
        break;
    }
    case MENU_SETTINGS: {
        items[0] = (ui_menu_item_t){ "WiFi", "Networks and Time Sync", true, false, true, 0 };
        items[1] = (ui_menu_item_t){ "SD Card", "Back Up and Restore", true, false, true, 1 };
        items[2] = (ui_menu_item_t){ "About", "Version and Hardware", true, false, true, 2 };
        items[3] = (ui_menu_item_t){ "Battery", "Charge and Runtime", true, false, true, 3 };
        items[4] = (ui_menu_item_t){ "Display", "Brightness and Rotation", true, false, true, 4 };
        items[5] = (ui_menu_item_t){ "Sound", "Volume and Mute", true, false, true, 5 };
        items[6] = (ui_menu_item_t){ "Auto Feed", "Wild Prey and Unattended Hunting",
                                     true, g.auto_feed, false, 6 };
        items[7] = (ui_menu_item_t){ "Save Screenshot", net_busy() ? "Wait for WiFi" : sd_busy() ? "Saving..." :
            s_screenshot_failed ? "Capture Failed; See Serial Log" : sd_status_text(),
            !sd_busy() && !net_busy(), false, false, 7 };
        items[8] = (ui_menu_item_t){ "Wipe Spider", "Erases This Spider", true, false, true, 8 };
        ui_menu_open("Settings", items, 9, on_settings_pick);
        ui_menu_set_item_checkbox(6);
        ui_menu_set_destructive(8);
        ui_menu_set_back(back_to_main);
        break;
    }
    case MENU_POWER: {
        static char status[80], runtime[48];
        power_status(status, sizeof status);
        power_runtime_text(runtime, sizeof runtime);
        items[0] = (ui_menu_item_t){ "Battery", status, false, false, false, 0 };
        items[1] = (ui_menu_item_t){ "Runtime", runtime, false, false, false, 0 };
        items[2] = (ui_menu_item_t){ "Power Mode", power_mode_name(g.power_mode), true, false, false, 2 };
        items[3] = (ui_menu_item_t){ "Screen Timeout", power_timeout_name(g.screen_timeout), true, false, false, 3 };
        items[4] = (ui_menu_item_t){ "Deep Sleep After", power_delay_name(g.deep_sleep_after), true, false, false, 4 };
        items[5] = (ui_menu_item_t){ "RTC Wake Interval", power_wake_name(g.rtc_wake), true, false, false, 5 };
        items[6] = (ui_menu_item_t){ "Deep Sleep Now", "Save Then Sleep / Right Button Wakes", !net_busy(), false, true, 6 };
        items[7] = (ui_menu_item_t){ "Automatic Sleep", "RTC Mode Only / Battery, Not USB", false, false, false, 8 };
        ui_menu_open("Battery", items, 8, on_power_pick);
        ui_menu_set_back(back_to_settings);
        break;
    }
    case MENU_POWER_CONFIRM:
        items[0] = (ui_menu_item_t){ "Save and Sleep", power_wake_name(g.rtc_wake), true, false, false, 7 };
        items[1] = (ui_menu_item_t){ "Wake With Right Button", "Touch Is Off / Timer Also Wakes", false, false, false, 8 };
        items[2] = (ui_menu_item_t){ "Pet Ages While Asleep", "RTC Catch-Up on Wake / No Radio", false, false, false, 9 };
        ui_menu_open("Deep Sleep?", items, 3, on_power_pick);
        ui_menu_set_back(back_to_settings);
        break;
    case MENU_DISPLAY: {
        /* The slider row's readout is derived from its value by refresh_page(). */
        items[0] = (ui_menu_item_t){ "Brightness", NULL, true, false, false, 0 };
        items[1] = (ui_menu_item_t){ "Screen Rotation", power_rotation_name(power_rotation()),
                                     true, false, true, 1 };
        ui_menu_open("Display", items, 2, on_display_pick);
        ui_menu_set_item_slider(0, g.brightness, on_brightness_slider);
        ui_menu_set_back(back_to_settings);
        break;
    }

    case MENU_WIFI: {
        int n = 0;
        if (net_busy()) {
            items[n++] = (ui_menu_item_t){ "Working; Please Wait", "Radio Is Busy",
                                           false, false, false, 0 };
            ui_menu_open("WiFi", items, n, on_wifi_pick);
            ui_menu_set_back(back_to_settings);
            break;
        }
        items[n++] = (ui_menu_item_t){ "Network Status", s_wifi_message[0] ? s_wifi_message : net_status_text(),
                                       false, false, false, 0 };
        items[n++] = (ui_menu_item_t){ "Scan Networks", "Find Nearby Networks",
                                       true, false, false, 0 };
        items[n++] = (ui_menu_item_t){ "Add Network", "Enter Name and Password",
                                       true, false, true, 1 };
        items[n++] = (ui_menu_item_t){ "Saved Networks", "Join or Forget",
                                       true, false, true, 2 };
        ui_menu_open("WiFi", items, n, on_wifi_pick);
        ui_menu_set_back(back_to_settings);
        break;
    }
    case MENU_WIFI_SCAN: {
        int n = 0;
        items[n++] = (ui_menu_item_t){ "Scan Status", s_wifi_message[0] ? s_wifi_message : net_status_text(),
                                       false, false, false, 0 };
        static char details[12][32];
        int found = net_scan_count();
        for (int i = 0; i < found && n < 20; i++) {
            const char *ssid = net_scan_ssid(i);
            if (!ssid[0]) continue;   /* hidden network */
            bool secure = net_scan_secure(i);
            snprintf(details[i], sizeof details[i], "%d dBm / %s", net_scan_rssi(i),
                     secure ? "Secured" : "Open");
            items[n].label      = ssid;
            items[n].detail     = details[i];
            items[n].enabled    = true;
            items[n].selected   = (strcmp(ssid, net_ssid()) == 0);
            items[n].is_submenu = secure;
            items[n].id         = (uint8_t)(i + 4);
            n++;
        }
        ui_menu_open("Nearby Networks", items, n, on_wifi_pick);
        for (int i = 0; i < n; i++) ui_menu_set_item_icon(i, sprite_icon_index(i ? "wifi" : "status"));
        ui_menu_set_back(back_to_wifi);
        break;
    }
    case MENU_WIFI_SAVED: {
        int count = net_saved_count();
        for (int i = 0; i < count; i++)
            items[i] = (ui_menu_item_t){ net_saved_ssid(i), "Tap to Manage",
                                         true, false, true, i };
        if (!count) items[0] = (ui_menu_item_t){ "No Saved Networks", "Add One First",
                                                 false, false, false, 0 };
        ui_menu_open("Saved Networks", items, count ? count : 1, on_saved_network);
        ui_menu_set_back(back_to_wifi);
        break;
    }
    case MENU_WIFI_NETWORK:
        items[0] = (ui_menu_item_t){ "Sync RTC Now", "Set Clock From Network", true, false, false, 0 };
        items[1] = (ui_menu_item_t){ "Forget Network", "Remove Saved Credentials", true, false, false, 1 };
        ui_menu_open(net_saved_ssid(s_saved_network), items, 2, on_network_action);
        ui_menu_set_back(back_to_wifi);
        break;
    case MENU_OTA:
        items[0] = (ui_menu_item_t){ "Update From GitHub", "Latest Stable Release / Preserves Pet", net_has_credentials(), false, false, 0 };
        items[1] = (ui_menu_item_t){ "Advanced HTTPS URL", "Application Binary / Trusted Host Only", net_has_credentials(), false, true, 1 };
        items[2] = (ui_menu_item_t){ "Update Status", net_status_text(), false, false, false, 2 };
        items[3] = (ui_menu_item_t){ "After Installation", "Press RESET to Apply / Art Changes Need USB", false, false, false, 3 };
        ui_menu_open("Firmware Update", items, 4, on_ota_pick);
        ui_menu_set_back(back_to_about);
        break;

    case MENU_SD: {
        /* Mount on demand. Probing at boot stole DMA buffers from the LCD
         * driver and logged a burst of sdmmc errors on units with no card. */
        bool have = sd_present();
        items[0] = (ui_menu_item_t){ "Back Up Save",
                                     have ? "Write to Card" : sd_status_text(),
                                     have, false, false, 0 };
        items[1] = (ui_menu_item_t){ "Restore Save",
                                     !have ? sd_status_text() : sd_backup_exists() ? "Read From Card" : "No Backup",
                                     have && sd_backup_exists(), false, false, 1 };
        items[2] = (ui_menu_item_t){ "SD Status", s_sd_message ? s_sd_message : sd_status_text(),
                                     false, false, false, 2 };
        ui_menu_open("SD Card", items, 3, on_sd_pick);
        ui_menu_set_back(back_to_settings);
        break;
    }

    case MENU_RESET: {
        items[0] = (ui_menu_item_t){ "Start a New Tank", "Erases This Spider", true, false, false, 0 };
        ui_menu_open("New Tank", items, 1, on_reset_pick);
        ui_menu_set_destructive(0);
        ui_menu_set_back(back_to_settings);
        break;
    }

    case MENU_THEME: {
        static char names[THEME_COUNT][32];
        static char theme_sub[THEME_COUNT][32];
        for (int i = 0; i < THEME_COUNT; i++) {
            const theme_def_t *t = theme_get((uint8_t)i);
            ui_menu_format_name(names[i], sizeof names[i], t->name);
            /* Every row carries a subtext, so the list scans as one column. */
            if (!theme_unlocked((uint8_t)i)) {
                snprintf(theme_sub[i], sizeof theme_sub[i], "Level %lu",
                         (unsigned long)t->unlock_level);
            } else if (theme_current() == t) {
                snprintf(theme_sub[i], sizeof theme_sub[i], "In Use");
            } else if (theme_owned(i)) {
                snprintf(theme_sub[i], sizeof theme_sub[i], "Owned / Apply Free");
            } else {
                snprintf(theme_sub[i], sizeof theme_sub[i], "%u Silk / Buy", theme_cost(i));
            }
            items[i].label     = names[i];
            items[i].detail    = theme_sub[i];
            items[i].enabled   = theme_unlocked((uint8_t)i) &&
                (theme_owned(i) || g.silk >= theme_cost(i));
            items[i].selected  = (theme_current() == t);
            items[i].is_submenu = false;
            items[i].id        = (uint8_t)i;
        }
        ui_menu_open_grid("Backgrounds", items, THEME_COUNT, on_theme_pick);
        for (int i = 0; i < THEME_COUNT; i++)
            ui_menu_set_item_icon(i, sprite_background_index(i));
        ui_menu_set_back(back_to_main);
        break;
    }

    case MENU_BORDERS: {
        static char names[THEME_COUNT][32], details[THEME_COUNT][40];
        items[0] = (ui_menu_item_t){ "No Border", "Remove Frame / Free", true,
                                     g.border_id == 255, false, 255 };
        for (int i = 0; i < THEME_COUNT; i++) {
            ui_menu_format_name(names[i], sizeof names[i], theme_get(i)->name);
            if (g.border_id == i) snprintf(details[i], sizeof details[i], "In Use");
            else if (border_owned(i)) snprintf(details[i], sizeof details[i], "Owned / Apply Free");
            else snprintf(details[i], sizeof details[i], "%u Silk / Buy Frame", border_cost(i));
            items[i + 1] = (ui_menu_item_t){ names[i], details[i], border_owned(i) || g.silk >= border_cost(i),
                                            g.border_id == i, false, i };
        }
        ui_menu_open_grid("Borders", items, THEME_COUNT + 1, on_border_pick);
        for (int i = 0; i < THEME_COUNT; i++) ui_menu_set_item_icon(i + 1, sprite_border_index(i));
        ui_menu_set_back(back_to_main);
        break;
    }
    case MENU_SOUND: {
        /* The slider row's readout is derived from its value by refresh_page(),
         * so no detail string is needed here. */
        items[0] = (ui_menu_item_t){ "Volume", NULL, true, false, false, 0 };
        items[1] = (ui_menu_item_t){ "Sound", audio_muted() ? "Off / All Audio Muted" : "On / Effects and Music",
                                    true, !audio_muted(), false, 1 };
        items[2] = (ui_menu_item_t){ "Background Music", "Gentle Ambient Score", true, g.music_enabled, false, 2 };
        items[3] = (ui_menu_item_t){ "Music Volume", NULL, true, false, false, 3 };
        items[4] = (ui_menu_item_t){ "Soundtrack", audio_music_track_name(g.music_track),
                                    true, false, true, 4 };
        ui_menu_open("Sound", items, 5, on_sound_pick);
        ui_menu_set_item_checkbox(1);
        ui_menu_set_item_checkbox(2);
        ui_menu_set_item_slider(0, audio_volume(), on_volume_slider);
        ui_menu_set_item_slider(3, g.music_volume, on_volume_slider);
        ui_menu_set_item_icon(3, sprite_icon_index("energy"));
        ui_menu_set_item_icon(4, sprite_background_index(k_track_background[g.music_track]));
        ui_menu_set_back(back_to_settings);
        break;
    }
    case MENU_SOUNDTRACK:
        for (int i = 0; i < AUDIO_MUSIC_TRACK_COUNT; i++)
            items[i] = (ui_menu_item_t){ audio_music_track_name(i),
                i == g.music_track ? "Selected / Original Ambient Score" : "Choose This Score",
                true, i == g.music_track, false, i };
        ui_menu_open("Soundtrack", items, AUDIO_MUSIC_TRACK_COUNT, on_soundtrack_pick);
        for (int i = 0; i < AUDIO_MUSIC_TRACK_COUNT; i++)
            ui_menu_set_item_icon(i, sprite_background_index(k_track_background[i]));
        ui_menu_set_back(back_to_sound);
        break;

    case MENU_ACHIEVEMENTS: {
        for (int i = 0; i < ACHIEVEMENT_COUNT; i++) {
            bool got = (g.achievements & ACHIEVEMENTS[i].bit) != 0;
            items[i].label     = ACHIEVEMENTS[i].name;
            items[i].detail    = got ? "Earned / +15 XP +20 Silk" : "Goal / +15 XP +20 Silk";
            items[i].enabled   = true;
            items[i].selected  = got;
            items[i].is_submenu = false;
            items[i].id        = (uint8_t)i;
        }
        ui_menu_open_grid("Achievements", items, ACHIEVEMENT_COUNT, on_achievement_pick);
        for (int i = 0; i < ACHIEVEMENT_COUNT; i++) ui_menu_set_item_icon(i, sprite_achievement_index(ACHIEVEMENTS[i].bit));
        ui_menu_set_back(back_to_main);
        break;
    }
    case MENU_SPECIES:
        static char species_detail[PREY_SPECIES_COUNT][64];
        static char species_names[PREY_SPECIES_COUNT][32];
        for (int i = 0; i < PREY_SPECIES_COUNT; i++) {
            const species_def_t *species = species_get((prey_species_t)i);
            /* The nutrition is shown as a plain multiplier so the player can
             * see why a waxworm is worth waiting for. */
            snprintf(species_detail[i], sizeof species_detail[i],
                "%s / level %lu / food x%.1f",
                species_unlocked(i) ? "Available" : "Locked",
                (unsigned long)species->unlock_level,
                (double)species->hunger_mult);
            ui_menu_format_name(species_names[i], sizeof species_names[i], species->name);
            items[i] = (ui_menu_item_t){ species_names[i],
                species_detail[i], false, species_unlocked(i), false, i };
        }
        static char species_title[48];
        snprintf(species_title, sizeof species_title, "Prey / Lv %lu", (unsigned long)g.level);
        ui_menu_open_grid(species_title, items, PREY_SPECIES_COUNT, NULL);
        for (int i = 0; i < PREY_SPECIES_COUNT; i++)
            ui_menu_set_item_icon(i, sprite_prey_index(i, false, false, 0));
        ui_menu_set_back(back_to_main);
        break;
    case MENU_DAILY: {
        static char claim_sub[64], task_sub[64], task_name[48];
        uint32_t today = needs_daily_today();
        if (needs_daily_available()) {
            uint32_t next = (g.daily_day != DAILY_NEVER && g.daily_day + 1 == today &&
                             g.daily_streak > 0)
                          ? (g.daily_streak < DAILY_STREAK_MAX ? g.daily_streak + 1u : DAILY_STREAK_MAX)
                          : 1u;
            uint32_t step = next - 1u;
            snprintf(claim_sub, sizeof claim_sub, "Day %u / +%lu XP +%lu Silk",
                     (unsigned)next,
                     (unsigned long)(DAILY_XP_BASE + step * DAILY_XP_PER_DAY),
                     (unsigned long)(DAILY_SILK_BASE + step * DAILY_SILK_PER_DAY));
        } else {
            snprintf(claim_sub, sizeof claim_sub, "Claimed / Streak %u",
                     (unsigned)g.daily_streak);
        }
        items[0] = (ui_menu_item_t){ "Check In", claim_sub, true, false, true, 0 };

        uint8_t task = needs_daily_task();
        ui_menu_format_name(task_name, sizeof task_name, needs_daily_task_name(task));
        if (g.daily_task_done) {
            snprintf(task_sub, sizeof task_sub, "Done / +%u XP +%u Silk",
                     DAILY_TASK_XP, DAILY_TASK_SILK);
        } else if (needs_daily_task_ready()) {
            snprintf(task_sub, sizeof task_sub, "Complete / Tap to Claim");
        } else {
            snprintf(task_sub, sizeof task_sub, "%lu of %lu",
                     (unsigned long)needs_daily_task_progress(),
                     (unsigned long)needs_daily_task_goal());
        }
        items[1] = (ui_menu_item_t){ task_name, task_sub, true, false, true, 1 };

        static char daily_title[48];
        snprintf(daily_title, sizeof daily_title, "Daily / Day %lu",
                 (unsigned long)today);
        ui_menu_open(daily_title, items, 2, on_daily_pick);
        ui_menu_set_back(back_to_main);
        break;
    }
    case MENU_SHOP: {
        static char shop_sub[TREAT_COUNT][48];
        static char shop_name[TREAT_COUNT][32];
        for (int i = 0; i < TREAT_COUNT; i++) {
            uint32_t cost = treat_cost((treat_t)i);
            ui_menu_format_name(shop_name[i], sizeof shop_name[i],
                                treat_name((treat_t)i));
            snprintf(shop_sub[i], sizeof shop_sub[i], "%lu Silk%s",
                     (unsigned long)cost, g.silk < cost ? " / Need More" : "");
            items[i] = (ui_menu_item_t){ shop_name[i], shop_sub[i],
                                         g.silk >= cost, false, true, (uint8_t)i };
        }
        static char shop_title[48];
        snprintf(shop_title, sizeof shop_title, "Treats / %lu Silk",
                 (unsigned long)g.silk);
        ui_menu_open_grid(shop_title, items, TREAT_COUNT, on_shop_pick);
        for (int i = 0; i < TREAT_COUNT; i++) ui_menu_set_item_icon(i, sprite_treat_index(i));
        ui_menu_set_back(back_to_main);
        break;
    }
    case MENU_DECOR: {
        static char decor_sub[DECOR_COUNT][48];
        static char decor_name[DECOR_COUNT][32];
        for (int i = 0; i < DECOR_COUNT; i++) {
            const decor_def_t *def = decor_def((uint8_t)i);
            ui_menu_format_name(decor_name[i], sizeof decor_name[i], def->name);
            if (!decor_unlocked((uint8_t)i)) {
                snprintf(decor_sub[i], sizeof decor_sub[i], "Level %u / %u Silk",
                         def->unlock_level, def->cost);
            } else if (decor_owned((uint8_t)i)) {
                snprintf(decor_sub[i], sizeof decor_sub[i], "Owned / %s",
                         def->blocking ? "Blocks" : "Walk Over");
            } else {
                snprintf(decor_sub[i], sizeof decor_sub[i], "%u Silk / %s",
                         def->cost, def->blocking ? "Blocks" : "Walk Over");
            }
            /* `selected` is deliberately NOT "owned". ui_menu_open() jumps to
             * the page of the first selected row, so marking owned kinds
             * selected yanked the player to page 2 the moment they bought
             * anything past the third row. The owned state is already in the
             * subtext and the checkmark is not needed here. */
            items[i] = (ui_menu_item_t){ decor_name[i], decor_sub[i],
                                         decor_unlocked(i) && (decor_owned(i) || g.silk >= def->cost),
                                         false, true, (uint8_t)i };
        }
        static char decor_title[48];
        snprintf(decor_title, sizeof decor_title, "Decor / %lu Silk",
                 (unsigned long)g.silk);
        ui_menu_open_grid(decor_title, items, DECOR_COUNT, on_decor_pick);
        /* Every row gets its own sprite, so the shop reads as a catalogue of
         * things rather than a list of names. */
        for (int i = 0; i < DECOR_COUNT; i++)
            ui_menu_set_item_icon((uint8_t)i, sprite_decor_item_index((uint8_t)i));
        ui_menu_set_back(back_to_main);
        break;
    }
    case MENU_DECOR_ITEM:
        decor_begin_move(s_decor_item_slot);
        break;
    case MENU_GIFT_ITEM:
        on_gift_item_pick(0);
        break;
    case MENU_ABOUT:
        items[0] = (ui_menu_item_t){ "Spidarium", esp_app_get_description()->version,
                                     false, false, false, 0 };
        items[1] = (ui_menu_item_t){ "ESP32-S3 / 480x480 AMOLED", "8 MB PSRAM / 16 MB Flash",
                                     false, false, false, 0 };
        items[2] = (ui_menu_item_t){ "Personal, Non-Commercial", "See LICENSE",
                                     false, false, false, 0 };
        items[3] = (ui_menu_item_t){ "Firmware Update", "GitHub Latest Release", true, false, true, 3 };
        ui_menu_open("About", items, 4, on_about_pick);
        ui_menu_set_back(back_to_settings);
        break;
    case MENU_HELP:
        for (int i = 0; i < HELP_TOPIC_COUNT; i++)
            items[i] = (ui_menu_item_t){ HELP_TOPICS[i].name, "Tap to Read", true, false, true, i };
        ui_menu_open("Help Guide", items, HELP_TOPIC_COUNT, on_help_pick);
        for (int i = 0; i < HELP_TOPIC_COUNT; i++) ui_menu_set_item_icon(i, sprite_icon_index(HELP_TOPICS[i].icon));
        ui_menu_set_back(back_to_main);
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Pause                                                               */
/* ------------------------------------------------------------------ */
/* LVGL-task-only gameplay pause. Keep the service/render timer alive so the
 * UI can process requests and resume. Network exclusivity uses the adapter's
 * worker-side pause/acknowledgement API instead, including LCD composition. */
void game_pause(void)
{
    if (s_paused) return;
    s_paused = true;

    if (s_logic_timer)  lv_timer_pause(s_logic_timer);

    ESP_LOGI(TAG, "game paused (WiFi/OTA exclusive access to DMA RAM)");
}

/* LVGL-task-only; reset elapsed time rather than simulating the pause. */
void game_resume(void)
{
    if (!s_paused) return;
    s_paused = false;

    /* Reset the delta baseline, or the first tick after a long pause sees a
     * huge dt and the spider teleports. */
    s_last_logic = lv_tick_get();

    if (s_logic_timer)  lv_timer_resume(s_logic_timer);

    ESP_LOGI(TAG, "game resumed (DMA RAM shared again)");
}

bool game_paused(void)
{
    return s_paused || save_restore_pending() || net_busy() || power_sleep_pending();
}

void game_on_menu(lv_event_t *e)
{
    (void)e;
    g.taps++;
    menu_open(MENU_MAIN);
}

/* Open the per-item decor menu for a slot, for the serial console. */
bool game_ui_open_decor_item(int slot)
{
    if (!decor_at(slot)) return false;
    s_decor_item_slot = slot;
    menu_open(MENU_DECOR_ITEM);
    return true;
}

/* Open a menu by name, for the serial console.
 *
 * This calls the same menu_open() the UI calls, so a test through here exercises
 * the real path, including the SD page's queued on-demand probe. */
bool game_open_menu(const char *name)
{
    if      (!strcmp(name, "main"))     menu_open(MENU_MAIN);
    else if (!strcmp(name, "settings")) menu_open(MENU_SETTINGS);
    else if (!strcmp(name, "wifi"))     menu_open(MENU_WIFI);
    else if (!strcmp(name, "saved"))    menu_open(MENU_WIFI_SAVED);
    else if (!strcmp(name, "ota"))      menu_open(MENU_OTA);
    else if (!strcmp(name, "password")) game_ui_open_wifi_password_entry();
    else if (!strcmp(name, "species"))  menu_open(MENU_SPECIES);
    else if (!strcmp(name, "about"))    menu_open(MENU_ABOUT);
    else if (!strcmp(name, "help"))     menu_open(MENU_HELP);
    else if (!strcmp(name, "sd"))       on_settings_pick(1);
    else if (!strcmp(name, "theme"))    menu_open(MENU_THEME);
    else if (!strcmp(name, "borders"))  menu_open(MENU_BORDERS);
    else if (!strcmp(name, "sound"))    menu_open(MENU_SOUND);
    else if (!strcmp(name, "soundtrack")) menu_open(MENU_SOUNDTRACK);
    else if (!strcmp(name, "ach"))      menu_open(MENU_ACHIEVEMENTS);
    else if (!strcmp(name, "reset"))    menu_open(MENU_RESET);
    else if (!strcmp(name, "close"))    { ui_menu_close(); ui_hud_gift_menu_hide(); }
    else if (!strcmp(name, "power")) menu_open(MENU_POWER);
    else if (!strcmp(name, "display")) menu_open(MENU_DISPLAY);
    else if (!strcmp(name, "daily")) menu_open(MENU_DAILY);
    else if (!strcmp(name, "shop")) menu_open(MENU_SHOP);
    else if (!strcmp(name, "decor")) menu_open(MENU_DECOR);
    else if (!strcmp(name, "decoritem")) menu_open(MENU_DECOR_ITEM);
    else return false;
    return true;
}

/* ------------------------------------------------------------------ */
/* UI marshalling for the serial console                               */
/* ------------------------------------------------------------------ */
/* The console runs on its own task. Anything that touches lv_* must run on the
 * LVGL task instead, or it corrupts LVGL's internal state -- the same class of
 * bug as calling lv_timer_pause() from a worker, which crashed the device.
 *
 * So the console posts a request and the render tick performs it. That is the
 * request-plus-acknowledgement pattern the rest of the project uses. */
static atomic_int s_ui_req = GAME_UI_NONE;
static int s_ui_value;
static atomic_bool s_ui_ok;
static lv_draw_buf_t *s_capture;
const lv_draw_buf_t *game_screen_capture(void) { return s_capture; }

static lv_draw_buf_t *capture_target(int target)
{
    if (net_busy()) {
        ESP_LOGW(TAG, "screenshot rejected: radio operation in progress");
        return NULL;
    }
    if (target != 2) return lv_snapshot_take(target == 1 ? cspider_obj() : lv_screen_active(),
        target == 1 ? LV_COLOR_FORMAT_ARGB8888 : LV_COLOR_FORMAT_RGB565);
    ui_hud_capture_hide(true);
    ui_menu_capture_hide(true);
    lv_draw_buf_t *image = lv_snapshot_take(ui.screen, LV_COLOR_FORMAT_RGB565);
    ui_menu_capture_hide(false);
    ui_hud_capture_hide(false);
    return image;
}

static bool game_save_screenshot(void)
{
    if (sd_busy() || s_sd_capture || save_restore_pending()) {
        ESP_LOGW(TAG, "screenshot rejected: SD busy or restore awaiting reboot");
        return false;
    }
    lv_draw_buf_t *capture = capture_target(2);
    if (!capture) { ESP_LOGE(TAG, "playfield snapshot allocation failed"); return false; }
    sd_screenshot_t image = { .width = capture->header.w, .height = capture->header.h,
                             .stride = capture->header.stride };
    image.pixels = capture->data;
    if (!sd_request_screenshot(&image)) {
        lv_draw_buf_destroy(capture);
        return false;
    }
    s_sd_capture = capture;
    return true;
}

static bool capture_test(void)
{
    if (!ui_menu_visible() || lv_screen_active() != ui.screen) {
        ESP_LOGE(TAG, "capture test requires an open menu on the tank");
        return false;
    }
    game_state_t state = g;
    lv_draw_buf_t *before = capture_target(0);
    lv_draw_buf_t *field = capture_target(2);
    lv_draw_buf_t *after = capture_target(0);
    bool restored = before && after && before->header.w == after->header.w &&
        before->header.h == after->header.h;
    bool differs = false;
    bool dimensions = field && field->header.w == ui.W && field->header.h == ui.H;
    if (restored && dimensions) {
        for (unsigned y = 0; y < before->header.h; y++) {
            restored = restored && !memcmp(before->data + y * before->header.stride,
                after->data + y * after->header.stride, before->header.w * 2);
            differs = differs || memcmp(before->data + y * before->header.stride,
                field->data + y * field->header.stride, before->header.w * 2);
        }
    }
    bool unchanged = !memcmp(&state, &g, sizeof g);
    if (before) lv_draw_buf_destroy(before);
    if (field) lv_draw_buf_destroy(field);
    if (after) lv_draw_buf_destroy(after);
    ESP_LOGI(TAG, "capture test: menu_restored=%d playfield_differs=%d dimensions=%d state_unchanged=%d",
        restored, differs, dimensions, unchanged);
    return restored && differs && dimensions && unchanged;
}
enum { UI_PREPARING = -1 };

static bool test_gifts(void)
{
    game_state_t original = g;
    int tested = 0;
    for (int kind = 0; kind < GIFT_KINDS; kind++) {
        g.gift.used = 1;
        g.gift.kind = kind;
        gift_compute();
        const gift_bonus_t *bonus = gift_bonus();
        bool effective = bonus->energy_regen_mult != 1.0f ||
            bonus->hygiene_decay_mult != 1.0f || bonus->pounce_range_mult != 1.0f ||
            bonus->aim_window_mult != 1.0f || bonus->wild_spawn_bonus != 0 ||
            bonus->pounce_success_mult != 1.0f || bonus->movement_efficiency_mult != 1.0f ||
            bonus->mood_mult != 1.0f || bonus->fear_mult != 1.0f ||
            bonus->prey_flee_penalty != 1.0f || bonus->catch_success_mult != 1.0f ||
            bonus->prey_detection_range_mult != 1.0f || bonus->all_stats_decay_mult != 1.0f;
        if (effective) tested++;
        printf("GIFT %d %s effective=%d eR=%.2f hD=%.2f range=%.2f catch=%.2f decay=%.2f\n",
               kind, gift_kind_name(kind), effective, bonus->energy_regen_mult,
               bonus->hygiene_decay_mult, bonus->pounce_range_mult,
               bonus->catch_success_mult * bonus->pounce_success_mult,
               bonus->all_stats_decay_mult);
    }
    g.gift.used = 0;
    gift_compute();
    bool removed = gift_bonus()->energy_regen_mult == 1.0f &&
                   gift_bonus()->all_stats_decay_mult == 1.0f &&
                   gift_bonus()->catch_success_mult == 1.0f;
    g = original;
    gift_compute();
    bool economy = gift_economy_test();
    bool visits = behaviour_gift_test();
    return tested == GIFT_KINDS && removed && economy && visits;
}

bool game_ui_call(game_ui_action_t action)
{
    return game_ui_call_value(action, 0);
}

bool game_ui_call_value(game_ui_action_t action, int value)
{
    if (power_sleep_pending() || action <= GAME_UI_NONE || action > GAME_UI_ADD_SILK ||
        (action == GAME_UI_ADD_SILK && value <= 0) ||
        (action == GAME_UI_DEEP_SLEEP && (value < 5 || value > 86400)) ||
        (action == GAME_UI_SCREEN_CAPTURE && (value < 0 || value > 2)) ||
        (action >= GAME_UI_SET_HUNGER && action <= GAME_UI_SET_SICK && (value < 0 || value > 100)) ||
        ((action == GAME_UI_SET_AUTO_FEED || action == GAME_UI_SET_AUTO_HUNT ||
          action == GAME_UI_SET_MUSIC || action == GAME_UI_SET_MUTED) && (value < 0 || value > 1)) ||
        ((action == GAME_UI_SET_VOLUME || action == GAME_UI_SET_MUSIC_VOLUME) &&
         (value < 0 || value > 100)) ||
        (action == GAME_UI_SET_MUSIC_TRACK && (value < 0 || value >= AUDIO_MUSIC_TRACK_COUNT)) ||
        (action == GAME_UI_BACKGROUND && (value < 0 || value >= THEME_COUNT)) ||
        (action == GAME_UI_BORDER && value != 255 && (value < 0 || value >= THEME_COUNT)) ||
        (action == GAME_UI_STATE && (value < 0 || value > SPIDER_SICK)) ||
        (action == GAME_UI_SPAWN && (value < -1 || value >= PREY_SPECIES_COUNT))) {
        ESP_LOGW(TAG, "UI request rejected: invalid action or value");
        return false;
    }
    int expected = GAME_UI_NONE;
    if (!atomic_compare_exchange_strong(&s_ui_req, &expected, UI_PREPARING)) {
        ESP_LOGW(TAG, "UI request rejected: busy");
        return false;
    }
    s_ui_value = value;
    atomic_store(&s_ui_ok, true);
    atomic_store(&s_ui_req, action);

    /* Wait for the render tick to pick it up. Bounded, so a stalled render
     * timer cannot hang the console forever. */
    int attempts = action == GAME_UI_TEST_UI ? 1000 : action == GAME_UI_DECOR_TEST ? 500 : 100;
    for (int i = 0; i < attempts && s_ui_req != GAME_UI_NONE; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_ui_req != GAME_UI_NONE) {
        ESP_LOGW(TAG, "UI request acknowledgement timed out");
        return false;
    }
    return atomic_load(&s_ui_ok);
}

typedef struct { lv_point_t point; lv_indev_state_t state; } test_pointer_t;

static void test_pointer_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    test_pointer_t *pointer = lv_indev_get_user_data(indev);
    data->point = pointer->point;
    data->state = pointer->state;
}

bool game_ui_decor_edit(decor_edit_t operation, int value, int x, int y, int *count)
{
    if (s_ui_req != GAME_UI_NONE || (unsigned)operation > DECOR_EDIT_BLOCK ||
        x < INT16_MIN || x > INT16_MAX || y < INT16_MIN || y > INT16_MAX) {
        ESP_LOGE(TAG, "decor edit request rejected: busy or invalid input");
        return false;
    }
    s_decor_edit.operation = operation;
    s_decor_edit.value = value;
    s_decor_edit.x = x;
    s_decor_edit.y = y;
    bool ok = game_ui_call(GAME_UI_DECOR_EDIT);
    if (ok && count) *count = s_decor_edit.count;
    return ok;
}

static void test_pointer_send(lv_indev_t *indev, test_pointer_t *pointer,
                              int x, int y, lv_indev_state_t state)
{
    pointer->point = (lv_point_t){x, y};
    pointer->state = state;
    lv_indev_read(indev);
}

static bool test_furniture_pointer(void)
{
    game_state_t original = g;
    decor_item_t *original_extra = decor_detach_extra();
    s_testing_placement = true;
    ui_menu_close();
    ui_hud_set_chrome(false);
    decor_clear();
    g.decor_owned = 1u;
    int slot = decor_place(0, 110, 150);
    g.gift = (gift_t){0};
    terrarium_refresh_decor();
    lv_indev_t *indev = lv_indev_create();
    if (!indev) {
        ESP_LOGE(TAG, "placement test: pointer allocation failed");
        g = original;
        decor_restore_extra(original_extra);
        s_testing_placement = false;
        return false;
    }
    test_pointer_t pointer = {0};
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, test_pointer_read);
    lv_indev_set_user_data(indev, &pointer);
    lv_indev_set_mode(indev, LV_INDEV_MODE_EVENT);
    lv_indev_set_long_press_time(indev, 0);
    lv_obj_update_layout(ui.screen);
    test_pointer_send(indev, &pointer, 110, 150, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 110, 150, LV_INDEV_STATE_PRESSED);
    bool held = !ui_menu_visible() && g_decor_mode && g_decor_move_slot == slot;
    test_pointer_send(indev, &pointer, 110, 150, LV_INDEV_STATE_RELEASED);
    s_decor_item_slot = slot;
    decor_begin_move(slot);
    test_pointer_send(indev, &pointer, 110, 150, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 330, 350, LV_INDEV_STATE_PRESSED);
    bool staged = g_decor_mode && s_place_x == 330 && s_place_y == 350 &&
                  g.decor[slot].x == 110 && g.decor[slot].y == 150;
    test_pointer_send(indev, &pointer, 330, 350, LV_INDEV_STATE_RELEASED);
    staged = staged && g_decor_mode && g.decor[slot].x == 110;
    test_pointer_send(indev, &pointer, 326, 420, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 326, 420, LV_INDEV_STATE_RELEASED);
    bool moved = slot >= 0 && g.decor[slot].x == 330 && g.decor[slot].y == 350 &&
                 !g_decor_mode && g.decor_owned == 1;
    g.gift.kind = 0; g.gift.variant = 2; g.gift.used = 1; g.gift.x = 200; g.gift.y = 200;
    terrarium_refresh_gift();
    uint32_t silk = g.silk, owned = g.gifts_owned;
    on_gift_item_pick(0);
    test_pointer_send(indev, &pointer, 200, 200, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 350, 180, LV_INDEV_STATE_PRESSED);
    staged = staged && g.gift_mode && s_place_x == 350 && s_place_y == 180 &&
             g.gift.x == 200 && g.gift.y == 200;
    test_pointer_send(indev, &pointer, 350, 180, LV_INDEV_STATE_RELEASED);
    staged = staged && g.gift_mode && g.gift.x == 200;
    test_pointer_send(indev, &pointer, 326, 420, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 326, 420, LV_INDEV_STATE_RELEASED);
    bool gift_moved = !g.gift_mode && g.gift.x == 350 && g.gift.y == 180 &&
                      g.gift.variant == 2 && g.silk == silk && g.gifts_owned == owned;
    on_gift_item_pick(0);
    test_pointer_send(indev, &pointer, 350, 180, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 300, 240, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 300, 240, LV_INDEV_STATE_RELEASED);
    test_pointer_send(indev, &pointer, 154, 420, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 154, 420, LV_INDEV_STATE_RELEASED);
    bool cancelled = !g.gift_mode && g.gift.x == 350 && g.gift.y == 180 &&
                     g.gift.variant == 2 && g.silk == silk;
    decor_begin_move(slot);
    placement_position(-100, 1000);
    cancelled = cancelled && g.decor[slot].x == 330 &&
                s_place_x == g_play.left + decor_def(0)->radius &&
                s_place_y == g_play.bottom - decor_def(0)->radius;
    game_on_placement_cancel(NULL);
    cancelled = cancelled && g.decor[slot].x == 330 && g.decor[slot].y == 350;
    int count = decor_placed_count();
    decor_begin_placement(0);
    placement_position(180, 260);
    bool created = decor_placed_count() == count;
    game_on_placement_done(NULL);
    created = created && decor_placed_count() == count + 1;
    g.gifts_owned |= 1u;
    game_gift_chosen(0);
    placement_position(260, 260);
    created = created && g.gift.x == 350;
    game_on_placement_done(NULL);
    created = created && g.gift.x == 260 && g.gift.y == 260 && g.silk == silk;
    decor_begin_move(slot);
    uint8_t blocking = g.decor[slot].blocking;
    test_pointer_send(indev, &pointer, 240, 256, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 240, 256, LV_INDEV_STATE_RELEASED);
    bool actions = game_placement_blocking() != blocking && g.decor[slot].blocking == blocking;
    game_on_placement_cancel(NULL);
    actions = actions && g.decor[slot].blocking == blocking;
    decor_begin_move(slot);
    game_on_placement_blocking(NULL);
    game_on_placement_done(NULL);
    actions = actions && g.decor[slot].blocking != blocking;
    decor_begin_move(slot);
    test_pointer_send(indev, &pointer, 240, 362, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 240, 362, LV_INDEV_STATE_RELEASED);
    actions = actions && !g_decor_mode && !decor_at(slot) && !ui_menu_visible() && decor_owned(0);
    on_gift_item_pick(0);
    test_pointer_send(indev, &pointer, 240, 362, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 240, 362, LV_INDEV_STATE_RELEASED);
    actions = actions && !g.gift_mode && !g.gift.used && !ui_menu_visible() && g.silk == silk;
    decor_clear();
    int first = decor_place(0, 120, 150);
    decor_place(0, 220, 150);
    int last = decor_place(0, 320, 150);
    terrarium_refresh_decor();
    decor_begin_move(last);
    test_pointer_send(indev, &pointer, 120, 310, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 120, 310, LV_INDEV_STATE_RELEASED);
    bool layers = s_place_layer == 1 && decor_at(last)->x == 320 &&
        terrarium_preview_test(DECOR_WALK_OVER, 1);
    game_on_placement_cancel(NULL);
    layers = layers && decor_at(last)->x == 320 && decor_at(first)->x == 120;
    decor_begin_move(last);
    test_pointer_send(indev, &pointer, 120, 310, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 120, 310, LV_INDEV_STATE_RELEASED);
    game_on_placement_done(NULL);
    layers = layers && decor_at(1)->x == 320 && decor_at(2)->x == 220 && terrarium_decor_test();
    decor_begin_move(first);
    test_pointer_send(indev, &pointer, 356, 256, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 356, 256, LV_INDEV_STATE_RELEASED);
    bool paths = s_place_blocking == DECOR_WALK_BEHIND && decor_at(first)->blocking == 0 &&
        terrarium_preview_test(DECOR_WALK_BEHIND, 0);
    game_on_placement_done(NULL);
    paths = paths && decor_at(first)->blocking == DECOR_WALK_BEHIND &&
        !decor_blocks_point(120, 150) && decor_hit_test(120, 150) == first && terrarium_decor_test();
    decor_begin_placement(0);
    game_on_placement_blocking(NULL);
    game_on_placement_blocking(NULL);
    paths = paths && s_place_blocking == DECOR_WALK_BEHIND &&
        terrarium_preview_test(DECOR_WALK_BEHIND, 1);
    game_on_placement_cancel(NULL);
    lv_indev_set_long_press_time(indev, 550);
    cancel_enclosure_tap();
    uint8_t before_lights = g.lights_dimmed;
    bool lights = true;
    for (int i = 0; i < 6; i++) {
        int x = i % 2 ? 435 : 400;
        test_pointer_send(indev, &pointer, x, 400, LV_INDEV_STATE_PRESSED);
        test_pointer_send(indev, &pointer, x + 8, 407, LV_INDEV_STATE_PRESSED);
        if (i % 2) s_last_tap_at = game_now() - 350;
        test_pointer_send(indev, &pointer, x + 8, 407, LV_INDEV_STATE_RELEASED);
        lights = lights && g.lights_dimmed == (before_lights ^ (((i + 1) / 2) % 2));
    }
    cancel_enclosure_tap();
    before_lights = g.lights_dimmed;
    test_pointer_send(indev, &pointer, 400, 400, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 400, 400, LV_INDEV_STATE_RELEASED);
    s_last_tap_at = game_now() - 451;
    test_pointer_send(indev, &pointer, 400, 400, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 400, 400, LV_INDEV_STATE_RELEASED);
    lights = lights && g.lights_dimmed == before_lights;
    cancel_enclosure_tap();
    for (int i = 0; i < 2; i++) {
        test_pointer_send(indev, &pointer, 400, 400, LV_INDEV_STATE_PRESSED);
        test_pointer_send(indev, &pointer, 450, 400, LV_INDEV_STATE_PRESSED);
        test_pointer_send(indev, &pointer, 400, 400, LV_INDEV_STATE_PRESSED);
        test_pointer_send(indev, &pointer, 400, 400, LV_INDEV_STATE_RELEASED);
    }
    lights = lights && g.lights_dimmed == before_lights;
    cancel_enclosure_tap();
    lv_indev_delete(indev);
    g = original;
    decor_restore_extra(original_extra);
    apply_enclosure_lights();
    game_on_placement_cancel(NULL);
    s_testing_placement = false;
    ui_hud_set_place_mode(false, NULL);
    terrarium_refresh_gift();
    terrarium_refresh_decor();
    ESP_LOGI(TAG, "furniture pointer: hold=%d staged=%d decor_move=%d gift_move=%d cancel=%d new=%d actions=%d lights=%d paths=%d layers=%d",
             held, staged, moved, gift_moved, cancelled, created, actions, lights, paths, layers);
    return held && staged && moved && gift_moved && cancelled && created && actions && lights && paths && layers;
}

static bool test_hunt_pointer(int32_t target_x, int32_t target_y)
{
    test_pointer_t pointer = {0};
    lv_indev_t *indev = lv_indev_create();
    if (!indev) { ESP_LOGE(TAG, "hunt test: pointer allocation failed"); return false; }
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, test_pointer_read);
    lv_indev_set_user_data(indev, &pointer);
    lv_indev_set_mode(indev, LV_INDEV_MODE_EVENT);
    lv_obj_update_layout(ui.screen);
    test_pointer_send(indev, &pointer, target_x, target_y, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, target_x, target_y, LV_INDEV_STATE_RELEASED);
    bool tap = !behaviour_pouncing() && !behaviour_aim_dragging();

    /* Hit the real Care button, then tap the tank to dismiss without shooting. */
    test_pointer_send(indev, &pointer, 29, 29, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 29, 29, LV_INDEV_STATE_RELEASED);
    bool overlay = ui_hud_chrome_visible();
    test_pointer_send(indev, &pointer, target_x, target_y, LV_INDEV_STATE_PRESSED);
    overlay = overlay && !behaviour_aim_dragging();
    test_pointer_send(indev, &pointer, target_x, target_y, LV_INDEV_STATE_RELEASED);
    overlay = overlay && !ui_hud_chrome_visible() && !behaviour_pouncing();

    int start_x = g.spider_x, start_y = g.spider_y;
    test_pointer_send(indev, &pointer, start_x, start_y, LV_INDEV_STATE_PRESSED);
    bool press = behaviour_aim_dragging();
    test_pointer_send(indev, &pointer, target_x, target_y, LV_INDEV_STATE_PRESSED);
    behaviour_tick(game_now() + 5000, 0.25f);
    prey_tick(game_now() + 5000, 0.25f);
    int32_t px = 0, py = 0;
    bool held = behaviour_aim_dragging() && g.spider_x == start_x && g.spider_y == start_y &&
        prey_nearest(&px, &py) && px == target_x && py == target_y;
    test_pointer_send(indev, &pointer, target_x, target_y, LV_INDEV_STATE_RELEASED);
    bool released = behaviour_pouncing() && !behaviour_aim_dragging() &&
        behaviour_pounce_by_player() && g.energy < 80;
    uint32_t now = game_now();
    for (int i = 1; i <= 8; i++) behaviour_tick(now + i * 50, 0.05f);
    bool landed = !behaviour_pouncing() && g.state == SPIDER_EAT &&
        abs(g.spider_x - target_x) <= 1 && abs(g.spider_y - target_y) <= 1;
    float x, y;
    behaviour_pos(&x, &y);
    bool subpixel = fabsf(x - g.spider_x) < 1 && fabsf(y - g.spider_y) < 1;
    behaviour_set_state(SPIDER_WANDER);
    behaviour_seek_to(target_x + 40, target_y);
    behaviour_tick(now + 450, 0.05f);
    subpixel = subpixel && abs(g.spider_x - target_x) < 8 && abs(g.spider_y - target_y) < 8;
    lv_indev_delete(indev);
    bool ok = press && held && tap && overlay && released && landed && subpixel;
    printf("HUNT press=%d held=%d tap=%d overlay=%d released=%d landed=%d subpixel=%d\n",
           press, held, tap, overlay, released, landed, subpixel);
    return ok;
}

static bool test_hunt(void)
{
    if (behaviour_pouncing() || terrarium_poop_count() == MAX_POOP) {
        ESP_LOGW(TAG, "hunt test requires no active jump and a free poop slot");
        return false;
    }
    game_state_t original = g;
    lv_obj_t *poop[MAX_POOP];
    memcpy(poop, ui.poop, sizeof poop);
    int original_prey = prey_count();
    ui_menu_close();
    ui_hud_set_chrome(false);
    behaviour_set_state(SPIDER_IDLE);
    g.spider_x = 176;
    g.spider_y = 240;
    g.spider_heading = 0;
    g.hunger = 50;
    g.energy = 80;
    g.gift_mode = 0;
    behaviour_sync_position();
    bool ok = prey_hunt_test(test_hunt_pointer);
    behaviour_set_state(SPIDER_IDLE);
    for (int i = 0; i < MAX_POOP; i++)
        if (ui.poop[i] && ui.poop[i] != poop[i]) {
            lv_obj_delete(ui.poop[i]);
            ui.poop[i] = poop[i];
        }
    g = original;
    behaviour_sync_position();
    spider_launch_pounce(0, 0);
    ok = ok && prey_count() == original_prey && !memcmp(&g, &original, sizeof g);
    printf("%s hunttest restored=%d\n", ok ? "OK" : "ERR",
           !memcmp(&g, &original, sizeof g));
    return ok;
}

/* Called from game_tick_render(), on the LVGL task. */
static void ui_service_request(void)
{
    static bool scanloop_paused, scanloop_chrome;
    int req = s_ui_req;
    if (req <= GAME_UI_NONE) return;

    switch ((game_ui_action_t)req) {
    case GAME_UI_SCREEN_CAPTURE:
        if (s_capture) lv_draw_buf_destroy(s_capture);
        s_capture = capture_target(s_ui_value);
        atomic_store(&s_ui_ok, s_capture != NULL);
        break;
    case GAME_UI_SCREEN_SD:
        atomic_store(&s_ui_ok, game_save_screenshot());
        break;
    case GAME_UI_SCREEN_TEST:
        atomic_store(&s_ui_ok, capture_test());
        break;
    case GAME_UI_SCREEN_RELEASE:
        if (s_capture) lv_draw_buf_destroy(s_capture);
        s_capture = NULL;
        break;
    case GAME_UI_FEED:
        atomic_store(&s_ui_ok, prey_count() < MAX_PREY);
        game_on_feed(NULL);
        break;
    case GAME_UI_CLEAN: {
        /* Success means the tank actually changed, not that XP was paid: a tank
         * holding only bugs is cleaned for 0 XP, and reporting that as a
         * failure would contradict what the player just saw. */
        bool needed = needs_clean_needed();
        needs_clean();
        atomic_store(&s_ui_ok, needed);
        break;
    }
    case GAME_UI_PLAY:  game_on_play(NULL);  break;
    case GAME_UI_WAKE: power_wake(); break;
    case GAME_UI_SLEEP: game_on_sleep(NULL); break;
    case GAME_UI_DEEP_SLEEP:
        atomic_store(&s_ui_ok, power_request_deep_sleep((uint32_t)s_ui_value));
        break;
    case GAME_UI_MENU:  game_on_menu(NULL);  break;
    case GAME_UI_SAVE:  atomic_store(&s_ui_ok, game_save_now()); break;
    case GAME_UI_TEST_GIFTS: s_ui_ok = test_gifts(); break;
    case GAME_UI_REFRESH:
        spider_refresh_colours();
        terrarium_rebuild();
        break;
    case GAME_UI_RENDER: cspider_render(game_now()); break;
    case GAME_UI_SD_BACKUP:
        atomic_store(&s_ui_ok, sd_request(SD_JOB_BACKUP));
        break;
    case GAME_UI_SD_RESTORE:
        atomic_store(&s_ui_ok, sd_request(SD_JOB_RESTORE));
        break;
    case GAME_UI_SPAWN:
        atomic_store(&s_ui_ok, s_ui_value < 0 ? prey_spawn() :
                     prey_spawn_species((prey_species_t)s_ui_value));
        break;
    case GAME_UI_SPECKLE:
        /* Creates LVGL objects, so it must run here on the LVGL task rather
         * than on the console task. */
        for (int i = 0; i < s_ui_value; i++) {
            terrarium_add_bug_poop(g_play.left + rand() % (g_play.right - g_play.left),
                                   g_play.top + rand() % (g_play.bottom - g_play.top));
        }
        ESP_LOGI(TAG, "speckle: added %d, poop=%d dirt=%d", s_ui_value,
                 terrarium_poop_count(), terrarium_dirt());
        atomic_store(&s_ui_ok, true);
        break;
    case GAME_UI_STATE: behaviour_set_state((spider_state_t)s_ui_value); break;
    case GAME_UI_SET_HUNGER:  g.hunger = (uint8_t)s_ui_value; break;
    case GAME_UI_SET_MOOD:    g.mood = (uint8_t)s_ui_value; break;
    case GAME_UI_SET_ENERGY:  g.energy = (uint8_t)s_ui_value; break;
    case GAME_UI_SET_HYGIENE: g.hygiene = (uint8_t)s_ui_value; break;
    case GAME_UI_SET_BOND:    g.bond = (uint8_t)s_ui_value; break;
    case GAME_UI_SET_SICK:    g.sick = s_ui_value != 0; break;
    case GAME_UI_SET_AUTO_FEED:
        if (g.auto_feed != s_ui_value) on_settings_pick(6);
        break;
    case GAME_UI_SET_AUTO_HUNT:
        if (g.auto_hunt != s_ui_value) on_settings_pick(6);
        break;
    case GAME_UI_SET_VOLUME:
        g.volume = (uint8_t)s_ui_value;
        audio_set_volume(g.volume);
        game_volume_changed();
        break;
    case GAME_UI_SET_MUSIC:
        g.music_enabled = s_ui_value != 0;
        audio_set_music(g.music_enabled);
        game_save_now();
        break;
    case GAME_UI_SET_MUSIC_VOLUME:
        g.music_volume = (uint8_t)s_ui_value;
        audio_set_music_volume(g.music_volume);
        game_save_now();
        break;
    case GAME_UI_SET_MUSIC_TRACK:
        g.music_track = (uint8_t)s_ui_value;
        audio_set_music_track(g.music_track);
        game_save_now();
        break;
    case GAME_UI_SET_MUTED:
        audio_set_muted(s_ui_value != 0);
        game_save_now();
        break;
    case GAME_UI_ADD_SILK: {
        uint32_t before = g.silk;
        if ((uint32_t)s_ui_value > UINT32_MAX - before) {
            ESP_LOGW(TAG, "Silk grant rejected: balance would overflow");
            atomic_store(&s_ui_ok, false);
            break;
        }
        game_add_silk((uint32_t)s_ui_value);
        bool queued = game_save_now();
        if (!queued) g.silk = before;
        atomic_store(&s_ui_ok, queued);
        break;
    }
    case GAME_UI_BORDER:
        atomic_store(&s_ui_ok, border_set(s_ui_value));
        terrarium_rebuild();
        break;
    case GAME_UI_BACKGROUND:
        atomic_store(&s_ui_ok, theme_set(s_ui_value));
        if (atomic_load(&s_ui_ok)) terrarium_rebuild();
        break;
    case GAME_UI_TEST_HUNT: atomic_store(&s_ui_ok, test_hunt()); break;
    case GAME_UI_TEST_GAIT: {
        bool ok = cspider_gait_test();
        printf("%s gaittest cadence=20/30/60 repeated=100 front_poses=10240\n",
               ok ? "OK" : "ERR");
        atomic_store(&s_ui_ok, ok);
        break;
    }
    case GAME_UI_TEST_PREY: {
        bool ok = prey_progression_test();
        printf("%s preytest levels=1,2,4,6,10,13,16,20 spawn_gates=%d\n",
               ok ? "OK" : "ERR", ok);
        atomic_store(&s_ui_ok, ok);
        break;
    }
    case GAME_UI_SETUP_DONE: {
        /* Test-only: finish the naming screen without tapping, so the tank can
         * be captured on a freshly erased save. */
        ESP_LOGI(TAG, "setupdone: naming and loading the tank");
        if (!g.name[0]) snprintf(g.name, sizeof g.name, "Test Spider");
        game_save_now();
        lv_screen_load(ui.screen);
        ESP_LOGI(TAG, "setupdone: tank loaded");
        atomic_store(&s_ui_ok, true);
        break;
    }
    case GAME_UI_DECOR_EDIT: {
        bool ok = true;
        int value = s_decor_edit.value;
        switch (s_decor_edit.operation) {
        case DECOR_EDIT_STATUS: break;
        case DECOR_EDIT_CLEAR: decor_clear(); break;
        case DECOR_EDIT_PLACE:
            if (value < 0 || value >= DECOR_COUNT) { ok = false; break; }
            if (!decor_owned(value)) {
                uint32_t silk = g.silk, level = g.level;
                g.silk = decor_def(value)->cost;
                g.level = decor_def(value)->unlock_level;
                ok = decor_buy(value);
                g.silk = silk; g.level = level;
            }
            if (ok) {
                s_decor_edit.count = decor_place(value, s_decor_edit.x, s_decor_edit.y);
                ok = s_decor_edit.count >= 0;
            }
            break;
        case DECOR_EDIT_MOVE: ok = decor_move(value, s_decor_edit.x, s_decor_edit.y); break;
        case DECOR_EDIT_BLOCK: ok = decor_toggle_blocking(value); break;
        }
        if (s_decor_edit.operation != DECOR_EDIT_PLACE) s_decor_edit.count = decor_placed_count();
        if (ok && s_decor_edit.operation != DECOR_EDIT_STATUS) {
            terrarium_refresh_decor();
            ok = game_save_now();
        }
        if (!ok) ESP_LOGE(TAG, "decor edit failed operation=%u value=%d", s_decor_edit.operation, value);
        atomic_store(&s_ui_ok, ok);
        break;
    }
    case GAME_UI_DECOR_TEST: {
        /* Exercise the real render path: place one of every kind, confirm the
         * layer matches, then restore the player's enclosure. */
        decor_item_t saved[DECOR_SLOTS];
        memcpy(saved, g.decor, sizeof saved);
        uint32_t extra_count = g.decor_extra_count, extra_owned = g.decor_owned_extra;
        uint8_t high_owned = g.catalogue_owned_high;
        decor_item_t *extra = decor_detach_extra();
        uint32_t owned = g.decor_owned;
        uint32_t silk = g.silk;
        uint32_t level = g.level;
        decor_clear();
        g.decor_owned = 0xFFFFFFFFu;
        g.decor_owned_extra = 0xFFFFFFFFu;
        g.catalogue_owned_high |= 0xF0;
        g.level = 20;
        g.silk = 100000;
        bool ok = true;
        for (int i = 0; i < DECOR_COUNT; i++) {
            int slot = decor_place((uint8_t)i, (int16_t)(60 + i * 30),
                                   (int16_t)(120 + (i % 3) * 90));
            ok = ok && slot >= 0;
        }
        terrarium_refresh_decor();
        ok = ok && terrarium_test();
        memcpy(g.decor, saved, sizeof saved);
        decor_restore_extra(extra);
        g.decor_extra_count = extra_count;
        g.decor_owned_extra = extra_owned;
        g.catalogue_owned_high = high_owned;
        g.decor_owned = owned;
        g.silk = silk;
        g.level = level;
        terrarium_refresh_decor();
        atomic_store(&s_ui_ok, ok);
        break;
    }
    case GAME_UI_SCANLOOP_BEGIN:
        scanloop_paused = s_paused;
        scanloop_chrome = ui_hud_chrome_visible();
        behaviour_aim_cancel();
        ui_menu_close();
        ui_hud_set_chrome(false);
        game_pause();
        break;
    case GAME_UI_SCANLOOP_END:
        ui_hud_set_chrome(scanloop_chrome);
        if (!scanloop_paused) game_resume();
        break;
    case GAME_UI_TEST_UI: {
        ui_menu_close();
        ui_hud_set_chrome(true);
        bool menu_ok = ui_menu_test();
        bool hud_ok = ui_hud_test();
        bool backgrounds_ok = terrarium_test();
        bool hunger_ok = needs_hunger_test();
        bool power_ok = power_ui_test();
        bool save_ok = save_format_test();
        bool decor_ok = decor_test();
        bool pointer_ok = test_furniture_pointer();
        bool original_muted = audio_muted();
        uint8_t original_music = g.music_enabled;
        s_testing_placement = true;
        on_sound_pick(1);
        bool sound_ok = audio_muted() != original_muted &&
                        g.music_enabled == original_music &&
                        !strcmp(s_menu_items[1].label, "Sound");
        on_sound_pick(1);
        sound_ok = sound_ok && audio_muted() == original_muted &&
                   g.music_enabled == original_music;
        uint8_t original_music_volume = g.music_volume;
        uint8_t original_track = g.music_track;
        uint8_t original_volume = audio_volume();
        on_volume_slider(3, 35);
        on_soundtrack_pick(AUDIO_MUSIC_TRACK_COUNT - 1);
        bool music_ok = g.music_volume == 35 && audio_music_volume() == 35 &&
                        g.music_track == AUDIO_MUSIC_TRACK_COUNT - 1 &&
                        audio_music_track() == AUDIO_MUSIC_TRACK_COUNT - 1 &&
                        ui_menu_page() == (AUDIO_MUSIC_TRACK_COUNT - 1) / 4 &&
                        audio_volume() == original_volume;
        g.music_volume = original_music_volume;
        g.music_track = original_track;
        audio_set_music_volume(original_music_volume);
        audio_set_music_track(original_track);
        menu_open(MENU_SD);
        bool sd_feedback = !strcmp(s_menu_items[2].label, "SD Status") &&
                           !strcmp(s_menu_items[2].detail, s_sd_message ? s_sd_message : sd_status_text());
        menu_open(MENU_MAIN);
        bool borders_route = ui_menu_test_pick(MENU_BORDERS) && ui_menu_visible() &&
                             s_open_menu == MENU_BORDERS && !strcmp(s_menu_items[0].label, "No Border");
        uint32_t original_silk = g.silk;
        g.silk = 0;
        menu_open(MENU_SHOP);
        bool shop_gates = !s_menu_items[0].enabled && !ui_menu_test_pick(0);
        ui_menu_set_page(1);
        on_shop_pick(TREAT_COUNT - 1);
        shop_gates = shop_gates && ui_menu_visible() && ui_menu_page() == (TREAT_COUNT - 1) / 6 && g.silk == 0;
        g.silk = original_silk;
        menu_open(MENU_ABOUT);
        bool help_routes = ui_menu_test_pick(3) && s_open_menu == MENU_OTA;
        menu_open(MENU_MAIN);
        help_routes = help_routes && ui_menu_test_pick(MENU_HELP) && s_open_menu == MENU_HELP &&
                      ui_menu_test_pick(0) && ui_menu_visible();
        back_to_help();
        for (int i = 0; i < HELP_TOPIC_COUNT; i++) {
            on_help_pick(i);
            help_routes = ui_menu_test_text_fits() && help_routes;
        }
        game_state_t original_state = g;
        g.achievements = 0; g.xp = 0; g.level = 1; g.silk = 0;
        bool rewards = true;
        for (int i = 0; i < ACHIEVEMENT_COUNT; i++) {
            uint32_t prior = g.silk;
            game_award(ACHIEVEMENTS[i].bit);
            uint32_t paid = g.silk;
            game_award(ACHIEVEMENTS[i].bit);
            rewards = rewards && (g.achievements & ACHIEVEMENTS[i].bit) &&
                      paid >= prior && paid == g.silk;
        }
        rewards = rewards && g.silk == ACHIEVEMENT_COUNT * ACHIEVEMENT_SILK &&
                  g.xp == ACHIEVEMENT_COUNT * XP_ACHIEVEMENT;
        g.achievements = 0; g.stage = STAGE_SPIDERLING; g.age_ticks = 2 * 2880;
        needs_update_stage();
        bool molt = g.stage == STAGE_JUVENILE && g.state == SPIDER_MOLT &&
                    (g.achievements & ACH_FIRST_MOLT);
        behaviour_tick(game_now() + 801u, .05f);
        molt = molt && g.state != SPIDER_MOLT;
        g = original_state;
        behaviour_set_state(SPIDER_IDLE);
        g.state = original_state.state;
        s_testing_placement = false;
        ui_menu_close();
        gift_t original_gift = g.gift;
        g.gift.used = 1;
        g.gift.x = 0;
        g.gift.y = ui.H;
        keep_gift_in_playfield();
        bool gift_bounds = g.gift.x == g_play.left && g.gift.y == g_play.bottom;
        g.gift = original_gift;
        printf("%s uitest pages=%d keyboard=%d editor=%d hud=%d backgrounds=%d hunger=%d power=%d save=%d decor=%d pointer=%d bounds=%d sound=%d music=%d sd_feedback=%d borders=%d shop_gates=%d help=%d rewards=%d molt=%d\n",
               menu_ok && hud_ok && backgrounds_ok && gift_bounds && hunger_ok &&
               power_ok && save_ok && decor_ok && pointer_ok && sound_ok && music_ok && sd_feedback &&
               borders_route && shop_gates && help_routes && rewards && molt ? "OK" : "ERR",
               menu_ok, menu_ok, menu_ok, hud_ok, backgrounds_ok, hunger_ok,
               power_ok, save_ok, decor_ok, pointer_ok, gift_bounds, sound_ok, music_ok, sd_feedback,
               borders_route, shop_gates, help_routes, rewards, molt);
        break;
    }
    default: break;
    }
    s_ui_req = GAME_UI_NONE;
}

/* Open a menu by name from the console, marshalled onto the LVGL task.
 *
 * Opening a menu creates and deletes LVGL objects. Doing that from the console
 * task corrupted LVGL's event list and crashed with "LoadProhibited in
 * lv_event_mark_deleted" -- the same class of bug as calling lv_timer_pause()
 * from a worker. So the request is posted and the render tick performs it. */
static char s_ui_menu_name[16];
static atomic_bool s_ui_menu_req;
static atomic_bool s_ui_menu_done;
static atomic_bool s_ui_menu_ok;

bool game_ui_open_menu(const char *name)
{
    if (!name || strlen(name) >= sizeof s_ui_menu_name || s_ui_menu_req) {
        ESP_LOGW(TAG, "menu request rejected: invalid name or busy");
        return false;
    }

    strncpy(s_ui_menu_name, name, sizeof s_ui_menu_name - 1);
    s_ui_menu_name[sizeof s_ui_menu_name - 1] = '\0';

    s_ui_menu_done = false;
    s_ui_menu_ok   = false;
    s_ui_menu_req  = true;

    /* Bounded wait, so a stalled render timer cannot hang the console. */
    for (int i = 0; i < 100 && !s_ui_menu_done; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (!s_ui_menu_done) ESP_LOGW(TAG, "menu acknowledgement timed out");
    return s_ui_menu_done && s_ui_menu_ok;
}
/* Called from game_tick_render(), on the LVGL task. */
static void ui_service_menu_request(void)
{
    if (!s_ui_menu_req) return;

    s_ui_menu_req = false;

    const char *name = s_ui_menu_name;

    if (!strcmp(name, "gift")) {
        ui_hud_gift_menu_show();
        s_ui_menu_ok = true;
    } else if (!strcmp(name, "chrome")) {
        ui_hud_set_chrome(true);
        s_ui_menu_ok = true;
    } else if (!strcmp(name, "nochrome")) {
        ui_hud_set_chrome(false);
        s_ui_menu_ok = true;
    } else if (!strncmp(name, "item", 4)) {
        /* "item N" opens the per-item decor menu for slot N. */
        int slot = atoi(name + 4);
        s_ui_menu_ok = game_ui_open_decor_item(slot);
    } else {
        s_ui_menu_ok = game_open_menu(name);
    }

    s_ui_menu_done = true;
}

/* Slingshot: press starts a pull, move updates it, release fires.
 *
 * The chrome check is essential and is NOT redundant with the one in the tap
 * handler. LVGL dispatches PRESSED before CLICKED, so without this the press
 * started a drag, the click then dismissed the chrome, and the RELEASED handler
 * fired the shot -- so "tap anywhere to dismiss" also pounced. Gating here
 * means the dismissing tap never begins a pull in the first place. */
void game_on_terrarium_press(lv_event_t *e)
{
    (void)e;
    s_tap_valid = false;
    if (ui_hud_chrome_visible() || ui_menu_visible() ||
        game_paused() || g.gift_mode || g_decor_mode) cancel_enclosure_tap();
    else {
        lv_indev_t *indev = lv_indev_active();
        if (indev) {
            lv_indev_get_point(indev, &s_tap_origin);
            s_tap_valid = true;
        }
    }
    if (ui_hud_chrome_visible() || ui_menu_visible() || game_paused()) return;
    if (g.gift_mode || g_decor_mode) {
        lv_indev_t *indev = lv_indev_active();
        if (!indev) return;
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        int reach = g.gift_mode ? 40 : decor_def(g_decor_selected)->radius * 2 + 12;
        bool grabbed = abs(p.x - s_place_x) <= reach && abs(p.y - s_place_y) <= reach;
        s_place_dx = grabbed ? s_place_x - p.x : 0;
        s_place_dy = grabbed ? s_place_y - p.y : 0;
        s_place_dragging = true;
        placement_position(p.x + s_place_dx, p.y + s_place_dy);
        return;
    }
    if (!behaviour_aim_active()) return;

    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p = {0, 0};
    if (indev) lv_indev_get_point(indev, &p);

    behaviour_aim_drag_begin(p.x, p.y);
}

void game_on_terrarium_pressing(lv_event_t *e)
{
    (void)e;
    lv_indev_t *active = lv_indev_active();
    if (s_tap_valid && active) {
        lv_point_t p;
        lv_indev_get_point(active, &p);
        int dx = p.x - s_tap_origin.x, dy = p.y - s_tap_origin.y;
        int distance = dx * dx + dy * dy;
        if (distance > ENCLOSURE_TAP_SLOP * ENCLOSURE_TAP_SLOP ||
            (behaviour_aim_dragging() && distance >= AIM_MIN_PULL_PX * AIM_MIN_PULL_PX)) {
            s_tap_valid = false;
            cancel_enclosure_tap();
        }
    }
    if (ui_hud_chrome_visible() || ui_menu_visible() || game_paused()) {
        behaviour_aim_cancel();
        return;
    }
    if (g.gift_mode || g_decor_mode) {
        lv_indev_t *indev = lv_indev_active();
        if (s_place_dragging && indev) {
            lv_point_t p;
            lv_indev_get_point(indev, &p);
            placement_position(p.x + s_place_dx, p.y + s_place_dy);
        }
        return;
    }
    if (!behaviour_aim_dragging()) return;
    lv_indev_t *indev = lv_indev_active();
    lv_point_t p = {0, 0};
    if (indev) lv_indev_get_point(indev, &p);

    behaviour_aim_drag_move(p.x, p.y);
}

void game_on_terrarium_release(lv_event_t *e)
{
    (void)e;
    if (g.gift_mode || g_decor_mode) {
        game_on_terrarium_pressing(e);
        s_place_dragging = false;
        return;
    }
    if (ui_hud_chrome_visible() || ui_menu_visible() || game_paused() || g.gift_mode ||
        g_decor_mode || !behaviour_aim_dragging()) {
        behaviour_aim_cancel();
        return;
    }

    if (behaviour_aim_release()) {
        game_cancel_pending_tap();
        g.taps++;
    }
}

/* Hold an item to edit it directly over the enclosure, without a submenu. */
void game_on_terrarium_long_press(lv_event_t *e)
{
    (void)e;
    s_tap_valid = false;
    cancel_enclosure_tap();
    if (ui_hud_chrome_visible() || ui_menu_visible() || game_paused() ||
        g.gift_mode || g_decor_mode) return;

    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p = {0, 0};
    lv_indev_get_point(indev, &p);

    int slot = decor_hit_test((int16_t)p.x, (int16_t)p.y);
    bool canopy = slot >= 0 && decor_at(slot)->blocking == DECOR_WALK_BEHIND;
    if (!canopy && g.gift.used && abs(p.x - g.gift.x) <= 32 && abs(p.y - g.gift.y) <= 32) {
        behaviour_aim_cancel();
        on_gift_item_pick(0);
        s_place_dragging = true;
        s_place_dx = s_place_x - p.x;
        s_place_dy = s_place_y - p.y;
        return;
    }
    if (slot < 0) return;

    /* A long press must not also fire the pounce on release. */
    behaviour_aim_cancel();
    s_decor_item_slot = slot;
    decor_begin_move(slot);
    s_place_dragging = true;
    s_place_dx = s_place_x - p.x;
    s_place_dy = s_place_y - p.y;
}

void game_on_terrarium_tap(lv_event_t *e)
{
    (void)e;
    if (ui_menu_visible()) return;
    lv_indev_t *indev = lv_indev_active();
    lv_point_t p = {0, 0};
    if (indev) lv_indev_get_point(indev, &p);

    if (g_decor_mode || g.gift_mode || game_paused()) {
        cancel_enclosure_tap();
        return;
    }
    if (ui_hud_chrome_visible()) {
        cancel_enclosure_tap();
        ui_hud_dismiss_if_visible();
        return;
    }
    if (indev) {
        int dx = p.x - s_tap_origin.x, dy = p.y - s_tap_origin.y;
        int distance = dx * dx + dy * dy;
        if (!s_tap_valid || distance > ENCLOSURE_TAP_SLOP * ENCLOSURE_TAP_SLOP ||
            (behaviour_aim_active() && distance >= AIM_MIN_PULL_PX * AIM_MIN_PULL_PX)) {
            cancel_enclosure_tap();
            return;
        }
        dx = p.x - s_last_tap.x; dy = p.y - s_last_tap.y;
        uint32_t now = game_now();
        if (s_tap_pending && now - s_last_tap_at <= ENCLOSURE_DOUBLE_TAP_MS &&
            dx * dx + dy * dy <= ENCLOSURE_DOUBLE_TAP_SLOP * ENCLOSURE_DOUBLE_TAP_SLOP) {
            enclosure_double_tap();
            return;
        }
        s_last_tap = p;
        s_last_tap_at = now;
        s_tap_pending = true;
    }

    /* Care has its own hit target; edge drags no longer reveal a tray. */

    /* Remember where the player touched: a bonded spider turns toward it. */
    if (indev) behaviour_note_tap(p.x, p.y);

    /* 2. Aiming is handled by the press/move/release handlers, not by a click.
     * A click here means the player tapped without dragging, which the release
     * handler treats as a cancel. */
    if (behaviour_aim_active()) {
        return;
    }

    /* 3. A tap on the spider itself is a pet.
     *
     * Petting was removed once because it competed with the slingshot for the
     * same gesture. The ambiguity is resolved by requiring the tap to land on
     * the spider's body: a pull that starts on the spider still fires, because
     * the release handler only treats a drag past AIM_MIN_PULL_PX as a shot and
     * a click only arrives when the finger did not drag. */
    if (indev) {
        s_pet_tap = p;
        if (s_pet_tap_timer) lv_timer_delete(s_pet_tap_timer);
        s_pet_tap_timer = lv_timer_create(delayed_pet, ENCLOSURE_DOUBLE_TAP_MS + 25, NULL);
        lv_timer_set_repeat_count(s_pet_tap_timer, 1);
        return;
    }

    /* 4. Empty tap: nothing happens, and nothing is said.
     *
     * The player explicitly asked for the on-screen hints to go. "tap the
     * spider" and "wait for TAP!" were instructions, not feedback, and they
     * appeared on every stray tap. Silence is the correct response to a tap
     * that hit nothing. */
    g.taps++;
}

/* ------------------------------------------------------------------ */
/* Timers                                                              */
/* ------------------------------------------------------------------ */
void game_tick_logic(lv_timer_t *timer)
{
    (void)timer;
    /* Only a genuine pause stops the clock. Menus, the care HUD, a locked
     * screen and the first-run setup screen used to return here, which froze
     * hunger and hygiene whenever the player left the device on a menu, with
     * the screen off, or before naming the spider. Needs must keep advancing;
     * only the parts that need a visible, interactive tank are skipped below. */
    if (s_paused || save_restore_pending() || net_busy() || power_sleep_pending()) {
        s_last_logic = lv_tick_get();
        return;
    }

    bool screen_off = power_sleeping() || buttons_locked();
    bool tank_hidden = screen_off || ui_menu_visible() || ui_hud_chrome_visible() ||
                       lv_screen_active() != ui.screen;

    uint32_t now = lv_tick_get();
    float dt = (float)(now - s_last_logic) / 1000.0f;
    s_last_logic = now;

    /* Clamp dt so a long stall cannot teleport the spider. */
    if (dt <= 0.0f || dt > 0.25f) dt = 1.0f / (float)LOGIC_HZ;

    /* The gift bonus is global, but recompute each tick so replacing or
     * removing the gift takes effect immediately. */
    gift_compute();

    /* WiFi reconnect backoff. Cheap when connected or off. */
    net_tick(now);
    audio_tick(now);

    /* The radio is NOT started automatically.
     *
     * Bringing WiFi up allocates a large block of internal DMA-capable RAM for
     * its RX/TX pools. The LCD SPI driver needs the same pool for every frame,
     * and the two cannot coexist: starting WiFi at boot produced a steady
     * stream of "Draw bitmap failed: ESP_ERR_NO_MEM" and a corrupted display.
     *
     * Deferring it to the first tick was not enough -- the allocation is
     * permanent, so the display starves from then on regardless of when it
     * happens. The radio is therefore started only when the player opens the
     * WiFi menu, which is the only time it is needed. */

    /* The mind decides what the spider WANTS; behaviour decides what it can
     * actually do. Keeping them separate means the intent layer can never
     * break the safety guarantees in the FSM. */
    if (!tank_hidden) mind_tick(now);

    /* Stamp the wall clock so a power-off or a dead battery is measurable at
     * the next boot. Cheap: one RTC read per minute, not per tick. */
    static uint32_t next_stamp;
    if ((int32_t)(now - next_stamp) >= 0) {
        next_stamp = now + 60000u;
        time_t utc = 0;
        if (pet_rtc_read(&utc) == ESP_OK && utc > 0) g.last_seen = (uint32_t)utc;
    }

    needs_tick(now, dt);
    game_check_achievements();
    buttons_tick(now);
    if (!tank_hidden) {
        behaviour_tick(now, dt);
        prey_tick(now, dt);
    }

    /* A bonded spider contributes Silk, not free items that bypass the shop. */
    static uint32_t next_gift;
    if (g.bond >= BOND_GIFT && !g.gift.used) {
        if (next_gift == 0) {
            next_gift = now + 120000u + (uint32_t)(rand() % 120000);
        } else if (now >= next_gift) {
            next_gift = now + 120000u + (uint32_t)(rand() % 120000);
            game_add_silk(2);
            audio_play(SFX_TAP);
            ESP_LOGI(TAG, "bond reward: +2 Silk");
        }
    } else if (g.bond < BOND_GIFT) {
        next_gift = 0;
    }

    if (now - s_last_save >= SAVE_PERIOD_MS) {
        s_last_save = now;
        game_save_now();
    }

    /* Heartbeat: makes an unattended soak verifiable from the serial log. */
    static uint32_t last_beat;
    if (now - last_beat >= 30000u) {
        last_beat = now;
        const gift_bonus_t *gb = gift_bonus();
        ESP_LOGI(TAG, "hb state=%u pos=(%d,%d) h=%u m=%u e=%u hy=%u prey=%d sick=%u lvl=%lu aim=%d streak=%u bond=%u temp=%s gift=%s eR=%.2f hD=%.2f pR=%.2f aW=%.2f",
                 (unsigned)g.state, g.spider_x, g.spider_y,
                 g.hunger, g.mood, g.energy, g.hygiene,
                 prey_count(), g.sick, (unsigned long)g.level,
                 behaviour_aim_dragging() ? 1 : 0, (unsigned)g.streak,
                 (unsigned)g.bond, game_temperament()->name,
                 gift_present() ? gift_kind_name(gift_kind()) : "none",
                 gb->energy_regen_mult, gb->hygiene_decay_mult,
                 gb->pounce_range_mult, gb->aim_window_mult);
    }
}

void game_tick_render(lv_timer_t *timer)
{
    power_tick();
    static bool save_failure_shown;
    if (!save_busy() && !save_last_ok() && !save_failure_shown) {
        ui_hud_bubble("Save Failed / See Log", 0xf5c86b, 2400);
        save_failure_shown = true;
    } else if (save_last_ok()) save_failure_shown = false;
    (void)timer;

    uint32_t now = lv_tick_get();

    /* Perform any UI action the serial console asked for. This runs on the LVGL
     * task, which is the only thread allowed to touch lv_*. */
    ui_service_request();
    ui_service_menu_request();
    /* Refresh the WiFi menu when a radio job finishes.
     *
     * The scan runs on the net task and the UI never waits for it, so the menu
     * has to be rebuilt once the results are in. This is the poll that closes
     * the loop: while net_busy() the menu shows "scanning...", and the moment
     * it clears the list is rebuilt with the networks found.
     *
     * Keyed on net_generation() rather than the busy edge, because rebuilding
     * the menu calls net_init() -- an edge test would re-trigger itself. */
    /* The menu can be closed from inside ui_menu.c (BACK, or a row that
     * dismisses it), so the tracker is resynced here rather than at every
     * close site. */
    if (!ui_menu_visible()) s_open_menu = -1;

    static uint32_t seen_gen;
    uint32_t gen = net_generation();
    if (gen != seen_gen) {
        seen_gen = gen;
        /* Preserve navigation while refreshing network-dependent actions. */
        if (s_open_menu == MENU_WIFI) menu_open(MENU_WIFI);
        else if (s_open_menu == MENU_WIFI_SCAN) menu_open(MENU_WIFI_SCAN);
        else if (s_open_menu == MENU_OTA) menu_open(MENU_OTA);
        else if (s_open_menu == MENU_SETTINGS) {
            int page = ui_menu_page();
            menu_open(MENU_SETTINGS);
            ui_menu_set_page(page);
        }
    }
    static uint32_t seen_sd_gen;
    uint32_t sd_gen = sd_generation();
    if (sd_gen != seen_sd_gen) {
        seen_sd_gen = sd_gen;
        if (s_sd_capture && !sd_busy()) {
            lv_draw_buf_destroy(s_sd_capture);
            s_sd_capture = NULL;
        }
        if (sd_take_restored(&g)) {
            audio_set_volume(g.volume);
            audio_set_music_volume(g.music_volume);
            audio_set_music_track(g.music_track);
            audio_set_music(g.music_enabled);
            apply_enclosure_lights();
            state_finish_load();
            keep_gift_in_playfield();
            audio_set_muted(g.muted != 0);
            spider_refresh_colours();
            terrarium_rebuild();
            terrarium_refresh_gift();
        }
        if (s_open_menu == MENU_SD) menu_open(MENU_SD);
        else if (s_open_menu == MENU_SETTINGS && ui_menu_visible()) {
            int page = ui_menu_page();
            menu_open(MENU_SETTINGS);
            ui_menu_set_page(page);
        }
    }

    if (s_paused || save_restore_pending() || power_sleep_pending() || power_sleeping() || net_busy() || ui_menu_visible() || ui_hud_chrome_visible() ||
        lv_screen_active() != ui.screen) {
        if (!s_paused && !net_busy() && !ui_menu_visible() &&
            lv_screen_active() == ui.screen) ui_hud_tick(now);
        return;
    }
    /* Re-read the clock: ui_service_request() above may have created objects
     * stamped with a later lv_tick_get(). Passing the older `now` to
     * terrarium_tick() made now - stamp negative, which wraps to a huge
     * unsigned value and expires the object on the very tick it was made. */
    now = lv_tick_get();
    terrarium_tick(now);
    cspider_render(now);
    spider_render(now);   /* aim visuals only */
    prey_render(now);
    ui_hud_tick(now);
}

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */

/* LVGL callers queue requests; the network worker owns pause and teardown. */
void game_ui_start_wifi_scan(void)
{
    if (!net_scan_start()) {
        ESP_LOGE(TAG, "WiFi scan failed to start");
    }
}

void game_ui_connect_wifi(int scan_index)
{
    if (scan_index < 0 || scan_index >= net_scan_count()) {
        ESP_LOGW(TAG, "invalid WiFi scan selection %d", scan_index);
        return;
    }

    on_wifi_pick((uint8_t)(scan_index + 4));
}

void game_ui_forget_wifi(void)
{
    if (!net_forget_saved(0)) ESP_LOGW(TAG, "forget WiFi rejected");
}

void game_ui_open_wifi_password_entry(void)
{
    ui_menu_open_input("Network Name (SSID)", "", false, 32, on_ssid);
    ui_menu_set_back(back_to_wifi);
}

void game_init(void)
{
    state_load();

    /* Seed the RNG: it was never seeded, so placement repeated every boot. */
    srand((unsigned int)(g.seed ^ (uint32_t)lv_tick_get()));

    /* Roll the temperament once, now that the RNG is seeded. A v1 save has no
     * temperament field, so the sentinel survives the load and this fires. */
    if (g.temperament >= TEMPERAMENT_COUNT) {
        g.temperament = (uint8_t)(rand() % TEMPERAMENT_COUNT);
        ESP_LOGI(TAG, "temperament: %s", game_temperament()->name);
    }

    /* bsp_display_start() already launched the LVGL worker task, which runs
     * lv_timer_handler() over the same object tree. Hold the adapter lock for
     * the whole UI build or we race it (Load access fault). */
    if (esp_lv_adapter_lock(-1) != ESP_OK) {
        ESP_LOGE(TAG, "game_init: failed to acquire LVGL adapter lock");
        return;
    }

    memset(&ui, 0, sizeof ui);
    ui.W = LV_HOR_RES;
    ui.H = LV_VER_RES;
    if (ui.W < 40) ui.W = 480;
    if (ui.H < 40) ui.H = 480;

    compute_playfield();

    /* --- terrarium screen -------------------------------------------- */
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, ui.W, ui.H);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x04050a), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_scrollable(scr, false);
    ui.screen = scr;
    /* Every UI layer hangs off the rotation wrapper so a quarter turn moves
     * the whole interface, not just the background. */
    lv_obj_t *root = power_rotation_layer(scr);

    /* Tap layer sits above the background but below the spider. */
    ui.tap_layer = lv_obj_create(root);
    lv_obj_remove_style_all(ui.tap_layer);
    lv_obj_set_size(ui.tap_layer, ui.W, ui.H);
    lv_obj_set_pos(ui.tap_layer, 0, 0);
    lv_obj_set_style_bg_opa(ui.tap_layer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui.tap_layer, 0, 0);
    lv_obj_set_scrollable(ui.tap_layer, false);
    lv_obj_set_clickable(ui.tap_layer, true);
    lv_obj_set_press_lock(ui.tap_layer, true);
    lv_obj_add_event_cb(ui.tap_layer, game_on_terrarium_tap, LV_EVENT_CLICKED, NULL);
    /* Slingshot needs the raw press/move/release stream, not just clicks. */
    lv_obj_add_event_cb(ui.tap_layer, game_on_terrarium_press,
                        LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(ui.tap_layer, game_on_terrarium_pressing,
                        LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(ui.tap_layer, game_on_terrarium_release,
                        LV_EVENT_RELEASED, NULL);
    /* Hold on a placed decor item to open its move/blocking menu. */
    lv_obj_add_event_cb(ui.tap_layer, game_on_terrarium_long_press,
                        LV_EVENT_LONG_PRESSED, NULL);

    terrarium_init(root);
    ui.creature_layer = lv_obj_create(root);
    lv_obj_remove_style_all(ui.creature_layer);
    lv_obj_set_size(ui.creature_layer, ui.W, ui.H);
    lv_obj_set_clickable(ui.creature_layer, false);
    lv_obj_set_scrollable(ui.creature_layer, false);

    /* The creature is a single canvas, not a scene graph. cspider draws the
     * whole spider into one 96x96 buffer each frame; spider_init() now only
     * creates the slingshot aim visuals. */
    cspider_init(ui.creature_layer);
    spider_init(ui.creature_layer);
    terrarium_raise_canopy();

    s_light_overlay = lv_obj_create(root);
    lv_obj_remove_style_all(s_light_overlay);
    lv_obj_set_size(s_light_overlay, ui.W, ui.H);
    lv_obj_set_style_bg_color(s_light_overlay, lv_color_hex(0x070c20), 0);
    lv_obj_set_style_bg_opa(s_light_overlay, 110, 0);
    lv_obj_set_clickable(s_light_overlay, false);
    lv_obj_set_scrollable(s_light_overlay, false);
    apply_enclosure_lights();
    ui_hud_init(root);

    /* The tap layer must sit BELOW the HUD, or it swallows every button press
     * and the buttons appear dead -- pressing FEED just ran the terrarium tap
     * handler, which fell through to "tap the spider". It is created before the
     * HUD, but LVGL's hit test walks children in reverse creation order, so
     * sending it to the back makes the ordering explicit and independent of
     * creation order. */
    lv_obj_move_background(ui.tap_layer);

    /* --- first-start setup -------------------------------------------- */
    ui_setup_init();

    /* --- audio -------------------------------------------------------- */
    audio_init();
    audio_set_music_volume(g.music_volume);
    audio_set_music_track(g.music_track);
    audio_set_music(g.music_enabled);

    /* Network worker and RTC were initialized before display startup. */
    net_init();
    sd_init();

    /* --- timers ------------------------------------------------------- */
    s_last_logic = lv_tick_get();
    s_last_save  = s_last_logic;

    s_logic_timer  = lv_timer_create(game_tick_logic,  LOGIC_PERIOD_MS,  NULL);
    s_render_timer = lv_timer_create(game_tick_render, RENDER_PERIOD_MS, NULL);

    /* A brand-new spider needs a name and a colour before the tank opens. */
    if (ui_setup_needed()) {
        ui_setup_show();
    } else {
        lv_screen_load(scr);
    }
    esp_lv_adapter_unlock();

    ESP_LOGI(TAG, "Spidarium ready (%ldx%ld) stage=%s level=%lu",
             (long)ui.W, (long)ui.H, needs_stage_name(),
             (unsigned long)g.level);

    /* Where the RAM went, once everything is up. This is the baseline the
     * budget in docs/memory_budget.md is written against. */
    mem_report_detail("boot complete");
}
