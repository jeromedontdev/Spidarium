/*
 * Spidarium — serial test console.
 *
 * A tiny command interpreter on the console UART, so the device can be driven
 * and inspected from a script instead of by tapping the screen. This exists
 * because several bugs in this project were invisible from the outside: a scan
 * that silently returned nothing, a radio that leaked its buffers, a display
 * that starved thirty seconds in. All of those are trivially visible from a
 * command that runs the thing and prints what happened.
 *
 * Commands are single letters or short words, one per line, terminated by \n or
 * \r. Every command prints a line starting with "OK" or "ERR" so a script can
 * tell success from failure without parsing prose.
 *
 *   help              list commands
 *   mem               memory report (internal / dma-cap / psram)
 *   stat              game state summary
 *   scan              run a WiFi scan, print results and the memory delta
 *   scanloop N        run N scans back to back, checking memory does not leak
 *   radio             report whether the radio is up
 *   render N          run N render ticks, checking memory does not leak
 *   spider            report the spider's position and state
 *   prey              report the prey count and positions
 *   feed / clean      trigger the care actions
 *   save / load       force a save or reload
 *   reset             factory reset
 *   reboot            restart the device
 *
 * The leak checks are the point. `scanloop` and `render` print the internal
 * free and largest-block figures before and after, so a leak shows up as a
 * number that keeps falling rather than as a corrupted screen half a minute
 * later.
 */
#include "test_console.h"

#include "ambient.h"
#include "cspider.h"
#include "behaviour.h"
#include "buttons.h"
#include "decor.h"
#include "game.h"
#include "game_config.h"
#include "life.h"
#include "meminfo.h"
#include "mind.h"
#include "needs.h"
#include "net.h"
#include "rtc.h"
#include "prey.h"
#include "save.h"
#include "sd.h"
#include "audio.h"
#include "spider.h"
#include "terrarium.h"
#include "ui_hud.h"
#include "theme.h"
#include "power.h"

#include "driver/usb_serial_jtag.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_sleep.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

static const char *TAG = "spidarium.cmd";

