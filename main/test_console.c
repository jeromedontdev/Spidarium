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

#include "cspider.h"
#include "behaviour.h"
#include "game.h"
#include "game_config.h"
#include "meminfo.h"
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
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "spidarium.cmd";

#define LINE_MAX_LEN 384

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
    printf("  wifi ota HTTPS_URL   manual firmware update\n");
    printf("  rtc                  read hardware RTC and system clock\n");
    printf("  rtcset EPOCH          set RTC to UTC epoch (manual recovery)\n");
    printf("  gifttest             verify 18 effective bonuses and removal\n");
    printf("  uitest               test paging, keyboard, password, HUD, backgrounds\n");
    printf("  hunttest             inject touch, jump/catch, restore pet/prey\n");
    printf("  gaittest             cadence and bounded leg geometry checks\n");
    printf("  preytest             eight level gates, restore pet level\n");
    printf("  background [0-7]     show/set persistent pixel-art background\n");
    printf("  scan                 run a WiFi scan\n");
    printf("  scanloop N           N scans, check memory drift\n");
    printf("  render N             N render ticks, check memory drift\n");
    printf("  feed                 press FEED\n");
    printf("  clean                press CLEAN\n");
    printf("  play                 press PLAY\n");
    printf("  sleep                press SLEEP\n");
    printf("  menu                 open the main menu\n");
    printf("  open NAME            open a menu (main gift chrome nochrome)\n");
    printf("  spawn [SPECIES]      spawn unlocked bug (0..7, or random)\n");
    printf("  state N              force spider state\n");
    printf("  set FIELD VALUE      needs; auto_feed/auto_hunt accept 0 or 1\n");
    printf("  refresh              rebuild the visual layers\n");
    printf("  save                 save now\n");
    printf("  save export|schema  browser backup and field layout\n");
    printf("  save restore HEX    validated pet restore; reboot required\n");
    printf("  screenshot           capture actual LVGL RGB565 screen\n");
    printf("  sd backup|status     verified SD backup or card probe\n");
    printf("  sound N              play effect 0..9 (7..9 ambient)\n");
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
           "hygiene=%u level=%lu stage=%u sick=%u bond=%u prey=%d\n",
           (unsigned)g.state, g.spider_x, g.spider_y, (double)g.spider_heading,
           g.hunger, g.mood, g.energy, g.hygiene,
           (unsigned long)g.level, (unsigned)g.stage, (unsigned)g.sick,
           (unsigned)g.bond, prey_count());
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
    } else if (!strncmp(arg, "ota ", 4)) accepted = net_ota_start(arg + 4);
    else { printf("ERR unknown wifi action\n"); return; }
    if (!accepted) { printf("ERR wifi request rejected\n"); return; }
    int waited = 0;
    while (net_busy() && waited < 150000) {
        vTaskDelay(pdMS_TO_TICKS(100));
        waited += 100;
    }
    if (net_busy()) printf("ERR wifi timeout\n");
    else if (net_scan_error()[0]) printf("ERR wifi %s\n", net_scan_error());
    else printf("OK wifi %s\n", net_status_text());
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

    for (int i = 0; i < count; i++) {
        if (!game_ui_call(GAME_UI_RENDER)) {
            if (!game_ui_call(GAME_UI_SCANLOOP_END))
                printf("ERR render could not restore gameplay\n");
            printf("ERR render stopped after %d of %d ticks\n", i, count);
            return;
        }
    }

    vTaskDelay(pdMS_TO_TICKS(100));
    print_mem_line("render-after");
    size_t last_largest = internal_largest();
    long drift = (long)last_largest - (long)first_largest;

    printf("RENDER ticks=%d largest_drift=%ld\n", count, drift);

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
#undef SAVE_FIELD
    };
    enum { SCHEMA_CAP = 3072 };
    char *json = heap_caps_malloc(SCHEMA_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!json) { printf("ERR save schema allocation failed\n"); return; }
    int pos = snprintf(json, SCHEMA_CAP,
        "{\"magic\":%u,\"version\":%u,\"headerSize\":12,\"payloadSizes\":[%u,%u],\"fields\":[",
        (unsigned)SAVE_MAGIC, SAVE_VERSION, (unsigned)offsetof(game_state_t, silk),
        (unsigned)sizeof(game_state_t));
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
    if (!strcmp(arg, "export")) {
        if (!save_restore_pending() && !game_ui_call(GAME_UI_SAVE)) {
            printf("ERR save export snapshot not queued\n"); return;
        }
        if (!wait_save()) { printf("ERR save export persistence failed\n"); return; }
        uint8_t blob[sizeof(game_state_t) + 12];
        size_t size = save_read_raw(blob, sizeof blob);
        if (!size || !save_raw_valid(blob, size)) {
            printf("ERR save export invalid or missing blob\n"); return;
        }
        char hex[sizeof blob * 2 + 1];
        for (size_t i = 0; i < size; i++) snprintf(hex + i * 2, 3, "%02x", blob[i]);
        esp_log_level_t level = esp_log_level_get("*");
        esp_log_level_set("*", level > ESP_LOG_WARN ? ESP_LOG_WARN : level);
        bool sent = console_data("SAVE HEX ") && console_data(hex) &&
                    console_data("\nOK save export\n");
        esp_log_level_set("*", level);
        if (!sent) printf("ERR save export transfer incomplete\n");
        return;
    }
    if (!strncmp(arg, "restore ", 8)) {
        const char *hex = arg + 8;
        size_t length = strlen(hex);
        uint8_t blob[sizeof(game_state_t) + 12];
        if (length % 2 || length < 24 || length > sizeof blob * 2) {
            printf("ERR save restore invalid hex length\n"); return;
        }
        for (size_t i = 0; i < length / 2; i++) {
            int hi = hex_digit(hex[i * 2]), lo = hex_digit(hex[i * 2 + 1]);
            if (hi < 0 || lo < 0) { printf("ERR save restore invalid hex\n"); return; }
            blob[i] = (hi << 4) | lo;
        }
        if (!save_request_raw(blob, length / 2)) {
            printf("ERR save restore validation or queue rejected\n"); return;
        }
        printf("%s save restore; reboot required\n", wait_save() ? "OK" : "ERR");
        return;
    }
    printf("ERR use save export|schema|restore HEX\n");
}

