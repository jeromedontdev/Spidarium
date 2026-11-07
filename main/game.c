/*
 * Spidarium — game orchestrator.
 *
 * Owns the authoritative state and the two timers. All actual work lives in
 * the engine modules: terrarium (background), spider (rendering), behaviour
 * (movement/FSM), prey (hunting), needs (care/life cycle), ui_hud
 * (screens), audio (SFX), save (persistence).
 */
#include "game.h"

#include "assets.h"
#include "audio.h"
#include "behaviour.h"
#include "cspider.h"
#include "game_config.h"
#include "gift.h"
#include "meminfo.h"
#include "mind.h"
#include "needs.h"
#include "net.h"
#include "prey.h"
#include "save.h"
#include "sd.h"
#include "spider.h"
#include "test_console.h"
#include "terrarium.h"
#include "theme.h"
#include "ui_hud.h"
#include "ui_menu.h"
#include "ui_setup.h"

#include "esp_log.h"
#include "esp_lv_adapter.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "spidarium";

/* The single authoritative state. */
game_state_t g;
game_ui_t    ui;
playfield_t  g_play;

static uint32_t s_last_logic;
static uint32_t s_last_save;

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
}

void game_save_now(void)
{
    save_save(&g, sizeof g);
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
}

static void state_load(void)
{
    state_defaults();

    if (!save_load(&g, sizeof g)) {
        ESP_LOGI(TAG, "starting from defaults");
    }

    /* Runtime-only fields never survive a reboot. */
    g.state        = SPIDER_IDLE;
    g.eat_until_ms = 0;
    g.blink_until  = 0;
    g.blink_window = 0;
    g.gift_mode    = 0;

    /* Guard against a corrupt-but-valid-header blob. */
    if (g.level == 0) g.level = 1;
    if (g.stage > STAGE_ADULT) g.stage = STAGE_SPIDERLING;
    if (g.hunger > 100) g.hunger = 100;
    if (g.mood > 100) g.mood = 100;
    if (g.energy > 100) g.energy = 100;
    if (g.hygiene > 100) g.hygiene = 100;
}

/* ------------------------------------------------------------------ */
/* Playfield                                                           */
/* ------------------------------------------------------------------ */
static void compute_playfield(void)
{
    /* The whole screen, inset only by the spider's own visual radius. The old
     * field was UI_CORNER..W-UI_CORNER by HUD_TOP_H..H-HUD_BOTTOM_H, which
     * confined the spider to a narrow middle band and left most of the tank
     * unused. The HUD floats over the playfield, so the spider may pass behind
     * it -- that is what "the whole screen" means. */
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
}

/* ------------------------------------------------------------------ */
/* Event callbacks                                                     */
/* ------------------------------------------------------------------ */
void game_on_feed(lv_event_t *e)
{
    (void)e;
    g.taps++;
    needs_feed();
    /* No bubble: the spider reacting IS the feedback. */
}

void game_on_clean(lv_event_t *e)
{
    (void)e;
    g.taps++;
    needs_clean();
    game_save_now();
}

void game_on_play(lv_event_t *e)
{
    (void)e;
    g.taps++;
    needs_play();
    game_save_now();
}

void game_on_sleep(lv_event_t *e)
{
    (void)e;
    g.taps++;
    needs_sleep();
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
        g.gift_mode = 0;
        terrarium_preview_hide();
        ui_hud_set_gift_mode(false);
        return;
    }

    if (ui_hud_gift_menu_visible()) {
        ui_hud_gift_menu_hide();
        return;
    }

    ui_hud_gift_menu_show();
}

/* Called by the HUD when the player taps a kind in the picker. */
void game_gift_chosen(uint8_t kind)
{
    if (kind >= GIFT_KINDS) return;

    if (!gift_unlocked(kind)) {

        return;
    }

    g.gift_selected = kind;
    ui_hud_gift_menu_hide();

    /* Enter placement mode: the next tank tap decides where it goes. */
    g.gift_mode = 1;
    ui_hud_set_gift_mode(true);

    char buf[40];
    snprintf(buf, sizeof buf, "%s: tap to place", gift_kind_name(kind));

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
};