#define LINE_MAX_LEN (SAVE_MAX_RAW * 2 + 64)

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static size_t internal_free(void)
{
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static size_t internal_largest(void)
{
    return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static void print_mem_line(const char *tag)
{
    printf("MEM %s internal=%u largest=%u dma=%u psram=%u\n", tag,
           (unsigned)internal_free(),
           (unsigned)internal_largest(),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

/* ------------------------------------------------------------------ */
/* Commands                                                            */
/* ------------------------------------------------------------------ */
static void cmd_help(void)
{
    printf("OK commands:\n");
    printf("  help                 this list\n");
    printf("  mem                  memory report\n");
    printf("  stat                 game state summary\n");
    printf("  spider               spider position and state\n");
    printf("  prey                 prey count and nearest\n");
    printf("  radio                radio state\n");
    printf("  wifi saved           list saved networks (no passwords)\n");
    printf("  wifi add SSID|PASS   join, save on success, sync RTC\n");
    printf("  wifi sync N          sync RTC through saved network N\n");
    printf("  wifi forget N        forget saved network N\n");
    printf("  wifi ota latest|HTTPS_URL  GitHub latest or manual update\n");
    printf("  otatest             validate latest-release manifest parser\n");
    printf("  rtc                  read hardware RTC and system clock\n");
    printf("  buttons [lock|unlock|toggle]  report or drive the side buttons\n");
    printf("  rtcset EPOCH          set RTC to UTC epoch (manual recovery)\n");
    printf("  gifttest             verify 36 effective bonuses and removal\n");
    printf("  giftpath             report the placed gift's path (always Walk Over)\n");
    printf("  giftlayer [N]        report or set the gift's layer within its band\n");
    printf("  giftreport           report the gift's rendered objects and preview state\n");
    printf("  giftplace K X Y      place a gift through the real commit path\n");
    printf("  uitest               test paging, keyboard, password, HUD, backgrounds\n");
    printf("  hunttest             inject touch, jump/catch, restore pet/prey\n");
    printf("  gaittest             cadence and bounded leg geometry checks\n");
    printf("  mindtest             mind rule table and intent stability\n");
    printf("  ambienttest          ambient motes and idle-life ranges\n");
    printf("  ambient              report the live ambient state\n");
    printf("  dayperiod [EPOCH]    force the clock to test the day/night tint\n");
    printf("  rendercost           report and reset the render-tick cost\n");
    printf("  mind                 report the live intent and day period\n");
    printf("  preytest             eight level gates, restore pet level\n");
    printf("  decortest            place every decor kind, verify the layer\n");
    printf("  climb [N]            report the live climb lift; N pins it for testing\n");
    printf("  climbhere [big]      place a climbable item under the spider and climb it\n");
    printf("  perch [KIND]         place a small prop under the spider and report the perch\n");
    printf("  decor [place K X Y|move S X Y|block SLOT|item SLOT|clear]  inspect or edit placed decor\n");
    printf("  decorreport [SLOT]   report a placed decor object's live geometry\n");
    printf("  decorlayers          report proximity-based layer counts per item\n");
    printf("  setupdone            finish the naming screen without tapping\n");
    printf("  background [0-%d]    show/set persistent pixel-art background\n", THEME_COUNT - 1);
    printf("  border [0-%d|none]   show/buy/apply a matching enclosure frame\n", THEME_COUNT - 1);
    printf("  scan                 run a WiFi scan\n");
    printf("  scanloop N           N scans, check memory drift\n");
    printf("  render N             N render ticks, check memory drift\n");
    printf("  feed                 press FEED\n");
    printf("  clean                press CLEAN\n");
    printf("  speckle [N]          drop N bug speckles (default 1)\n");
    printf("  pests                report waste and the flies it has attracted\n");
    printf("  daily [show|claim|task|reset]  inspect or drive the daily loop\n");
    printf("  treat [0..%d]         buy a treat with Silk (no arg lists them)\n", TREAT_COUNT - 1);
    printf("  play                 press PLAY\n");
    printf("  pet                  pet the spider at its current position\n");
    printf("  sleep                press SLEEP\n");
    printf("  menu                 open the main menu\n");
    printf("  open NAME            main, gift, chrome, nochrome, personality, today, diary\n");
    printf("                       training, calendar, album, photos, generations (and Settings pages)\n");
    printf("  pick ID              activate an enabled item ID in the open menu (real action)\n");
    printf("  spawn [SPECIES]      spawn unlocked bug (0..7, or random)\n");
    printf("  state N              force spider state\n");
    printf("  set FIELD VALUE      needs; auto_feed/auto_hunt/usb_sounds accept 0 or 1, volume 0..100\n");
    printf("                       level 1..99 jumps straight to a level for testing\n");
    printf("  silk add N           add 1..2147483647 testing Silk and save\n");
    printf("  refresh              rebuild the visual layers\n");
    printf("  save                 save now\n");
    printf("  save export|schema  browser backup and field layout\n");
    printf("  save restore HEX    validated pet restore; reboot required\n");
    printf("  screenshot [menu|playfield|spider|sd] capture screen or save playfield to SD\n");
    printf("  capturetest          pixel-compare capture restoration (open a menu first)\n");
    printf("  sd backup|restore|status verified SD backup/restore or card probe\n");
    printf("  sound N              effect 0..11 (7=air, 8=pet, 9=leaves, 10=charge, 11=unplug)\n");
    printf("  audiotest            check environmental PCM hashes/envelopes\n");
    printf("  nap SECONDS          save and deep sleep (5..86400); right button wakes\n");
    printf("  battery              report PMIC charge and power policy\n");
    printf("  wake                 wake idle display without a gameplay tap\n");
    printf("  reset                erase the save\n");
    printf("  reboot               restart\n");
}

static void cmd_mem(void)
{
    mem_report_detail("cmd");
    print_mem_line("cmd");
    printf("STACK console_free=%u\n", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    printf("OK mem\n");
}

static void cmd_stat(void)
{
    printf("STAT state=%u pos=(%d,%d) heading=%.2f hunger=%u mood=%u energy=%u "
           "hygiene=%u level=%lu xp=%lu stage=%u sick=%u bond=%u prey=%d\n",
           (unsigned)g.state, g.spider_x, g.spider_y, (double)g.spider_heading,
           g.hunger, g.mood, g.energy, g.hygiene,
           (unsigned long)g.level, (unsigned long)g.xp, (unsigned)g.stage,
           (unsigned)g.sick, (unsigned)g.bond, prey_count());
    printf("CARE clean_needed=%d dirt=%d clean_xp=%lu\n", needs_clean_needed() ? 1 : 0,
           terrarium_dirt(), (unsigned long)needs_clean_reward());
    printf("DAILY day=%lu claimed=%d streak=%u task=%u task_done=%d progress=%lu/%lu "
           "ready=%d silk=%lu base_day=%lu base=%lu counters=%lu/%lu/%lu/%lu "
           "pet_xp=%u/%u\n",
           (unsigned long)needs_daily_today(), needs_daily_available() ? 0 : 1,
           (unsigned)g.daily_streak, (unsigned)needs_daily_task(),
           g.daily_task_done ? 1 : 0,
           (unsigned long)needs_daily_task_progress(),
           (unsigned long)needs_daily_task_goal(),
           needs_daily_task_ready() ? 1 : 0, (unsigned long)g.silk,
           (unsigned long)g.daily_base_day, (unsigned long)g.daily_task_base,
           (unsigned long)g.meals, (unsigned long)g.cleans,
           (unsigned long)g.plays, (unsigned long)g.pets,
           (unsigned)g.pet_xp_today, (unsigned)PET_XP_DAILY_MAX);
    for (int i = 0; i < DAILY_TASK_SLOTS; i++)
        printf("DAILYTASK slot=%d kind=%u name=\"%s\" progress=%lu/%lu ready=%d\n",
               i, (unsigned)needs_daily_task_at((uint8_t)i),
               needs_daily_task_name_at((uint8_t)i),
               (unsigned long)needs_daily_task_progress_at((uint8_t)i),
               (unsigned long)needs_daily_task_goal_at((uint8_t)i),
               needs_daily_task_ready_at((uint8_t)i) ? 1 : 0);
    printf("OK stat\n");
}

static void cmd_radio(void)
{
    printf("RADIO active=%d busy=%d state=%d status=\"%s\"\n",
           net_radio_active() ? 1 : 0, net_busy() ? 1 : 0,
           (int)net_state(), net_status_text());
    printf("OK radio\n");
}

static void cmd_wifi(char *arg)
{
    if (!arg) { printf("ERR usage: wifi saved|add|sync|forget|ota\n"); return; }
    if (!strcmp(arg, "saved")) {
        for (int i = 0; i < net_saved_count(); i++)
            printf("SAVED %d ssid=\"%s\"\n", i, net_saved_ssid(i));
        printf("OK wifi saved=%d\n", net_saved_count());
        return;
    }
    bool accepted = false;
    if (!strncmp(arg, "add ", 4)) {
        char *password = strchr(arg + 4, '|');
        if (!password) { printf("ERR usage: wifi add SSID|PASSWORD\n"); return; }
        *password++ = 0;
        accepted = net_connect_to(arg + 4, password);
        memset(password, 0, strlen(password));
    } else if (!strncmp(arg, "sync ", 5) || !strncmp(arg, "forget ", 7)) {
        bool forget = arg[0] == 'f';
        char *number = arg + (forget ? 7 : 5), *end;
        long index = strtol(number, &end, 10);
        if (!number[0] || *end || index < 0 || index >= NET_SAVED_MAX) {
            printf("ERR invalid network index\n");
            return;
        }
        accepted = forget ? net_forget_saved(index) : net_sync_saved(index);
    } else if (!strcmp(arg, "ota latest")) accepted = net_ota_latest();
    else if (!strncmp(arg, "ota ", 4)) accepted = net_ota_start(arg + 4);
    else { printf("ERR unknown wifi action\n"); return; }
    if (!accepted) { printf("ERR wifi request rejected\n"); return; }
    int waited = 0;
    while (net_busy() && waited < 420000) {
        vTaskDelay(pdMS_TO_TICKS(100));
        waited += 100;
    }
    if (net_busy()) printf("ERR wifi timeout\n");
    else if (net_scan_error()[0]) printf("ERR wifi %s\n", net_scan_error());
    else printf("OK wifi %s\n", net_status_text());
}

/* Report the physical buttons and let a script drive them without pressing. */
static void cmd_buttons(const char *arg)
{
    if (arg && !strcmp(arg, "lock")) { buttons_set_locked(true); }
    else if (arg && !strcmp(arg, "unlock")) { buttons_set_locked(false); }
    else if (arg && !strcmp(arg, "toggle")) { buttons_set_locked(!buttons_locked()); }
    buttons_report();
    printf("BUTTONS locked=%d volume=%u\n", buttons_locked(), (unsigned)audio_volume());
    printf("OK buttons\n");
}

static void cmd_rtc(void)
{
    time_t utc;
    esp_err_t err = pet_rtc_read(&utc);
    if (err != ESP_OK) { printf("ERR rtc %s\n", esp_err_to_name(err)); return; }
    printf("RTC utc=%lld system=%lld\n", (long long)utc, (long long)time(NULL));
    printf("OK rtc\n");
}

/* Run one scan and report everything about it. */
static void cmd_scan(void)
{
    print_mem_line("scan-before");

    bool queued = net_scan_start();
    if (!queued) {
        printf("ERR scan not queued (busy=%d)\n", net_busy() ? 1 : 0);
        return;
    }

    /* Wait for the worker. Bounded so a hang is reported rather than waited on
     * forever -- a hang is itself the bug worth knowing about. */
    int waited = 0;
    while (net_busy() && waited < 20000) {
        vTaskDelay(pdMS_TO_TICKS(50));
        waited += 50;
    }

    if (net_busy()) {
        printf("ERR scan did not finish within %d ms\n", waited);
        return;
    }

    const char *err = net_scan_error();
    int n = net_scan_count();

    printf("SCAN took=%dms found=%d err=\"%s\"\n", waited, n, err);

    for (int i = 0; i < n; i++) {
        printf("  AP %d ssid=\"%s\" rssi=%d secure=%d\n", i,
               net_scan_ssid(i), net_scan_rssi(i),
               net_scan_secure(i) ? 1 : 0);
    }

    print_mem_line("scan-after");

    if (err[0]) {
        printf("ERR scan failed: %s\n", err);
    } else {
        printf("OK scan found %d\n", n);
    }
}

/* Run N scans back to back. The memory figures must not drift: a falling
 * largest-block is the leak that corrupted the display. */
static void cmd_scanloop(int count)
{
    if (count < 1) count = 1;
    if (count > 20) count = 20;

    if (!game_ui_call(GAME_UI_SCANLOOP_BEGIN)) {
        printf("ERR scanloop could not quiesce gameplay\n");
        return;
    }
    /* The LCD copies PSRAM pixels into temporary DMA staging buffers. Compare
     * idle-display snapshots, not different points in live SPI transactions.
     * This wait is on the console worker, never the LVGL task. */
    vTaskDelay(pdMS_TO_TICKS(100));
    print_mem_line("loop-before");

    size_t first_largest = internal_largest();
    int ok = 0, failed = 0;

    for (int i = 0; i < count; i++) {
        if (!net_scan_start()) {
            printf("  %d: not queued\n", i);
            failed++;
            continue;
        }

        int waited = 0;
        while (net_busy() && waited < 20000) {
            vTaskDelay(pdMS_TO_TICKS(50));
            waited += 50;
        }

        if (net_busy()) {
            printf("  %d: TIMEOUT\n", i);
            failed++;
            continue;
        }

        const char *err = net_scan_error();
        printf("  %d: found=%d err=\"%s\" largest=%u\n", i,
               net_scan_count(), err, (unsigned)internal_largest());

        if (err[0]) failed++; else ok++;
    }

    vTaskDelay(pdMS_TO_TICKS(100));
    print_mem_line("loop-after");

    size_t last_largest = internal_largest();
    long drift = (long)last_largest - (long)first_largest;

    printf("LOOP ok=%d failed=%d largest_drift=%ld\n", ok, failed, drift);

    /* A drift of more than 4 KB across the loop means something is not being
     * released. That is the failure mode that broke the display. */
    bool resumed = game_ui_call(GAME_UI_SCANLOOP_END);
    if (!resumed) {
        printf("ERR scanloop could not restore gameplay\n");
    } else if (drift < -4096) {
        printf("ERR memory leaked %ld bytes across %d scans\n", -drift, count);
    } else if (failed) {
        printf("ERR scanloop %d scans failed\n", failed);
    } else {
        printf("OK scanloop\n");
    }
}

/* Run N render ticks and check the memory does not drift. This is the check
 * that would have caught the per-frame draw-layer allocations. */
static void cmd_render(int count)
{
    if (count < 1) count = 1;
    if (count > 2000) count = 2000;

    if (!game_ui_call(GAME_UI_SCANLOOP_BEGIN)) {
        printf("ERR render could not quiesce gameplay\n");
        return;
    }
    if (!game_ui_call(GAME_UI_RENDER)) {
        if (!game_ui_call(GAME_UI_SCANLOOP_END))
            printf("ERR render could not restore gameplay\n");
        printf("ERR render could not prime canvas\n");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    print_mem_line("render-before");
    size_t first_largest = internal_largest();

    /* Time the ticks as well as checking memory. Memory drift catches leaks;
     * this catches the other failure mode, which is a frame that is correct but
     * too slow. The rotated light shafts were exactly that: no leak, no crash,
     * just 111,000 blended pixels per shaft per frame, which showed up as the
     * spider's movement becoming choppy. */
    int64_t start_us = esp_timer_get_time();
    for (int i = 0; i < count; i++) {
        if (!game_ui_call(GAME_UI_RENDER)) {
            if (!game_ui_call(GAME_UI_SCANLOOP_END))
                printf("ERR render could not restore gameplay\n");
            printf("ERR render stopped after %d of %d ticks\n", i, count);
            return;
        }
    }
    int64_t elapsed_us = esp_timer_get_time() - start_us;

    vTaskDelay(pdMS_TO_TICKS(100));
    print_mem_line("render-after");
    size_t last_largest = internal_largest();
    long drift = (long)last_largest - (long)first_largest;

    /* Per-tick cost in microseconds. The render timer runs at 30 Hz, so a tick
     * must stay well under 33,333 us or the display stutters. */
    long per_tick_us = (long)(elapsed_us / (count > 0 ? count : 1));
    printf("RENDER ticks=%d largest_drift=%ld per_tick_us=%ld\n",
           count, drift, per_tick_us);

    if (!game_ui_call(GAME_UI_SCANLOOP_END)) {
        printf("ERR render could not restore gameplay\n");
    } else if (drift < -4096) {
        printf("ERR render leaked %ld bytes over %d ticks\n", -drift, count);
    } else {
        printf("OK render\n");
    }
}

static void cmd_spider(void)
{
    printf("SPIDER pos=(%d,%d) heading=%.2f state=%u colour=%u sick=%u\n",
           g.spider_x, g.spider_y, (double)g.spider_heading,
           (unsigned)g.state, (unsigned)g.colour, (unsigned)g.sick);
    printf("OK spider\n");
}

static void cmd_prey(void)
{
    int n = prey_count();
    printf("PREY count=%d\n", n);

    int32_t x, y;
    if (prey_nearest(&x, &y)) {
        printf("  nearest=(%ld,%ld)\n", (long)x, (long)y);
    }
    printf("OK prey\n");
}

static void cmd_save(void)
{
    if (!game_ui_call(GAME_UI_SAVE)) { printf("ERR save UI request failed\n"); return; }
    int waited = 0;
    while (save_busy() && waited < 5000) {
        vTaskDelay(pdMS_TO_TICKS(20));
        waited += 20;
    }
    if (save_busy() || !save_last_ok()) printf("ERR save incomplete or failed\n");
    else printf("OK save\n");
}

static bool wait_save(void)
{
    for (int i = 0; i < 500 && save_busy(); i++) vTaskDelay(pdMS_TO_TICKS(20));
    return !save_busy() && save_last_ok();
}

static void cmd_silk(const char *arg)
{
    char *end = NULL;
    errno = 0;
    long amount = arg && !strncmp(arg, "add ", 4) ? strtol(arg + 4, &end, 10) : 0;
    if (errno == ERANGE || !end || *end || amount <= 0 || amount > INT_MAX) {
        printf("ERR usage: silk add N (1..2147483647)\n");
        return;
    }
    if (!game_ui_call_value(GAME_UI_ADD_SILK, (int)amount)) {
        printf("ERR silk grant rejected or save not queued; inspect stat before retrying\n");
        return;
    }
    if (!wait_save()) {
        printf("ERR silk save incomplete or failed; balance changed in RAM, inspect stat before retrying\n");
        return;
    }
    printf("OK silk added=%ld total=%lu saved=1\n", amount, (unsigned long)g.silk);
    if (g.silk >= SILK_MAX) {
        printf("NOTE silk is at the %u cap; further grants have no effect\n",
               (unsigned)SILK_MAX);
    }
}

static bool console_data(const char *text)
{
    size_t length = strlen(text);
    for (size_t offset = 0; offset < length; offset += 128) {
        size_t count = length - offset < 128 ? length - offset : 128;
        if (usb_serial_jtag_write_bytes(text + offset, count, pdMS_TO_TICKS(2000)) != (int)count ||
            usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(2000)) != ESP_OK) {
            ESP_LOGE(TAG, "console data transfer incomplete");
            return false;
        }
    }
    return true;
}

static void cmd_save_schema(void)
{
    static const struct { const char *name, *type; size_t offset, length; unsigned min_version; } fields[] = {
#define SAVE_FIELD(name, type) {#name, type, offsetof(game_state_t, name), 0, 2}
        SAVE_FIELD(hunger, "u8"), SAVE_FIELD(mood, "u8"), SAVE_FIELD(energy, "u8"),
        SAVE_FIELD(hygiene, "u8"), SAVE_FIELD(age_ticks, "u32"), SAVE_FIELD(meals, "u32"),
        SAVE_FIELD(cleans, "u32"), SAVE_FIELD(plays, "u32"), SAVE_FIELD(xp, "u32"),
        SAVE_FIELD(stage, "u8"), SAVE_FIELD(sick, "u8"), SAVE_FIELD(muted, "u8"),
        SAVE_FIELD(level, "u32"), SAVE_FIELD(achievements, "u32"),
        SAVE_FIELD(days_lived, "u32"), SAVE_FIELD(best_streak, "u16"),
        SAVE_FIELD(bond, "u16"),
        {"name", "string", offsetof(game_state_t, name), sizeof g.name, 2},
        SAVE_FIELD(colour, "u8"), SAVE_FIELD(silk, "u32"),
        SAVE_FIELD(gifts_owned, "u32"), SAVE_FIELD(brightness, "u8"),
        {"auto_feed", "u8", offsetof(game_state_t, auto_feed), 0, 3},
        {"auto_hunt", "u8", offsetof(game_state_t, auto_hunt), 0, 3},
        {"volume", "u8", offsetof(game_state_t, volume), 0, 4},
        {"last_seen", "u32", offsetof(game_state_t, last_seen), 0, 5},
        {"daily_day", "u32", offsetof(game_state_t, daily_day), 0, 6},
        {"daily_base_day", "u32", offsetof(game_state_t, daily_base_day), 0, 6},
        {"daily_task_base", "u32", offsetof(game_state_t, daily_task_base), 0, 6},
        {"daily_streak", "u8", offsetof(game_state_t, daily_streak), 0, 6},
        {"daily_task_done", "u8", offsetof(game_state_t, daily_task_done), 0, 6},
        {"daily_task_base_extra", "u32[2]", offsetof(game_state_t, daily_task_base_extra),
         sizeof g.daily_task_base_extra, 24},
        {"daily_task_done_extra", "u8", offsetof(game_state_t, daily_task_done_extra), 0, 24},
        {"pet_xp_day", "u32", offsetof(game_state_t, pet_xp_day), 0, 7},
        {"pet_xp_today", "u8", offsetof(game_state_t, pet_xp_today), 0, 7},
        {"decor_owned", "u32", offsetof(game_state_t, decor_owned), 0, 8},
        {"decor", "decor[12]", offsetof(game_state_t, decor), sizeof g.decor, 8},
        {"backgrounds_owned", "u32", offsetof(game_state_t, backgrounds_owned), 0, 9},
        {"background_id", "u8", offsetof(game_state_t, background_id), 0, 9},
        {"power_mode", "u8", offsetof(game_state_t, power_mode), 0, 9},
        {"screen_timeout", "u8", offsetof(game_state_t, screen_timeout), 0, 9},
        {"deep_sleep_after", "u8", offsetof(game_state_t, deep_sleep_after), 0, 9},
        {"rtc_wake", "u8", offsetof(game_state_t, rtc_wake), 0, 9},
        {"catalogue_owned_high", "u8", offsetof(game_state_t, catalogue_owned_high), 0, 12},
        {"backgrounds_owned_extra", "u16", offsetof(game_state_t, backgrounds_owned_extra), 0, 12},
        {"borders_owned", "u32", offsetof(game_state_t, borders_owned), 0, 12},
        {"borders_owned_extra", "u16", offsetof(game_state_t, borders_owned_extra), 0, 12},
        {"border_id", "u8", offsetof(game_state_t, border_id), 0, 12},
        {"decor_owned_extra", "u32", offsetof(game_state_t, decor_owned_extra), 0, 10},
        {"music_enabled", "u8", offsetof(game_state_t, music_enabled), 0, 10},
        {"lights_dimmed", "u8", offsetof(game_state_t, lights_dimmed), 0, 10},
        {"music_volume", "u8", offsetof(game_state_t, music_volume), 0, 11},
        {"music_track", "u8", offsetof(game_state_t, music_track), 0, 11},
        {"usb_sounds", "u8", offsetof(game_state_t, usb_sounds), 0, 15},
        {"decor_extra_count", "u32", offsetof(game_state_t, decor_extra_count), 0, 10},
        {"decor_locked", "u8", offsetof(game_state_t, decor_locked), 0, 18},
        {"auto_clean", "u8", offsetof(game_state_t, auto_clean), 0, 18},
        {"gift_path", "u8", offsetof(game_state_t, gift_path), 0, 19},
        {"climb_owned", "u8", offsetof(game_state_t, climb_owned), 0, 21},
        {"gift_layer", "u8", offsetof(game_state_t, gift_layer), 0, 22},
#undef SAVE_FIELD
    };    enum { SCHEMA_CAP = 7168 };
    char *json = heap_caps_malloc(SCHEMA_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!json) { printf("ERR save schema allocation failed\n"); return; }
    int pos = snprintf(json, SCHEMA_CAP,
        "{\"magic\":%u,\"version\":%u,\"headerSize\":12,\"payloadSizes\":[%u,%u],\"maxPayload\":23308,\"decorTail\":{\"offset\":%u,\"stride\":6,\"countField\":\"decor_extra_count\"},"
        "\"life\":{\"offset\":860,\"size\":2236,\"minVersion\":16,\"visitor_day\":0,\"season_highwater\":4,"
        "\"progress\":{\"offset\":8,\"count\":3},\"cooldown\":11,\"daynight\":12,\"weather\":13,\"retired_count\":14,\"light_gesture_disabled\":{\"offset\":15,\"minVersion\":17},"
        "\"retired\":{\"offset\":16,\"count\":3,\"stride\":740,\"core\":{\"offset\":0,\"count\":120},\"daily\":{\"offset\":120,\"count\":24},"
        "\"companion\":{\"offset\":144,\"size\":592},\"progress\":{\"offset\":736,\"count\":3},\"reserved\":739}},"
        "\"companion\":{\"offset\":268,\"size\":592,\"minVersion\":15,"
        "\"request_day\":0,\"seen_achievements\":4,\"requests\":{\"offset\":8,\"count\":2,\"stride\":4,\"fields\":{\"kind\":0,\"target\":1,\"progress\":2,\"state\":3}},"
        "\"treat_counts\":{\"offset\":16,\"count\":12},\"gift_counts\":{\"offset\":28,\"count\":36},\"play_counts\":{\"offset\":64,\"count\":3},"
        "\"favorite_treat\":67,\"favorite_gift\":68,\"favorite_play\":69,\"diary_count\":70,\"clock_mode\":71,\"initialized\":72,\"bond_level\":73,\"last_stage\":74,\"was_sick\":75,"
        "\"reserved\":{\"offset\":76,\"count\":2},\"padding\":{\"offset\":78,\"count\":2},\"diary\":{\"offset\":80,\"count\":64,\"stride\":8,\"fields\":{\"day\":0,\"kind\":4,\"value\":5,\"pinned\":6,\"calendar\":7}}},\"fields\":[",
        (unsigned)SAVE_MAGIC, SAVE_VERSION, (unsigned)offsetof(game_state_t, silk),
        (unsigned)sizeof(game_state_t), (unsigned)sizeof(game_state_t));
    bool ok = pos > 0 && pos < SCHEMA_CAP;
    for (size_t i = 0; ok && i < sizeof fields / sizeof fields[0]; i++) {
        int written = snprintf(json + pos, SCHEMA_CAP - pos,
            "%s{\"name\":\"%s\",\"offset\":%u,\"type\":\"%s\",\"length\":%u,\"minVersion\":%u}",
            i ? "," : "", fields[i].name, (unsigned)fields[i].offset,
            fields[i].type, (unsigned)fields[i].length, fields[i].min_version);
        ok = written > 0 && written < SCHEMA_CAP - pos;
        if (ok) pos += written;
    }
    if (ok && pos + 3 < SCHEMA_CAP) {
        snprintf(json + pos, SCHEMA_CAP - pos, "]}");
        esp_log_level_t level = esp_log_level_get("*");
        esp_log_level_set("*", level > ESP_LOG_WARN ? ESP_LOG_WARN : level);
        bool sent = console_data("SAVE SCHEMA ") && console_data(json) &&
                    console_data("\nOK save schema\n");
        esp_log_level_set("*", level);
        if (!sent) printf("ERR save schema transfer incomplete\n");
    } else printf("ERR save schema buffer too small\n");
    heap_caps_free(json);
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void cmd_save_tool(const char *arg)
{
    if (!arg || !arg[0]) { cmd_save(); return; }
    if (!strcmp(arg, "schema")) { cmd_save_schema(); return; }
    if (!strcmp(arg, "export") || !strcmp(arg, "stored")) {
        if (strcmp(arg, "stored") && !save_restore_pending() && !game_ui_call(GAME_UI_SAVE)) {
            printf("ERR save export snapshot not queued\n"); return;
        }
        if (!wait_save()) { printf("ERR save export persistence failed\n"); return; }
        size_t size = save_read_raw(NULL, 0);
        uint8_t *blob = size && size <= SAVE_MAX_RAW ?
            heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
        if (!blob || save_read_raw(blob, size) != size || !save_raw_valid(blob, size)) {
            heap_caps_free(blob);
            printf("ERR save export invalid or missing blob\n"); return;
        }
        esp_log_level_t level = esp_log_level_get("*");
        esp_log_level_set("*", level > ESP_LOG_WARN ? ESP_LOG_WARN : level);
        bool sent = console_data("SAVE HEX ");
        char hex[129];
        for (size_t i = 0; sent && i < size; i += 64) {
            size_t count = size - i < 64 ? size - i : 64;
            for (size_t j = 0; j < count; j++) snprintf(hex + j * 2, 3, "%02x", blob[i + j]);
            sent = console_data(hex);
        }
        sent = sent && console_data("\nOK save export\n");
        heap_caps_free(blob);
        esp_log_level_set("*", level);
        if (!sent) printf("ERR save export transfer incomplete\n");
        return;
    }
    if (!strncmp(arg, "restore ", 8)) {
        const char *hex = arg + 8;
        size_t length = strlen(hex);
        if (length % 2 || length < 24 || length > SAVE_MAX_RAW * 2) {
            printf("ERR save restore invalid hex length\n"); return;
        }
        uint8_t *blob = heap_caps_malloc(length / 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!blob) { printf("ERR save restore PSRAM exhausted\n"); return; }
        for (size_t i = 0; i < length / 2; i++) {
            int hi = hex_digit(hex[i * 2]), lo = hex_digit(hex[i * 2 + 1]);
            if (hi < 0 || lo < 0) { heap_caps_free(blob); printf("ERR save restore invalid hex\n"); return; }
            blob[i] = (hi << 4) | lo;
        }
        bool queued = save_request_raw(blob, length / 2);
        heap_caps_free(blob);
        if (!queued) {
            printf("ERR save restore validation or queue rejected\n"); return;
        }
        printf("%s save restore; reboot required\n", wait_save() ? "OK" : "ERR");
        return;
    }
    printf("ERR use save export|schema|restore HEX\n");
}

static void cmd_screenshot(const char *arg)
{
    if (arg && !strcmp(arg, "sd")) {
        if (!game_ui_call(GAME_UI_SCREEN_SD)) { printf("ERR screenshot SD capture not queued\n"); return; }
        if (!wait_save()) { printf("ERR screenshot SD persistence failed: %s\n", sd_status_text()); return; }
        printf("OK screenshot SD: %s\n", sd_status_text());
        return;
    }
    if (arg && strcmp(arg, "spider") && strcmp(arg, "menu") && strcmp(arg, "playfield")) {
        printf("ERR use screenshot [menu|playfield|spider|sd]\n"); return;
    }
    bool spider = arg && !strcmp(arg, "spider");
    int target = spider ? 1 : arg && !strcmp(arg, "playfield") ? 2 : 0;
    if (!game_ui_call_value(GAME_UI_SCREEN_CAPTURE, target)) {
        printf("ERR screenshot capture failed\n"); return;
    }
    const lv_draw_buf_t *capture = game_screen_capture();
    size_t row_bytes = capture->header.w * (spider ? 4 : 2);
    char *hex = heap_caps_malloc(row_bytes * 2 + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!hex) {
        game_ui_call(GAME_UI_SCREEN_RELEASE);
        printf("ERR screenshot transfer allocation failed\n"); return;
    }
    esp_log_level_t log_level = esp_log_level_get("*");
    esp_log_level_set("*", log_level > ESP_LOG_WARN ? ESP_LOG_WARN : log_level);
    char line[240];
    snprintf(line, sizeof line, "SCREEN BEGIN %u %u %s\n", (unsigned)capture->header.w,
             (unsigned)capture->header.h, spider ? "bgra8888" : "rgb565le");
    bool sent = console_data(line);
    static const char digits[] = "0123456789abcdef";
    for (unsigned y = 0; sent && y < capture->header.h; y++) {
        const uint8_t *row = capture->data + y * capture->header.stride;
        for (size_t x = 0; x < row_bytes; x++) {
            hex[x * 2] = digits[row[x] >> 4];
            hex[x * 2 + 1] = digits[row[x] & 15];
        }
        hex[row_bytes * 2] = 0;
        for (size_t offset = 0; sent && offset < row_bytes; offset += 96) {
            size_t count = row_bytes - offset < 96 ? row_bytes - offset : 96;
            snprintf(line, sizeof line, "SCREEN PART %u %u %.*s\n", y, (unsigned)offset,
                     (int)(count * 2), hex + offset * 2);
            sent = console_data(line);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    heap_caps_free(hex);
    bool released = game_ui_call(GAME_UI_SCREEN_RELEASE);
    console_data(sent && released ? "OK screenshot\n" : "ERR screenshot transfer or release failed\n");
    esp_log_level_set("*", log_level);
}

/* --- device control -------------------------------------------------
 *
 * These drive the game the way the on-screen buttons do, so a feature can be
 * exercised from a script without tapping. Each one calls the same code path
 * the UI calls, so a test here is a test of the real thing.
 *
 * IMPORTANT: these run on the CONSOLE task, not the LVGL task. Anything that
 * touches lv_* must be marshalled onto the LVGL thread, or it corrupts LVGL's
 * internal state exactly as calling lv_timer_pause() from a worker did.
 *
 * game_ui_call() does that marshalling. Every command below that touches the UI
 * goes through it.
 */

static void cmd_feed(void)
{
    printf("%s feed\n", game_ui_call(GAME_UI_FEED) ? "OK" : "ERR");
}

static void cmd_clean(void)
{
    /* Clean always works; the reward scales with how much the tank improved.
     * Report the XP granted so a script can see the scaling. The verdict is
     * whether the tank actually changed, so a tank holding only bugs reports
     * OK with xp=0 rather than looking like a failure. */
    uint32_t xp = needs_clean_reward();
    printf("%s clean xp=%lu\n", game_ui_call(GAME_UI_CLEAN) ? "OK" : "ERR",
           (unsigned long)xp);
}

/* Drop bug speckles, so the bug-poop path and the Clean scaling can be
 * exercised without waiting BUG_POOP_MS for a live bug to produce one.
 * Marshalled to the LVGL task: it creates objects. */
static void cmd_speckle(const char *arg)
{
    int count = arg ? atoi(arg) : 1;
    if (count < 1) count = 1;
    if (count > MAX_POOP) count = MAX_POOP;
    if (!game_ui_call_value(GAME_UI_SPECKLE, count)) {
        printf("ERR speckle failed\n");
        return;
    }
    printf("OK speckle count=%d dirt=%d clean_xp=%lu\n", terrarium_poop_count(),
           terrarium_dirt(), (unsigned long)needs_clean_reward());
}

/* Report the neglect cycle: how much waste is in the tank and how many flies it
 * has attracted. A fly only appears once a dropping has been ignored for
 * FLY_ATTRACT_MS, so this is the observable proof that neglect has a visible
 * consequence rather than only a number in a menu. */
static void cmd_pests(void)
{
    printf("PESTS poop=%d dirt=%d flies=%d\n", terrarium_poop_count(),
           terrarium_dirt(), terrarium_pest_count());
    printf("OK pests\n");
}

static void cmd_daily(const char *arg)
{
    if (!arg || !strcmp(arg, "show")) {
        printf("DAILY day=%lu claimed=%d streak=%u task=%u task_done=%d "
               "progress=%lu/%lu ready=%d silk=%lu\n",
               (unsigned long)needs_daily_today(), needs_daily_available() ? 0 : 1,
               (unsigned)g.daily_streak, (unsigned)needs_daily_task(),
               g.daily_task_done ? 1 : 0,
               (unsigned long)needs_daily_task_progress(),
               (unsigned long)needs_daily_task_goal(),
               needs_daily_task_ready() ? 1 : 0, (unsigned long)g.silk);
        printf("OK daily\n");
        return;
    }
    if (!strcmp(arg, "claim")) {
        uint32_t xp = needs_daily_claim();
        if (xp == 0) {
            printf("ERR daily already claimed for day %lu\n",
                   (unsigned long)needs_daily_today());
            return;
        }
        game_save_now();
        printf("OK daily claim xp=%lu streak=%u silk=%lu\n", (unsigned long)xp,
               (unsigned)g.daily_streak, (unsigned long)g.silk);
        return;
    }
    if (!strcmp(arg, "task")) {
        /* `daily task` claims the first ready slot, so the console exercises the
         * same path the menu does without needing a slot argument. */
        int slot = -1;
        for (int i = 0; i < DAILY_TASK_SLOTS; i++)
            if (needs_daily_task_ready_at((uint8_t)i)) { slot = i; break; }
        if (slot < 0) {
            printf("ERR daily task not ready (%lu/%lu)\n",
                   (unsigned long)needs_daily_task_progress_at(0),
                   (unsigned long)needs_daily_task_goal_at(0));
            return;
        }
        uint32_t xp = needs_daily_task_claim_at((uint8_t)slot);
        game_save_now();
        printf("OK daily task slot=%d xp=%lu silk=%lu\n", slot, (unsigned long)xp,
               (unsigned long)g.silk);
        return;
    }
    /* `daily reset` clears the claim so the next `daily claim` pays again. It
     * exists so the loop is testable without waiting a whole in-game day. */
    if (!strcmp(arg, "reset")) {
        g.daily_day = DAILY_NEVER;
        g.daily_base_day = DAILY_NEVER;
        g.daily_task_base = 0;
        g.daily_streak = 0;
        g.daily_task_done = 0;
        for (int i = 0; i < DAILY_TASK_SLOTS - 1; i++) g.daily_task_base_extra[i] = 0;
        g.daily_task_done_extra = 0;
        g.pet_xp_day = DAILY_NEVER;
        g.pet_xp_today = 0;
        game_save_now();
        printf("OK daily reset\n");
        return;
    }
    /* `daily day N` forces the in-game day, so the streak and the task rotation
     * can be exercised without waiting AGE_TICK_MS * TICKS_PER_DAY. */
    if (!strncmp(arg, "day ", 4)) {
        char *end;
        unsigned long day = strtoul(arg + 4, &end, 10);
        if (*end) { printf("ERR usage: daily day N\n"); return; }
        needs_daily_force_day((uint32_t)day);
        game_save_now();
        printf("OK daily day=%lu task=%u\n", (unsigned long)needs_daily_today(),
               (unsigned)needs_daily_task());
        return;
    }
    /* `daily clock` drops the override so the real RTC (or the in-game counter
     * when the RTC is not valid) takes over again. */
    if (!strcmp(arg, "clock")) {
        needs_daily_clear_override();
        game_save_now();
        printf("OK daily clock day=%lu\n", (unsigned long)needs_daily_today());
        return;
    }
    printf("ERR usage: daily [show|claim|task|reset|clock|day N]\n");
}

static void cmd_treat(const char *arg)
{
    if (!arg) {
        for (int i = 0; i < TREAT_COUNT; i++)
            printf("TREAT %d name=\"%s\" cost=%lu\n", i, treat_name((treat_t)i),
                   (unsigned long)treat_cost((treat_t)i));
        printf("OK treat silk=%lu\n", (unsigned long)g.silk);
        return;
    }
    int kind = atoi(arg);
    if (kind < 0 || kind >= TREAT_COUNT) {
        printf("ERR usage: treat [0..%d]\n", TREAT_COUNT - 1);
        return;
    }
    if (!treat_buy((treat_t)kind)) {
        printf("ERR treat %d refused: silk=%lu cost=%lu\n", kind,
               (unsigned long)g.silk, (unsigned long)treat_cost((treat_t)kind));
        return;
    }
    game_save_now();
    printf("OK treat %d silk=%lu hunger=%u mood=%u energy=%u\n", kind,
           (unsigned long)g.silk, g.hunger, g.mood, g.energy);
}

/* "decor" lists the catalogue; "decor place KIND X Y" buys if needed and
 * places; "decor clear" empties the enclosure; "decor block SLOT" flips the
 * blocking flag. Mutations and renderer updates are copied to the LVGL task;
 * the console never reallocates records while rendering reads them. */
static void cmd_decor(const char *arg)
{
    if (!arg) {
        for (int i = 0; i < DECOR_COUNT; i++) {
            const decor_def_t *def = decor_def((uint8_t)i);
            printf("DECOR %d name=\"%s\" cost=%u level=%u radius=%u blocking=%u owned=%d\n",
                   i, def->name, def->cost, def->unlock_level, def->radius,
                   def->blocking, decor_owned((uint8_t)i));
        }
        int count = 0;
        if (!game_ui_decor_edit(DECOR_EDIT_STATUS, 0, 0, 0, &count)) {
            printf("ERR decor status request failed\n"); return;
        }
        printf("OK decor silk=%lu placed=%d storage=growable\n", (unsigned long)g.silk, count);
        return;
    }
    if (!strncmp(arg, "clear", 5)) {
        printf("%s decor clear\n", game_ui_decor_edit(DECOR_EDIT_CLEAR, 0, 0, 0, NULL) ? "OK" : "ERR");
        return;
    }
    if (!strncmp(arg, "item ", 5)) {
        int slot = atoi(arg + 5);
        char name[16];
        snprintf(name, sizeof name, "item %d", slot);
        if (!game_ui_open_menu(name)) {
            printf("ERR decor item %d: menu request failed\n", slot);
            return;
        }
        printf("OK decor item slot=%d\n", slot);
        return;
    }
    if (!strncmp(arg, "move ", 5)) {
        int slot = 0, x = 0, y = 0;
        if (sscanf(arg + 5, "%d %d %d", &slot, &x, &y) != 3) {
            printf("ERR usage: decor move SLOT X Y\n");
            return;
        }
        if (!game_ui_decor_edit(DECOR_EDIT_MOVE, slot, x, y, NULL)) {
            printf("ERR decor move %d: empty or out of range\n", slot);
            return;
        }
        printf("OK decor move slot=%d\n", slot);
        return;
    }
    if (!strncmp(arg, "block ", 6)) {
        int slot = atoi(arg + 6);
        if (!game_ui_decor_edit(DECOR_EDIT_BLOCK, slot, 0, 0, NULL)) {
            printf("ERR decor block %d: empty or out of range\n", slot);
            return;
        }
        printf("OK decor block slot=%d\n", slot);
        return;
    }
    if (!strncmp(arg, "place ", 6)) {
        int kind = 0, x = 0, y = 0;
        if (sscanf(arg + 6, "%d %d %d", &kind, &x, &y) != 3 ||
            kind < 0 || kind >= DECOR_COUNT) {
            printf("ERR usage: decor place KIND X Y\n");
            return;
        }
        int slot = -1;
        if (!game_ui_decor_edit(DECOR_EDIT_PLACE, kind, x, y, &slot)) {
            printf("ERR decor place %d: request failed; see log\n", kind);
            return;
        }
        printf("OK decor place kind=%d slot=%d\n", kind, slot);
        return;
    }
    printf("ERR usage: decor [place K X Y|move S X Y|block SLOT|item SLOT|clear]\n");
}

static void cmd_play(void)
{
    printf("%s play\n", game_ui_call(GAME_UI_PLAY) ? "OK" : "ERR");
}

/* Pet the spider at its current position, exercising the same needs_pet() path
 * a tap on the body takes. Reports the counters so a script can see the effect. */
static void cmd_pet(void)
{
    bool ok = needs_pet(g.spider_x, g.spider_y);
    if (!ok) {
        printf("ERR pet refused (cooldown or out of range)\n");
        return;
    }
    game_save_now();
    printf("OK pet mood=%u bond=%u pets=%lu xp=%lu pet_xp=%u/%u\n", g.mood,
           (unsigned)g.bond, (unsigned long)g.pets, (unsigned long)g.xp,
           (unsigned)g.pet_xp_today, (unsigned)PET_XP_DAILY_MAX);
}

static void cmd_sleep(void)
{
    printf("%s sleep\n", game_ui_call(GAME_UI_SLEEP) ? "OK" : "ERR");
}

static void cmd_menu(void)
{
    printf("%s menu\n", game_ui_call(GAME_UI_MENU) ? "OK" : "ERR");
}

/* Spawn a bug of a given species index, or a random one. */
static void cmd_spawn(int species)
{
    if (species == -1) {
        printf("%s spawn (random)\n", game_ui_call_value(GAME_UI_SPAWN, -1) ? "OK" : "ERR");
    } else if (species < 0 || species >= PREY_SPECIES_COUNT) {
        printf("ERR spawn species must be 0..%d\n", PREY_SPECIES_COUNT - 1);
    } else {
        printf("%s spawn species=%d\n",
               game_ui_call_value(GAME_UI_SPAWN, species) ? "OK" : "ERR", species);
    }
}

/* Force the spider into a state, so a state's rendering can be inspected. */
static void cmd_state(int state)
{
    /* SPIDER_PERCH is the last enumerator; there is no COUNT constant. */
    if (state < 0 || state > SPIDER_PERCH) {
        printf("ERR state must be 0..%d\n", (int)SPIDER_PERCH);
        return;
    }
    printf("%s state=%d\n", game_ui_call_value(GAME_UI_STATE, state) ? "OK" : "ERR", state);
}

/* Set a need directly, so a UI state can be reached without waiting. */
static void cmd_set(const char *field, int value)
{
    if ((!strcmp(field, "auto_feed") || !strcmp(field, "auto_hunt") ||
         !strcmp(field, "music_enabled") || !strcmp(field, "usb_sounds") ||
         !strcmp(field, "muted") || !strcmp(field, "decor_locked") ||
         !strcmp(field, "auto_clean")) &&
        (value < 0 || value > 1)) {
        printf("ERR automation checkboxes require 0 or 1\n");
        return;
    }
    if ((!strcmp(field, "music_volume") && (value < 0 || value > 100)) ||
        (!strcmp(field, "music_track") && (value < 0 || value >= AUDIO_MUSIC_TRACK_COUNT))) {
        printf("ERR music volume requires 0..100; track requires 0..%d\n", AUDIO_MUSIC_TRACK_COUNT - 1);
        return;
    }
    if (value < 0) value = 0;
    if (value > 100) value = 100;

    /* Level is not a 0..100 stat: it is derived from XP and the highest decor
     * unlock is 18, so the clamp above would silently cap it at 100 anyway.
     * Handle it before the stat clamp so the range is explicit. */
    if (!strcmp(field, "level")) {
        if (value < 1 || value > 99) {
            printf("ERR level requires 1..99\n");
            return;
        }
        printf("%s set level=%d\n",
               game_ui_call_value(GAME_UI_SET_LEVEL, value) ? "OK" : "ERR", value);
        return;
    }

    game_ui_action_t action;
    if      (!strcmp(field, "hunger"))  action = GAME_UI_SET_HUNGER;
    else if (!strcmp(field, "mood"))    action = GAME_UI_SET_MOOD;
    else if (!strcmp(field, "energy"))  action = GAME_UI_SET_ENERGY;
    else if (!strcmp(field, "hygiene")) action = GAME_UI_SET_HYGIENE;
    else if (!strcmp(field, "bond"))    action = GAME_UI_SET_BOND;
    else if (!strcmp(field, "sick"))    action = GAME_UI_SET_SICK;
    else if (!strcmp(field, "auto_feed")) action = GAME_UI_SET_AUTO_FEED;
    else if (!strcmp(field, "auto_hunt")) action = GAME_UI_SET_AUTO_HUNT;
    else if (!strcmp(field, "volume"))  action = GAME_UI_SET_VOLUME;
    else if (!strcmp(field, "music_enabled")) action = GAME_UI_SET_MUSIC;
    else if (!strcmp(field, "usb_sounds")) action = GAME_UI_SET_USB_SOUNDS;
    else if (!strcmp(field, "music_volume")) action = GAME_UI_SET_MUSIC_VOLUME;
    else if (!strcmp(field, "music_track")) action = GAME_UI_SET_MUSIC_TRACK;
    else if (!strcmp(field, "muted")) action = GAME_UI_SET_MUTED;
    else if (!strcmp(field, "decor_locked")) action = GAME_UI_SET_DECOR_LOCKED;
    else if (!strcmp(field, "auto_clean")) action = GAME_UI_SET_AUTO_CLEAN;
    else {
        printf("ERR unknown field \"%s\" (hunger mood energy hygiene bond sick level auto_feed auto_hunt volume music_enabled music_volume music_track usb_sounds muted decor_locked auto_clean)\n",
               field);
        return;
    }
    printf("%s set %s=%d\n", game_ui_call_value(action, value) ? "OK" : "ERR", field, value);
}

/* Rebuild the visual layers. Useful after changing colours or the save. */
static void cmd_refresh(void)
{
    printf("%s refresh\n", game_ui_call(GAME_UI_REFRESH) ? "OK" : "ERR");
}

/* Open a menu by name, so menu rendering can be tested without tapping.
 *
 * The menu names go through game_open_menu(), which calls the same menu_open()
 * the UI calls. Opening "sd" queues the same probe as Settings; merely opening
 * "wifi" does not start the radio.
 *
 * This runs on the CONSOLE task, so it must NOT call lv_* directly. Opening a
 * menu creates and deletes LVGL objects; doing that off the LVGL thread
 * corrupted LVGL's event list and crashed with "LoadProhibited in
 * lv_event_mark_deleted". game_ui_open_menu() marshals it onto the LVGL task. */
static void cmd_open(const char *name)
{
    if (game_ui_open_menu(name)) {
        printf("OK open %s\n", name);
    } else {
        printf("ERR unknown menu \"%s\" "
               "(main settings wifi saved password ota about species sd theme sound ach reset gift chrome nochrome personality today diary training calendar album photos generations close)\n",
               name);
    }
}

static void cmd_reset(void)
{
    if (!save_request_erase()) { printf("ERR reset not queued\n"); return; }
    unsigned waited = 0;
    while (save_busy() && waited < 5000) {
        vTaskDelay(pdMS_TO_TICKS(20));
        waited += 20;
    }
    if (save_busy() || !save_last_ok()) printf("ERR reset incomplete or failed\n");
    else printf("OK reset (reboot to take effect)\n");
}

static void cmd_reboot(void)
{
    printf("OK rebooting\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}

/* ------------------------------------------------------------------ */
/* Dispatch                                                            */
/* ------------------------------------------------------------------ */

static void run_line(char *line)
{
    /* Trim. */
    while (*line == ' ' || *line == '\t') line++;
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' ||
                       line[len - 1] == ' '  || line[len - 1] == '\t')) {
        line[--len] = '\0';
    }
    if (len == 0) return;

    /* Split the command from its arguments. `arg` points at the rest of the
     * line, which may itself contain a space (for "set FIELD VALUE"). */
    char *arg = strchr(line, ' ');
    if (arg) {
        *arg++ = '\0';
        while (*arg == ' ') arg++;
    }

    int n = arg ? atoi(arg) : 0;

    if      (!strcmp(line, "help"))     cmd_help();
    else if (!strcmp(line, "mem"))      cmd_mem();
    else if (!strcmp(line, "stat"))     cmd_stat();
    else if (!strcmp(line, "radio"))    cmd_radio();
    else if (!strcmp(line, "wifi"))     cmd_wifi(arg);
    else if (!strcmp(line, "rtc"))      cmd_rtc();
    else if (!strcmp(line, "buttons"))  cmd_buttons(arg);
    else if (!strcmp(line, "battery")) {
        char status[80];
        power_status(status, sizeof status);
        printf("%s battery %s policy=%d screen_sleep=%d wake_cause=%d\n",
               power_test() ? "OK" : "ERR", status, power_test(), power_sleeping(),
               esp_sleep_get_wakeup_cause());
    }
    else if (!strcmp(line, "wake")) {
        printf("%s wake\n", game_ui_call(GAME_UI_WAKE) ? "OK" : "ERR");
    }
    else if (!strcmp(line, "nap")) {
        char *end = NULL;
        long seconds = arg ? strtol(arg, &end, 10) : 0;
        if (!arg || *end || seconds < 5 || seconds > 86400)
            printf("ERR nap requires 5..86400 seconds; right button also wakes\n");
        else printf("%s nap queued=%ld; disconnects USB; timer/right button wake\n",
                    game_ui_call_value(GAME_UI_DEEP_SLEEP, (int)seconds) ? "OK" : "ERR", seconds);
    }
    else if (!strcmp(line, "screenshot")) cmd_screenshot(arg);
    else if (!strcmp(line, "capturetest")) {
        printf("%s capturetest\n", game_ui_call(GAME_UI_SCREEN_TEST) ? "OK" : "ERR");
    }
    else if (!strcmp(line, "audiotest")) {
        printf("%s audiotest\n", audio_test() ? "OK" : "ERR");
    }
    else if (!strcmp(line, "sound")) {
        char *end = NULL;
        long effect = arg ? strtol(arg, &end, 10) : -1;
        if (!arg || *end || effect < 0 || effect >= SFX_COUNT) printf("ERR sound requires effect 0..%d\n", SFX_COUNT - 1);
        else if (audio_muted()) printf("OK sound muted\n");
        else printf("%s sound queued=%ld\n", audio_play((sfx_t)effect) ? "OK" : "ERR", effect);
    }
    else if (!strcmp(line, "sd")) {
        if (arg && (!strcmp(arg, "backup") || !strcmp(arg, "restore"))) {
            bool restore = !strcmp(arg, "restore");
            if (!game_ui_call(restore ? GAME_UI_SD_RESTORE : GAME_UI_SD_BACKUP)) {
                printf("ERR SD %s not queued\n", arg); return;
            }
            for (int i = 0; i < 500 && save_busy(); i++) vTaskDelay(pdMS_TO_TICKS(20));
            printf("%s SD %s: %s\n", !save_busy() && save_last_ok() ? "OK" : "ERR",
                   arg, sd_status_text());
        } else if (arg && !strcmp(arg, "status")) {
            if (!game_ui_open_menu("sd")) { printf("ERR SD probe not queued\n"); return; }
            for (int i = 0; i < 500 && save_busy(); i++) vTaskDelay(pdMS_TO_TICKS(20));
            printf("%s SD present=%d backup=%d status=%s\n",
                !save_busy() && save_last_ok() ? "OK" : "ERR",
                sd_present(), sd_backup_exists(), sd_status_text());
        } else printf("ERR use sd backup, sd restore or sd status\n");
    }
    else if (!strcmp(line, "gifttest")) {
        if (!game_ui_call(GAME_UI_TEST_GIFTS)) printf("ERR gift test UI request failed\n");
        else printf("OK gifts tested=%d removed_neutral=1 economy=1 visits=1 blocks=1\n",
                    GIFT_KINDS);
    }
    else if (!strcmp(line, "giftpath")) {
        /* Report the placed gift's path. The gift is always walked over now --
         * its path control was removed -- so this is a read-only probe that
         * confirms the stored byte and that the canopy band is never used. */
        printf("GIFTPATH used=%u kind=%u path=%u canopy=%d mode=%u existing=%d\n",
               g.gift.used, g.gift.kind, g.gift_path, 0,
               g.gift_mode, game_placement_existing() ? 1 : 0);
        printf("OK giftpath\n");
    }
    else if (!strcmp(line, "giftreport")) {
        terrarium_gift_report();
        printf("OK giftreport\n");
    }
    else if (!strncmp(line, "giftplace", 9)) {
        /* Place a gift of KIND at X Y through the same commit path the UI uses,
         * so the layer and the preview teardown are exercised for real. */
        int kind = 0, x = 240, y = 240;
        if (arg && sscanf(arg, "%d %d %d", &kind, &x, &y) != 3) {
            printf("ERR usage: giftplace KIND X Y\n");
            return;
        }
        if (kind < 0 || kind >= GIFT_KINDS) {
            printf("ERR giftplace kind 0..%d\n", GIFT_KINDS - 1);
            return;
        }
        if (!game_place_gift_for_test((uint8_t)kind, (int16_t)x, (int16_t)y)) {
            printf("ERR giftplace failed\n");
            return;
        }
        printf("OK giftplace kind=%d\n", kind);
    }
    else if (!strcmp(line, "giftlayer")) {
        /* Report or set the gift's layer within its band. The gift is a single
         * item, so its layer is its position among the decor sharing its band,
         * which is what the player is actually arranging. */
        if (arg) {
            int index = atoi(arg);
            if (!decor_gift_set_layer(g.gift_path, index)) {
                printf("ERR giftlayer requires 0..%d\n",
                       decor_gift_layer_count(g.gift_path) - 1);
                return;
            }
            terrarium_refresh_gift();
            game_save_now();
        }
        printf("GIFTLAYER index=%d count=%d path=%u\n",
               decor_gift_layer_index(g.gift_path),
               decor_gift_layer_count(g.gift_path), g.gift_path);
        printf("OK giftlayer\n");
    }
    else if (!strcmp(line, "uitest")) {
        if (!game_ui_call(GAME_UI_TEST_UI)) printf("ERR UI test request failed\n");
    }
    else if (!strcmp(line, "hunttest")) {
        if (!game_ui_call(GAME_UI_TEST_HUNT)) printf("ERR hunt test request or assertion failed\n");
    }
    else if (!strcmp(line, "gaittest")) {
        if (!game_ui_call(GAME_UI_TEST_GAIT)) printf("ERR gait test request or assertion failed\n");
    }
    else if (!strcmp(line, "mindtest")) {
        /* The mind is a pure rules engine, so it can be checked directly
         * without touching the FSM. */
        bool ok = mind_test();
        printf("MIND want=%s rule=%s thought=\"%s\" ok=%d\n",
               mind_want_name(mind_want()),
               mind_active_rule() ? mind_active_rule()->name : "none",
               mind_thought(), ok ? 1 : 0);
        if (!ok) printf("ERR mind test failed\n");
        else printf("OK mindtest\n");
    }
    else if (!strcmp(line, "mind")) {
        /* Report the live intent, so the brain can be observed on hardware
         * rather than only asserted in a test. The gate is included because a
         * mind that is not being ticked looks identical to one that is idle. */
        printf("MIND want=%s rule=%s thought=\"%s\" held=%lums period=%u "
               "paused=%d chrome=%d screen=%d\n",
               mind_want_name(mind_want()),
               mind_active_rule() ? mind_active_rule()->name : "none",
               mind_thought(), (unsigned long)mind_held_ms(lv_tick_get()),
               (unsigned)life_period_now(),
               game_paused() ? 1 : 0,
               ui_hud_chrome_visible() ? 1 : 0,
               lv_screen_active() == ui.screen ? 1 : 0);
        printf("OK mind\n");
    }
    else if (!strcmp(line, "ambienttest")) {
        bool ok = ambient_test();
        printf("AMBIENT ok=%d\n", ok ? 1 : 0);
        if (!ok) printf("ERR ambient test failed\n");
        else printf("OK ambienttest\n");
    }
    else if (!strcmp(line, "ambient")) {
        /* `ambient off` / `ambient on` toggles the whole ambient layer. This is
         * the A/B lever for the frame budget: the ambient objects are cheap to
         * simulate but each one is a dirty rectangle, and the flush area is what
         * actually costs. Without a toggle there is no way to attribute a
         * slowdown to the layer rather than to the game. */
        if (arg && !strcmp(arg, "off")) {
            ambient_set_visible(false);
            printf("AMBIENT visible=0\n");
        } else if (arg && !strcmp(arg, "on")) {
            ambient_set_visible(true);
            printf("AMBIENT visible=1\n");
        } else {
            ambient_report();
        }
        printf("OK ambient\n");
    }
    else if (!strcmp(line, "rendercost")) {
        game_render_cost_report();
        printf("OK rendercost\n");
    }
    else if (!strcmp(line, "dayperiod")) {
        /* Force the clock so the tint and the shafts can be exercised at a
         * chosen time of day. `dayperiod` with no argument clears the override.
         * The RTC and the system clock are separate, so `rtcset` alone does not
         * move the tint. */
        if (!arg) {
            life_clock_override(0);
            printf("DAYPERIOD override=cleared\n");
        } else {
            uint32_t epoch = (uint32_t)strtoul(arg, NULL, 10);
            life_clock_override(epoch);
            life_clock_t c = life_clock_now();
            printf("DAYPERIOD override=%lu available=%d period=%u phase=%s\n",
                   (unsigned long)epoch, c.available ? 1 : 0, (unsigned)c.period,
                   life_phase_name(c.period));
        }
        printf("OK dayperiod\n");
    }
    else if (!strcmp(line, "preytest")) {
        if (!game_ui_call(GAME_UI_TEST_PREY)) printf("ERR prey test request or assertion failed\n");
    }
    else if (!strcmp(line, "decortest")) {
        if (!game_ui_call(GAME_UI_DECOR_TEST)) printf("ERR decor test request or assertion failed\n");
        else printf("OK decortest placed=%d slots=%d\n", DECOR_COUNT, DECOR_SLOTS);
    }
    else if (!strcmp(line, "spiderpos")) {
        /* Teleport the spider and report where it actually ended up.
         *
         * The spider wanders faster than a script can issue commands, so a
         * blocking rule cannot be checked by walking it into an obstacle: by the
         * time the next command arrives it has moved on. Writing the position
         * and reading it back after a tick shows whether the separation pass
         * pushed it out, which is the only way to prove Walk Behind blocks. */
        int px = 0, py = 0;
        if (!arg || sscanf(arg, "%d %d", &px, &py) != 2) {
            printf("ERR usage: spiderpos X Y\n");
            return;
        }
        g.spider_x = (int16_t)px;
        g.spider_y = (int16_t)py;
        printf("OK spiderpos want=(%d,%d)\n", px, py);
    }
    else if (!strcmp(line, "climb")) {
        /* Report the live climb state at the spider's position. This is the only
         * way to confirm on hardware that the lift is actually being applied,
         * since the lift is a draw-time value and never touches the save.
         *
         * "climb N" pins the lift to N pixels and "climb -1" returns to normal.
         * The spider wanders, so a scripted check cannot reliably keep it
         * standing on a climbable item long enough to inspect the shadow. */
        if (arg) {
            int32_t forced = (int32_t)atoi(arg);
            decor_climb_force(forced);
            printf("OK climb forced=%ld\n", (long)forced);
        } else {
            int slot = -1;
            uint8_t height = decor_climb_at(g.spider_x, g.spider_y, &slot);
            printf("CLIMB pos=(%d,%d) height=%u slot=%d lift=%ld\n",
                   g.spider_x, g.spider_y, height, slot,
                   (long)decor_climb_lift(g.spider_x, g.spider_y, game_now()));
            cspider_shadow_report();
            printf("OK climb\n");
        }
    }
    else if (!strcmp(line, "climbat")) {
        /* Sample the climb ramp at an arbitrary point. The `climb` command only
         * reports where the spider happens to be standing, which cannot confirm
         * that the ramp spans the artwork rather than the collision footprint:
         * the spider wanders off before a script can walk it across the whole
         * structure. */
        int px = 0, py = 0;
        if (!arg || sscanf(arg, "%d %d", &px, &py) != 2) {
            printf("ERR usage: climbat X Y\n");
            return;
        }
        int slot = -1;
        uint8_t height = decor_climb_at((int16_t)px, (int16_t)py, &slot);
        float slope = decor_climb_slope((int16_t)px, (int16_t)py, NULL);
        printf("CLIMBAT pos=(%d,%d) height=%u slot=%d slope=%.3f\n",
               px, py, height, slot, (double)slope);
        printf("OK climbat\n");
    }
    else if (!strcmp(line, "blockat")) {
        /* Sample the blocking test at an arbitrary point. `climbat` reports the
         * lift, which is a different question: a structure can block a point
         * without lifting the spider there (Walk Behind blocks the whole
         * artwork, Climb lifts along the ramp). The ends of a diagonal log lie
         * outside a circle of radius DECOR_CLIMB_EXTENT, so this is what proves
         * the span table replaced the circular test rather than supplementing
         * it. */
        int px = 0, py = 0;
        if (!arg || sscanf(arg, "%d %d", &px, &py) != 2) {
            printf("ERR usage: blockat X Y\n");
            return;
        }
        printf("BLOCKAT pos=(%d,%d) blocks=%d\n", px, py,
               decor_blocks_point((int16_t)px, (int16_t)py) ? 1 : 0);
        printf("OK blockat\n");
    }
    else if (!strcmp(line, "climbhere")) {        /* Place a climbable item under the spider and set it to Climb in one
         * command. The spider wanders faster than a script can issue separate
         * place/block/climb commands, so the footprint path could never be
         * exercised from the console without this.
         *
         * "climbhere big" uses the tallest large climb structure instead of the
         * Stone Bridge, so the ramp and the much larger lift can be inspected. */
        decor_climb_force(-1);
        uint8_t kind = (arg && !strcmp(arg, "big")) ? DECOR_CLIMB_DRIFTWOOD
                                                   : DECOR_STONE_BRIDGE;
        int slot = decor_place(kind, g.spider_x, g.spider_y);
        if (slot < 0) { printf("ERR climbhere: place failed\n"); return; }
        if (!decor_set_path(slot, DECOR_CLIMB)) {
            printf("ERR climbhere: set_path refused\n"); return;
        }
        /* decor_place() is the model layer; the renderer only picks the item up
         * when the decor layer is refreshed, which the UI normally does. */
        terrarium_refresh_decor();
        printf("OK climbhere slot=%d kind=%u pos=(%d,%d)\n", slot, kind,
               g.spider_x, g.spider_y);
    }
    else if (!strcmp(line, "perch")) {
        /* Place a small climbable prop under the spider and report the perch
         * state. The spider wanders faster than a script can issue separate
         * place/block commands, so the footprint-driven perch could not
         * otherwise be exercised from the console.
         *
         * "perch KIND" uses a specific decor kind, which is how the small
         * props whose collision radius is under PERCH_MIN_RADIUS (the Book
         * Stack is radius 12) get exercised. */
        decor_climb_force(-1);
        uint8_t kind = DECOR_LOG;
        if (arg) {
            int want = atoi(arg);
            if (want < 0 || want >= DECOR_COUNT) {
                printf("ERR perch: kind must be 0..%d\n", DECOR_COUNT - 1);
                return;
            }
            kind = (uint8_t)want;
        }
        int slot = decor_place(kind, g.spider_x, g.spider_y);
        if (slot < 0) { printf("ERR perch: place failed\n"); return; }
        if (!decor_set_path(slot, DECOR_CLIMB)) {
            printf("ERR perch: set_path refused\n"); return;
        }
        terrarium_refresh_decor();
        float want = 0.0f;
        bool has = decor_perch_heading(g.spider_x, g.spider_y, &want);
        printf("PERCH slot=%d kind=%u pos=(%d,%d) perch=%d radius=%d heading=%.3f\n",
               slot, (unsigned)kind, g.spider_x, g.spider_y,
               (int)decor_perch_at(g.spider_x, g.spider_y),
               (int)decor_perch_radius(g.spider_x, g.spider_y),
               has ? (double)want : 0.0);
        printf("OK perch\n");
    }
    else if (!strcmp(line, "decor")) cmd_decor(arg);
    else if (!strcmp(line, "decorreport")) {
        int slot = arg ? atoi(arg) : 0;
        terrarium_decor_report(slot);
        printf("OK decorreport\n");
    }
    else if (!strcmp(line, "decorlayers")) {
        /* Report the proximity-based layer count for every placed item, so the
         * "layers only between things that touch" rule is observable on
         * hardware rather than only asserted in a test. */
        int placed = 0;
        for (int i = 0; i < decor_slot_count(); i++) {
            const decor_item_t *item = decor_at(i);
            if (!item) continue;
            placed++;
            printf("DECORLAYERS slot=%d kind=%u pos=(%d,%d) band=%s "
                   "overlapping=%d index=%d\n",
                   i, (unsigned)item->kind, (int)item->x, (int)item->y,
                   item->blocking == DECOR_WALK_BEHIND ? "canopy" : "floor",
                   decor_layer_count_at(item->blocking, item->kind,
                                        item->x, item->y, i),
                   decor_layer_index(i));
        }
        printf("DECORLAYERS placed=%d gift_overlapping=%d\n", placed,
               decor_gift_layer_count(DECOR_WALK_OVER) - 1);
        printf("OK decorlayers\n");
    }
    else if (!strcmp(line, "setupdone")) {
        if (!game_ui_call(GAME_UI_SETUP_DONE)) printf("ERR setup request failed\n");
    }
    else if (!strcmp(line, "background")) {
        if (!arg) {
            printf("BACKGROUND id=%d name=%s\n", (int)(theme_current() - k_themes), theme_current()->name);
            printf("OK background\n");
        } else {
            char *end;
            long id = strtol(arg, &end, 10);
            if (*end || id < 0 || id >= THEME_COUNT || !game_ui_call_value(GAME_UI_BACKGROUND, id))
                printf("ERR invalid background or worker busy\n");
            else {
                for (int i = 0; i < 100 && save_busy(); i++) vTaskDelay(pdMS_TO_TICKS(10));
                printf("%s background\n", !save_busy() && save_last_ok() ? "OK" : "ERR");
            }
        }
    }
    else if (!strcmp(line, "border")) {
        if (!arg) printf("BORDER id=%u\nOK border\n", g.border_id);
        else {
            char *end = NULL;
            long id = !strcmp(arg, "none") ? 255 : strtol(arg, &end, 10);
            if ((end && *end) || id < 0 || (id >= THEME_COUNT && id != 255) ||
                !game_ui_call_value(GAME_UI_BORDER, id)) printf("ERR invalid border, insufficient Silk or worker busy\n");
            else {
                for (int i = 0; i < 100 && save_busy(); i++) vTaskDelay(pdMS_TO_TICKS(10));
                printf("%s border\n", !save_busy() && save_last_ok() ? "OK" : "ERR");
            }
        }
    }
    else if (!strcmp(line, "rtcset")) {
        char *end;
        long long value = arg ? strtoll(arg, &end, 10) : -1;
        if (!arg || !arg[0] || *end || value < 946684800LL || value > 4102444799LL) {
            printf("ERR rtcset requires UTC epoch in 2000..2099\n");
        } else {
            esp_err_t err = pet_rtc_write((time_t)value);
            if (err == ESP_OK) printf("OK rtcset\n");
            else printf("ERR rtcset %s\n", esp_err_to_name(err));
        }
    }
    else if (!strcmp(line, "scan"))     cmd_scan();
    else if (!strcmp(line, "scanloop")) cmd_scanloop(n);
    else if (!strcmp(line, "render"))   cmd_render(n);
    else if (!strcmp(line, "spider"))   cmd_spider();
    else if (!strcmp(line, "prey"))     cmd_prey();
    else if (!strcmp(line, "feed"))     cmd_feed();
    else if (!strcmp(line, "clean"))    cmd_clean();
    else if (!strcmp(line, "speckle"))  cmd_speckle(arg);
    else if (!strcmp(line, "pests"))    cmd_pests();
    else if (!strcmp(line, "daily"))    cmd_daily(arg);
    else if (!strcmp(line, "treat"))    cmd_treat(arg);
    else if (!strcmp(line, "play"))     cmd_play();
    else if (!strcmp(line, "pet"))      cmd_pet();
    else if (!strcmp(line, "sleep"))    cmd_sleep();
    else if (!strcmp(line, "menu"))     cmd_menu();
    else if (!strcmp(line, "open"))     cmd_open(arg ? arg : "");
    else if (!strcmp(line, "pick")) {
        char *end = NULL;
        long id = arg ? strtol(arg, &end, 10) : -1;
        if (!arg || end == arg || *end || id < 0 || id > 255)
            printf("ERR usage: pick ID (0..255)\n");
        else printf("%s pick %ld\n", game_ui_call_value(GAME_UI_MENU_PICK, (int)id) ? "OK" : "ERR", id);
    }
    else if (!strcmp(line, "spawn"))    cmd_spawn(arg ? n : -1);
    else if (!strcmp(line, "state"))    cmd_state(n);
    else if (!strcmp(line, "refresh"))  cmd_refresh();
    else if (!strcmp(line, "save"))     cmd_save_tool(arg);
    else if (!strcmp(line, "silk"))     cmd_silk(arg);
    else if (!strcmp(line, "reset"))    cmd_reset();
    else if (!strcmp(line, "reboot"))   cmd_reboot();
    else if (!strcmp(line, "otatest")) printf("%s otatest\n", net_ota_test() ? "OK" : "ERR");
    else if (!strcmp(line, "set")) {
        /* "set FIELD VALUE" -- split the argument again. */
        char *field = arg;
        char *val   = field ? strchr(field, ' ') : NULL;
        if (!field || !val) {
            printf("ERR usage: set FIELD VALUE\n");
            return;
        }
        *val++ = '\0';
        while (*val == ' ' || *val == '\t') val++;
        if (!strcmp(field, "usb_sounds") && strcmp(val, "0") && strcmp(val, "1")) {
            printf("ERR usb_sounds requires 0 or 1\n");
            return;
        }
        cmd_set(field, atoi(val));
    }
    else printf("ERR unknown command \"%s\" (try help)\n", line);
}

/* ------------------------------------------------------------------ */
/* Task                                                                */
/* ------------------------------------------------------------------ */



static void console_task(void *arg)
{
    (void)arg;

    char *line = heap_caps_malloc(LINE_MAX_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!line) { ESP_LOGE(TAG, "console line PSRAM exhausted"); vTaskDelete(NULL); return; }
    int  len = 0;
    bool overflow = false;

    printf("OK console ready (type help)\n");

    for (;;) {
        /* Read the USB Serial/JTAG driver's RX buffer.
         *
         * This board's console is the ESP32-S3's built-in USB Serial/JTAG
         * (VID:PID 303A:1001), not the UART bridge. fgets() on stdin does not
         * work here -- the console VFS does not route stdin to this transport,
         * so the task blocked forever while the device ran normally.
         *
         * Only ONE driver is installed, for the transport actually in use. An
         * earlier version installed both a UART driver and a USB-JTAG driver,
         * which cost about 18 KB of internal RAM and dropped the largest free
         * DMA block from 51200 to 32768 -- the pool the LCD needs for its draw
         * buffers. That is what corrupted the screen when opening Settings.
         *
         * The zero timeout makes this a poll, so the task yields and never
         * spins. */
        uint8_t ch = 0;
        int got = usb_serial_jtag_read_bytes(&ch, 1, 0);
        if (got != 1) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        int c = ch;

        if (c == '\r' || c == '\n') {
            if (overflow) {
                printf("ERR command too long\n");
                overflow = false;
                len = 0;
            } else if (len > 0) {
                line[len] = '\0';
                run_line(line);
                memset(line, 0, LINE_MAX_LEN);
                len = 0;
            }
            continue;
        }

        /* Backspace. */
        if (c == 0x08 || c == 0x7F) {
            if (len > 0) len--;
            continue;
        }

        if (len < LINE_MAX_LEN - 1) {
            line[len++] = (char)c;
        } else overflow = true;
    }
}

void test_console_init(void)
{
    /* Install ONLY the USB Serial/JTAG driver.
     *
     * One transport, one driver. The console VFS already owns the port for
     * OUTPUT (the log), and this driver gives us an RX ring buffer for INPUT.
     * uart_vfs_dev_use_driver() is deliberately not called: it would route
     * stdin through the same buffer this task reads, and the two compete. */
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.rx_buffer_size = 1024; /* Holds the complete v9 raw-restore command. */
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    ESP_LOGI(TAG, "usb-jtag driver: %s", esp_err_to_name(err));
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "console input unavailable");
        return;
    }

    /* Its own task, because reading blocks and the LVGL task must never block. */
    BaseType_t ok = xTaskCreateWithCaps(console_task, "spidarium_cmd", 8192, NULL, 2, NULL,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "console task create failed");
        return;
    }
    ESP_LOGI(TAG, "serial test console ready");
}