static void cmd_screenshot(const char *arg)
{
    if (arg && strcmp(arg, "spider")) { printf("ERR use screenshot [spider]\n"); return; }
    bool spider = arg != NULL;
    if (!game_ui_call_value(GAME_UI_SCREEN_CAPTURE, spider ? 1 : 0)) {
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
    printf("%s clean\n", game_ui_call(GAME_UI_CLEAN) ? "OK" : "ERR");
}

static void cmd_play(void)
{
    printf("%s play\n", game_ui_call(GAME_UI_PLAY) ? "OK" : "ERR");
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
    /* SPIDER_SICK is the last enumerator; there is no COUNT constant. */
    if (state < 0 || state > SPIDER_SICK) {
        printf("ERR state must be 0..%d\n", (int)SPIDER_SICK);
        return;
    }
    printf("%s state=%d\n", game_ui_call_value(GAME_UI_STATE, state) ? "OK" : "ERR", state);
}

/* Set a need directly, so a UI state can be reached without waiting. */
static void cmd_set(const char *field, int value)
{
    if ((!strcmp(field, "auto_feed") || !strcmp(field, "auto_hunt")) &&
        (value < 0 || value > 1)) {
        printf("ERR automation checkboxes require 0 or 1\n");
        return;
    }
    if (value < 0) value = 0;
    if (value > 100) value = 100;

    game_ui_action_t action;
    if      (!strcmp(field, "hunger"))  action = GAME_UI_SET_HUNGER;
    else if (!strcmp(field, "mood"))    action = GAME_UI_SET_MOOD;
    else if (!strcmp(field, "energy"))  action = GAME_UI_SET_ENERGY;
    else if (!strcmp(field, "hygiene")) action = GAME_UI_SET_HYGIENE;
    else if (!strcmp(field, "bond"))    action = GAME_UI_SET_BOND;
    else if (!strcmp(field, "sick"))    action = GAME_UI_SET_SICK;
    else if (!strcmp(field, "auto_feed")) action = GAME_UI_SET_AUTO_FEED;
    else if (!strcmp(field, "auto_hunt")) action = GAME_UI_SET_AUTO_HUNT;
    else {
        printf("ERR unknown field \"%s\" (hunger mood energy hygiene bond sick auto_feed auto_hunt)\n",
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
               "(main settings wifi saved password ota about species sd theme sound ach reset gift chrome nochrome close)\n",
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
    else if (!strcmp(line, "battery")) {
        char status[80];
        power_status(status, sizeof status);
        printf("%s battery %s policy=%d screen_sleep=%d\n",
               power_test() ? "OK" : "ERR", status, power_test(), power_sleeping());
    }
    else if (!strcmp(line, "wake")) {
        printf("%s wake\n", game_ui_call(GAME_UI_WAKE) ? "OK" : "ERR");
    }
    else if (!strcmp(line, "screenshot")) cmd_screenshot(arg);
    else if (!strcmp(line, "sound")) {
        char *end = NULL;
        long effect = arg ? strtol(arg, &end, 10) : -1;
        if (!arg || *end || effect < 0 || effect >= SFX_COUNT) printf("ERR sound requires effect 0..%d\n", SFX_COUNT - 1);
        else if (audio_muted()) printf("OK sound muted\n");
        else printf("%s sound queued=%ld\n", audio_play((sfx_t)effect) ? "OK" : "ERR", effect);
    }
    else if (!strcmp(line, "sd")) {
        if (arg && !strcmp(arg, "backup")) {
            if (!game_ui_call(GAME_UI_SD_BACKUP)) { printf("ERR SD backup not queued\n"); return; }
            for (int i = 0; i < 500 && save_busy(); i++) vTaskDelay(pdMS_TO_TICKS(20));
            printf("%s SD backup: %s\n", !save_busy() && save_last_ok() ? "OK" : "ERR",
                   sd_status_text());
        } else if (arg && !strcmp(arg, "status")) {
            if (!game_ui_open_menu("sd")) { printf("ERR SD probe not queued\n"); return; }
            for (int i = 0; i < 500 && save_busy(); i++) vTaskDelay(pdMS_TO_TICKS(20));
            printf("%s SD present=%d backup=%d status=%s\n",
                !save_busy() && save_last_ok() ? "OK" : "ERR",
                sd_present(), sd_backup_exists(), sd_status_text());
        } else printf("ERR use sd backup or sd status\n");
    }
    else if (!strcmp(line, "gifttest")) {
        if (!game_ui_call(GAME_UI_TEST_GIFTS)) printf("ERR gift test UI request failed\n");
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
    else if (!strcmp(line, "preytest")) {
        if (!game_ui_call(GAME_UI_TEST_PREY)) printf("ERR prey test request or assertion failed\n");
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
    else if (!strcmp(line, "play"))     cmd_play();
    else if (!strcmp(line, "sleep"))    cmd_sleep();
    else if (!strcmp(line, "menu"))     cmd_menu();
    else if (!strcmp(line, "open"))     cmd_open(arg ? arg : "");
    else if (!strcmp(line, "spawn"))    cmd_spawn(arg ? n : -1);
    else if (!strcmp(line, "state"))    cmd_state(n);
    else if (!strcmp(line, "refresh"))  cmd_refresh();
    else if (!strcmp(line, "save"))     cmd_save_tool(arg);
    else if (!strcmp(line, "reset"))    cmd_reset();
    else if (!strcmp(line, "reboot"))   cmd_reboot();
    else if (!strcmp(line, "set")) {
        /* "set FIELD VALUE" -- split the argument again. */
        char *field = arg;
        char *val   = field ? strchr(field, ' ') : NULL;
        if (!field || !val) {
            printf("ERR usage: set FIELD VALUE\n");
            return;
        }
        *val++ = '\0';
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

    char line[LINE_MAX_LEN];
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
                memset(line, 0, sizeof line);
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
    cfg.rx_buffer_size = 512; /* One complete 301-byte browser restore command. */
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