static void menu_open(int which);

/* Which menu is currently on screen, or -1 when none is. Tracked here because
 * ui_menu only knows that *a* menu is open, not which one. */
static int s_open_menu = -1;

/* Factory reset: wipe the save, put the state back to defaults, and drop the
 * player into the new-tank screen so they can start over. This exists so new
 * tanks can be retested without reflashing the device. */
static void do_factory_reset(void)
{
    save_erase();
    state_defaults();

    /* The live objects still carry the old spider's colours, so push the new
     * defaults into them before showing the setup screen. */
    spider_refresh_colours();
    terrarium_rebuild();

    ui_setup_show();
}

static void on_theme_pick(uint8_t id)
{
    if (!theme_set(id)) return;
    terrarium_rebuild();
    game_save_now();
    menu_open(MENU_THEME);   /* stay in the submenu so the choice is visible */
}

static void on_sound_pick(uint8_t id)
{
    audio_set_muted(id == 1);
    game_save_now();
    menu_open(MENU_SOUND);
}

static void on_main_pick(uint8_t id)
{
    switch (id) {
    case MENU_THEME:        menu_open(MENU_THEME);        break;
    case MENU_SOUND:        menu_open(MENU_SOUND);        break;
    case MENU_ACHIEVEMENTS: menu_open(MENU_ACHIEVEMENTS); break;
    case MENU_RESET:        menu_open(MENU_RESET);        break;
    case MENU_SETTINGS:     menu_open(MENU_SETTINGS);     break;
    default: break;
    }
}

/* Settings submenu: connectivity and storage. Both are infrastructure for OTA
 * and backups rather than gameplay, so they live behind their own entry rather
 * than cluttering the main list. */
static void on_settings_pick(uint8_t id)
{
    switch (id) {
    case 0:
        /* Entering the WiFi page starts a scan straight away.
         *
         * The player came here to find networks, so making them tap "scan" as
         * well is a second step for no reason. The menu opens showing
         * "scanning..." and rebuilds itself with the results when the job
         * finishes. */
        net_scan_start();
        menu_open(MENU_WIFI);
        break;
    case 1: menu_open(MENU_SD);   break;
    default: break;
    }
}

/* WiFi actions.
 *
 * Row 0 is "scan"; rows 1..N are the networks the scan found. Picking a
 * network connects to it. Open networks connect immediately; secured ones need
 * a password, which there is no on-screen flow for yet, so they are shown but
 * disabled with a "needs password" note rather than silently failing. */
static void on_wifi_pick(uint8_t id)
{
    if (id == 0) {
        net_scan_start();
        menu_open(MENU_WIFI);
        return;
    }

    int i = (int)id - 1;
    if (i >= 0 && i < net_scan_count()) {
        if (!net_scan_secure(i)) {
            net_connect_to(net_scan_ssid(i), "");
        }
    }
    menu_open(MENU_WIFI);
}

/* SD actions: backup and restore the save. */
static void on_sd_pick(uint8_t id)
{
    if (id == 0) {
        sd_backup_save();
    } else if (id == 1) {
        if (sd_restore_save()) {
            /* The restored blob is on disk; reload it into the live state. */
            state_load();
            spider_refresh_colours();
            terrarium_rebuild();
        }
    }
    menu_open(MENU_SD);
}

/* The reset submenu is a confirmation step: the destructive action is never one
 * tap away from the main list. */
static void on_reset_pick(uint8_t id)
{
    if (id == 0) do_factory_reset();
}

/* BACK from a submenu returns to the main menu; BACK from the main menu just
 * closes (ui_menu_set_back is not set there, so the menu closes itself). */
static void back_to_main(void)
{
    menu_open(MENU_MAIN);
}

static void menu_open(int which)
{
    static ui_menu_item_t items[16];

    /* Remember which menu is on screen so the render tick can tell whether the
     * WiFi list is still the one being looked at. Without this, a scan that
     * finishes while the player has navigated elsewhere would yank them back. */
    s_open_menu = which;

    switch (which) {
    case MENU_MAIN: {
        items[0] = (ui_menu_item_t){ "themes",       NULL, true, false, true, MENU_THEME };
        items[1] = (ui_menu_item_t){ "sound",        NULL, true, false, true, MENU_SOUND };
        items[2] = (ui_menu_item_t){ "achievements", NULL, true, false, true, MENU_ACHIEVEMENTS };
        items[3] = (ui_menu_item_t){ "settings",     NULL, true, false, true, MENU_SETTINGS };
        items[4] = (ui_menu_item_t){ "new tank",     NULL, true, false, true, MENU_RESET };
        ui_menu_open("menu", items, 5, on_main_pick);
        break;
    }

    case MENU_SETTINGS: {
        items[0] = (ui_menu_item_t){ "wifi", net_status_text(), true, false, true, 0 };
        items[1] = (ui_menu_item_t){ "sd card", sd_status_text(), true, false, true, 1 };
        ui_menu_open("settings", items, 2, on_settings_pick);
        ui_menu_set_back(back_to_main);
        break;
    }

    case MENU_WIFI: {
        int n = 0;

        /* A FRESH MENU PER STATE.
         *
         * The menu is rebuilt from scratch each time the state changes, rather
         * than being mutated in place. That is simpler to reason about and it
         * means the player always sees exactly one of three things: the scan
         * running, the networks found, or why the scan failed.
         *
         * net_init() only loads credentials and refreshes the status text; it
         * does not touch the radio. */
        net_init();

        if (net_busy()) {
            items[n++] = (ui_menu_item_t){ "scanning...", NULL,
                                           false, false, false, 0 };
            ui_menu_open("wifi", items, n, on_wifi_pick);
            ui_menu_set_back(back_to_main);
            break;
        }

        /* A failed scan says so. Silently showing an empty list is what made
         * this bug take several rounds: "found nothing" and "could not scan"
         * looked identical. */
        const char *err = net_scan_error();
        if (err[0]) {
            items[n++] = (ui_menu_item_t){ "scan failed", err,
                                           false, false, false, 0 };
        }

        items[n++] = (ui_menu_item_t){ "scan", net_status_text(),
                                       true, false, false, 0 };

        int found = net_scan_count();
        for (int i = 0; i < found && n < 15; i++) {
            const char *ssid = net_scan_ssid(i);
            if (!ssid[0]) continue;   /* hidden network */

            bool secure = net_scan_secure(i);
            items[n].label      = ssid;
            items[n].detail     = secure ? "needs password" : "open";
            /* Secured networks are listed but not selectable: there is no
             * password entry yet, and a row that silently does nothing is
             * worse than a row that says why. */
            items[n].enabled    = !secure;
            items[n].selected   = (strcmp(ssid, net_ssid()) == 0);
            items[n].is_submenu = false;
            items[n].id         = (uint8_t)(i + 1);
            n++;
        }

        ui_menu_open("wifi", items, n, on_wifi_pick);
        ui_menu_set_back(back_to_main);
        break;
    }

    case MENU_SD: {
        /* Mount on demand. Probing at boot stole DMA buffers from the LCD
         * driver and logged a burst of sdmmc errors on units with no card. */
        sd_mount();
        bool have = sd_present();
        items[0] = (ui_menu_item_t){ "back up save",
                                     have ? "to /sdcard" : "no card",
                                     have, false, false, 0 };
        items[1] = (ui_menu_item_t){ "restore save",
                                     sd_backup_exists() ? "from /sdcard" : "no backup",
                                     have && sd_backup_exists(), false, false, 1 };
        ui_menu_open("sd card", items, 2, on_sd_pick);
        ui_menu_set_back(back_to_main);
        break;
    }

    case MENU_RESET: {
        items[0] = (ui_menu_item_t){ "start a new tank", "erases this spider", true, false, false, 0 };
        ui_menu_open("new tank", items, 1, on_reset_pick);
        ui_menu_set_back(back_to_main);
        break;
    }

    case MENU_THEME: {
        for (int i = 0; i < THEME_COUNT; i++) {
            const theme_def_t *t = theme_get((uint8_t)i);
            items[i].label     = t->name;
            items[i].detail    = theme_unlocked((uint8_t)i) ? NULL : "locked";
            items[i].enabled   = theme_unlocked((uint8_t)i);
            items[i].selected  = (theme_current() == t);
            items[i].is_submenu = false;
            items[i].id        = (uint8_t)i;
        }
        ui_menu_open("themes", items, THEME_COUNT, on_theme_pick);
        ui_menu_set_back(back_to_main);
        break;
    }

    case MENU_SOUND: {
        items[0] = (ui_menu_item_t){ "sound on",  NULL, true, !audio_muted(), false, 0 };
        items[1] = (ui_menu_item_t){ "sound off", NULL, true,  audio_muted(), false, 1 };
        ui_menu_open("sound", items, 2, on_sound_pick);
        ui_menu_set_back(back_to_main);
        break;
    }

    case MENU_ACHIEVEMENTS: {
        /* Read-only list: the achievements are shown, not toggled. */
        /* "first pet" is gone: the pet mechanic was removed, so the
         * achievement could never be earned again. */
        static const char *const names[9] = {
            "first meal", "first clean", "first play", "first hunt",
            "first molt", "ten meals", "adulthood", "level 5",
            "survived sickness",
        };
        static const uint32_t bits[9] = {
            ACH_FIRST_MEAL, ACH_FIRST_CLEAN, ACH_FIRST_PLAY,
            ACH_FIRST_HUNT, ACH_FIRST_MOLT, ACH_TEN_MEALS, ACH_ADULT,
            ACH_LEVEL_5, ACH_SURVIVED_SICK,
        };
        for (int i = 0; i < 9; i++) {
            bool got = (g.achievements & bits[i]) != 0;
            items[i].label     = names[i];
            items[i].detail    = got ? "earned" : NULL;
            items[i].enabled   = false;   /* read-only */
            items[i].selected  = got;
            items[i].is_submenu = false;
            items[i].id        = (uint8_t)i;
        }
        ui_menu_open("achievements", items, 9, NULL);
        ui_menu_set_back(back_to_main);
        break;
    }

    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Pause                                                               */
/* ------------------------------------------------------------------ */
/* Stop the game while something exclusive runs.
 *
 * WiFi is the only user. The radio allocates internal DMA-capable RAM and runs
 * its own high-priority tasks; doing that while the 20 Hz logic tick and the
 * 30 Hz render tick are also running starved the LCD SPI driver and crashed the
 * device. Pausing both timers makes the network operation the only thing
 * happening, which is how WiFi and OTA were handled previously.
 *
 * The timers are paused rather than deleted, so resuming is cheap and the
 * spider's state is untouched. */
void game_pause(void)
{
    if (s_paused) return;
    s_paused = true;

    if (s_logic_timer)  lv_timer_pause(s_logic_timer);
    if (s_render_timer) lv_timer_pause(s_render_timer);

    ESP_LOGI(TAG, "game paused");
}

void game_resume(void)
{
    if (!s_paused) return;
    s_paused = false;

    /* Reset the delta baseline, or the first tick after a long pause sees a
     * huge dt and the spider teleports. */
    s_last_logic = lv_tick_get();

    if (s_logic_timer)  lv_timer_resume(s_logic_timer);
    if (s_render_timer) lv_timer_resume(s_render_timer);

    ESP_LOGI(TAG, "game resumed");
}

bool game_paused(void)
{
    return s_paused;
}

void game_on_menu(lv_event_t *e)
{
    (void)e;
    g.taps++;
    menu_open(MENU_MAIN);
}

/* Open a menu by name, for the serial console.
 *
 * This calls the same menu_open() the UI calls, so a test through here exercises
 * the real path -- including the WiFi page's scan-on-open behaviour, which is
 * where the "radio unavailable" report came from. */
bool game_open_menu(const char *name)
{
    if      (!strcmp(name, "main"))     menu_open(MENU_MAIN);
    else if (!strcmp(name, "settings")) menu_open(MENU_SETTINGS);
    else if (!strcmp(name, "wifi"))     menu_open(MENU_WIFI);
    else if (!strcmp(name, "sd"))       menu_open(MENU_SD);
    else if (!strcmp(name, "theme"))    menu_open(MENU_THEME);
    else if (!strcmp(name, "sound"))    menu_open(MENU_SOUND);
    else if (!strcmp(name, "ach"))      menu_open(MENU_ACHIEVEMENTS);
    else if (!strcmp(name, "reset"))    menu_open(MENU_RESET);
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
static volatile int s_ui_req = GAME_UI_NONE;

void game_ui_call(game_ui_action_t action)
{
    s_ui_req = (int)action;

    /* Wait for the render tick to pick it up. Bounded, so a stalled render
     * timer cannot hang the console forever. */
    for (int i = 0; i < 100 && s_ui_req != GAME_UI_NONE; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* Called from game_tick_render(), on the LVGL task. */
static void ui_service_request(void)
{
    int req = s_ui_req;
    if (req == GAME_UI_NONE) return;

    s_ui_req = GAME_UI_NONE;

    switch ((game_ui_action_t)req) {
    case GAME_UI_FEED:  game_on_feed(NULL);  break;
    case GAME_UI_CLEAN: game_on_clean(NULL); break;
    case GAME_UI_PLAY:  game_on_play(NULL);  break;
    case GAME_UI_SLEEP: game_on_sleep(NULL); break;
    case GAME_UI_MENU:  game_on_menu(NULL);  break;
    default: break;
    }
}

/* Open a menu by name from the console, marshalled onto the LVGL task.
 *
 * Opening a menu creates and deletes LVGL objects. Doing that from the console
 * task corrupted LVGL's event list and crashed with "LoadProhibited in
 * lv_event_mark_deleted" -- the same class of bug as calling lv_timer_pause()
 * from a worker. So the request is posted and the render tick performs it. */
static char s_ui_menu_name[16];
static volatile bool s_ui_menu_req;
static volatile bool s_ui_menu_done;
static volatile bool s_ui_menu_ok;

bool game_ui_open_menu(const char *name)
{
    if (!name || strlen(name) >= sizeof s_ui_menu_name) return false;

    strncpy(s_ui_menu_name, name, sizeof s_ui_menu_name - 1);
    s_ui_menu_name[sizeof s_ui_menu_name - 1] = '\0';

    s_ui_menu_done = false;
    s_ui_menu_ok   = false;
    s_ui_menu_req  = true;

    /* Bounded wait, so a stalled render timer cannot hang the console. */
    for (int i = 0; i < 100 && !s_ui_menu_done; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    return s_ui_menu_ok;
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
    if (ui_hud_chrome_visible()) return;
    if (!behaviour_aim_active()) return;

    lv_indev_t *indev = lv_indev_active();
    lv_point_t p = {0, 0};
    if (indev) lv_indev_get_point(indev, &p);

    behaviour_aim_drag_begin(p.x, p.y);
}

void game_on_terrarium_pressing(lv_event_t *e)
{
    if (!behaviour_aim_dragging()) return;

    lv_indev_t *indev = lv_indev_active();
    lv_point_t p = {0, 0};
    if (indev) lv_indev_get_point(indev, &p);

    behaviour_aim_drag_move(p.x, p.y);
}

void game_on_terrarium_release(lv_event_t *e)
{
    if (!behaviour_aim_dragging()) return;

    if (behaviour_aim_release()) {
        g.taps++;
    }
}

void game_on_terrarium_tap(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_active();
    lv_point_t p = {0, 0};
    if (indev) lv_indev_get_point(indev, &p);

    /* 1. Gift placement keeps priority.
     *
     * The kind was already chosen in the picker, so a single tap here commits
     * the placement. The old two-tap confirm existed because the kind was
     * chosen by blind cycling; now that the player has explicitly picked what
     * to give, asking them to confirm the spot as well is just friction. */
    if (g.gift_mode) {
        if (indev) {
            g.gift.kind    = g.gift_selected;
            g.gift.variant = (uint8_t)(rand() % 3);
            g.gift.used    = 1;
            g.gift.x       = (int16_t)p.x;
            g.gift.y       = (int16_t)p.y;

            terrarium_refresh_gift();
            game_save_now();

            g.gift_mode = 0;
            ui_hud_set_gift_mode(false);

        }
        return;
    }

    /* The edge-band reveal is gone. It used to check the top and bottom 56px
     * strips here and reveal the chrome, which meant a slingshot drag starting
     * near an edge revealed the tray instead of pulling. The chrome is now
     * toggled by the hamburger button, which is a real object with its own
     * click handler, so nothing needs to be intercepted here. */

    /* Tapping the tank dismisses the chrome if it is up.
     *
     * The chrome is a floating overlay, so a tap that reaches this handler is
     * by definition a tap that missed every button. Dismissing here means the
     * tray can be closed by tapping anywhere, which is what the player expects
     * of a menu they opened by accident.
     *
     * This runs BEFORE the aim/pet logic and returns, so the dismissing tap
     * does not also start a slingshot pull. */
    if (ui_hud_chrome_visible()) {
        ui_hud_dismiss_if_visible();
        return;
    }

    /* Remember where the player touched: a bonded spider turns toward it. */
    if (indev) behaviour_note_tap(p.x, p.y);

    /* 2. Aiming is handled by the press/move/release handlers, not by a click.
     * A click here means the player tapped without dragging, which the release
     * handler treats as a cancel. */
    if (behaviour_aim_active()) {
        return;
    }

    /* 3. Empty tap: nothing happens, and nothing is said.
     *
     * The pet mechanic is gone. Tapping the spider used to raise mood and bond,
     * but it competed with the slingshot for the same gesture -- a tap near the
     * spider was ambiguous between "pet" and "start a pull" -- and mood is
     * already raised deliberately by the PLAY button. Removing it makes the
     * whole tank surface mean one thing: drag to hunt.
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
    mind_tick(now);

    needs_tick(now, dt);
    behaviour_tick(now, dt);
    prey_tick(now, dt);

    /* A deeply bonded spider occasionally brings the player a gift of its own.
     * This is the payoff for bond 75. It only fires when there is no gift yet,
     * so it never overwrites something the player chose. */
    static uint32_t next_gift;
    if (g.bond >= BOND_GIFT && !g.gift.used) {
        if (next_gift == 0) {
            next_gift = now + 120000u + (uint32_t)(rand() % 120000);
        } else if (now >= next_gift) {
            next_gift = now + 120000u + (uint32_t)(rand() % 120000);

            g.gift.used    = 1;
            g.gift.kind    = (uint8_t)(rand() % GIFT_KINDS);
            g.gift.variant = (uint8_t)(rand() % 3);
            g.gift.x       = (int16_t)(g.spider_x + (rand() % 41) - 20);
            g.gift.y       = (int16_t)(g.spider_y + (rand() % 41) - 20);
            terrarium_refresh_gift();

            audio_play(SFX_TAP);
            ESP_LOGI(TAG, "bond gift: kind %u", (unsigned)g.gift.kind);
        }
    } else if (g.bond < BOND_GIFT) {
        next_gift = 0;
    }

    /* Wild prey drifts in occasionally. This keeps the tank alive without
     * feeding the spider for free: wild bugs are leaner than the ones the
     * player introduces, so the player is still necessary. */
    static uint32_t last_wild;
    static bool     wild_seeded;

    /* Seed one bug on the first tick. Without this the tank is empty for the
     * first WILD_SPAWN_MS (20 min) after boot, so a fresh session opens with
     * nothing to hunt. */
    if (!wild_seeded) {
        wild_seeded = true;
        last_wild = now;
        if (prey_count() == 0) prey_spawn_wild();
    } else if (prey_count() == 0) {
        /* A flower gift shortens the wait for wild prey. */
        uint32_t wait = WILD_SPAWN_MS /
                        (uint32_t)(1 + gift_bonus()->wild_spawn_bonus);
        if ((now - last_wild) >= wait) {
            last_wild = now;
            prey_spawn_wild();
        }
    }

    /* One rare visitor per in-game day: the reason to come back tomorrow. */
    static uint32_t last_visitor_day = 0xFFFFFFFFu;
    if (g.days_lived != last_visitor_day) {
        last_visitor_day = g.days_lived;
        if (g.days_lived > 0) {
            int top = prey_unlocked_species();
            prey_species_t rare = (prey_species_t)((top < PREY_SPECIES_COUNT)
                                                 ? top : PREY_SPECIES_COUNT - 1);
            prey_spawn_species_ex(rare, true);

            audio_play(SFX_ALERT);
            ESP_LOGI(TAG, "daily visitor: species %d", (int)rare);
        }
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
    (void)timer;

    uint32_t now = lv_tick_get();

    /* THE TANK IS NOT DRAWN WHILE THE RADIO IS ON.
     *
     * This is the rule Pocket Tank follows and the one this device needs: the
     * radio is only on during a check, and the tank is not being drawn while it
     * is. The radio takes internal DMA-capable RAM that the LCD driver needs for
     * its draw buffers, so drawing the tank at the same time is what made the
     * screen go berserk.
     *
     * Note what is NOT skipped: this function still runs, and LVGL still
     * composites. Only the tank's own drawing stops. The menu is drawn by LVGL
     * from its own object tree, so the WiFi list stays visible and interactive
     * while the scan runs -- the player sees "scanning..." on a frozen tank
     * rather than a corrupted one.
     *
     * The logic timer is paused too, so the spider does not move while the radio
     * has the CPU. */
    if (net_radio_active()) {
        if (!s_paused) {
            s_paused = true;
            if (s_logic_timer) lv_timer_pause(s_logic_timer);
        }
        return;
    }

    if (s_paused) {
        s_paused = false;
        s_last_logic = now;
        if (s_logic_timer) lv_timer_resume(s_logic_timer);
    }

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
        /* Only rebuild if the player is still looking at the WiFi list. */
        if (s_open_menu == MENU_WIFI) menu_open(MENU_WIFI);
    }

    terrarium_tick(now);
    cspider_render(now);
    spider_render(now);   /* aim visuals only */
    prey_render(now);
    ui_hud_tick(now);
}

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */
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

    /* Tap layer sits above the background but below the spider. */
    ui.tap_layer = lv_obj_create(scr);
    lv_obj_remove_style_all(ui.tap_layer);
    lv_obj_set_size(ui.tap_layer, ui.W, ui.H);
    lv_obj_set_pos(ui.tap_layer, 0, 0);
    lv_obj_set_style_bg_opa(ui.tap_layer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui.tap_layer, 0, 0);
    lv_obj_set_scrollable(ui.tap_layer, false);
    lv_obj_set_clickable(ui.tap_layer, true);
    lv_obj_add_event_cb(ui.tap_layer, game_on_terrarium_tap, LV_EVENT_CLICKED, NULL);
    /* Slingshot needs the raw press/move/release stream, not just clicks. */
    lv_obj_add_event_cb(ui.tap_layer, game_on_terrarium_press,
                        LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(ui.tap_layer, game_on_terrarium_pressing,
                        LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(ui.tap_layer, game_on_terrarium_release,
                        LV_EVENT_RELEASED, NULL);

    terrarium_init(scr);

    /* The creature is a single canvas, not a scene graph. cspider draws the
     * whole spider into one 96x96 buffer each frame; spider_init() now only
     * creates the slingshot aim visuals. */
    cspider_init(scr);
    spider_init(scr);

    ui_hud_init(scr);

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

    /* --- connectivity and storage -------------------------------------
     * Both are optional. net_init() stays off when no credentials are saved,
     * and sd_init() does not touch the hardware at all.
     *
     * WiFi is NOT started here. Bringing the radio up allocates internal
     * DMA-capable buffers, and doing that while the LCD driver is still
     * claiming its own produced an intermittent "Draw bitmap failed:
     * ESP_ERR_NO_MEM" on the first frame -- roughly one boot in three. The
     * radio is started from the first logic tick instead, by which point the
     * display has finished its initial draw. */
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

    /* Serial test console. Started last, so every command has a fully
     * initialised game to talk to. */
    test_console_init();
}
