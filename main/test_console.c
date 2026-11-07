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
#include "prey.h"
#include "save.h"
#include "spider.h"
#include "terrarium.h"
#include "ui_hud.h"

#include "driver/usb_serial_jtag.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "spidarium.cmd";

#define LINE_MAX_LEN 64

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
    printf("  scan                 run a WiFi scan\n");
    printf("  scanloop N           N scans, check memory drift\n");
    printf("  render N             N render ticks, check memory drift\n");
    printf("  feed                 press FEED\n");
    printf("  clean                press CLEAN\n");
    printf("  play                 press PLAY\n");
    printf("  sleep                press SLEEP\n");
    printf("  menu                 open the main menu\n");
    printf("  open NAME            open a menu (main gift chrome nochrome)\n");
    printf("  spawn [SPECIES]      spawn a bug (0..3, or random)\n");
    printf("  state N              force spider state\n");
    printf("  set FIELD VALUE      set a need (hunger mood energy hygiene bond sick)\n");
    printf("  refresh              rebuild the visual layers\n");
    printf("  save                 save now\n");
    printf("  reset                erase the save\n");
    printf("  reboot               restart\n");
}

static void cmd_mem(void)
{
    mem_report_detail("cmd");
    print_mem_line("cmd");
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

    print_mem_line("loop-after");

    size_t last_largest = internal_largest();
    long drift = (long)last_largest - (long)first_largest;

    printf("LOOP ok=%d failed=%d largest_drift=%ld\n", ok, failed, drift);

    /* A drift of more than 4 KB across the loop means something is not being
     * released. That is the failure mode that broke the display. */
    if (drift < -4096) {
        printf("ERR memory leaked %ld bytes across %d scans\n", -drift, count);
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

    print_mem_line("render-before");
    size_t first_largest = internal_largest();

    for (int i = 0; i < count; i++) {
        cspider_render(lv_tick_get());
        if ((i % 50) == 0) vTaskDelay(pdMS_TO_TICKS(1));
    }

    print_mem_line("render-after");
    size_t last_largest = internal_largest();
    long drift = (long)last_largest - (long)first_largest;

    printf("RENDER ticks=%d largest_drift=%ld\n", count, drift);

    if (drift < -4096) {
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
    game_save_now();
    printf("OK save\n");
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
    game_ui_call(GAME_UI_FEED);
    printf("OK feed\n");
}

static void cmd_clean(void)
{
    game_ui_call(GAME_UI_CLEAN);
    printf("OK clean\n");
}

static void cmd_play(void)
{
    game_ui_call(GAME_UI_PLAY);
    printf("OK play\n");
}

static void cmd_sleep(void)
{
    game_ui_call(GAME_UI_SLEEP);
    printf("OK sleep\n");
}

static void cmd_menu(void)
{
    game_ui_call(GAME_UI_MENU);
    printf("OK menu\n");
}

/* Spawn a bug of a given species index, or a random one. */
static void cmd_spawn(int species)
{
    if (species < 0 || species >= PREY_SPECIES_COUNT) {
        prey_spawn();
        printf("OK spawn (random)\n");
    } else {
        prey_spawn_species((prey_species_t)species);
        printf("OK spawn species=%d\n", species);
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
    behaviour_set_state((spider_state_t)state);
    printf("OK state=%d\n", state);
}

/* Set a need directly, so a UI state can be reached without waiting. */
static void cmd_set(const char *field, int value)
{
    if (value < 0) value = 0;
    if (value > 100) value = 100;

    if      (!strcmp(field, "hunger"))  g.hunger  = (uint8_t)value;
    else if (!strcmp(field, "mood"))    g.mood    = (uint8_t)value;
    else if (!strcmp(field, "energy"))  g.energy  = (uint8_t)value;
    else if (!strcmp(field, "hygiene")) g.hygiene = (uint8_t)value;
    else if (!strcmp(field, "bond"))    g.bond    = (uint8_t)value;
    else if (!strcmp(field, "sick"))    g.sick    = (uint8_t)(value ? 1 : 0);
    else {
        printf("ERR unknown field \"%s\" (hunger mood energy hygiene bond sick)\n",
               field);
        return;
    }
    printf("OK set %s=%d\n", field, value);
}

/* Rebuild the visual layers. Useful after changing colours or the save. */
static void cmd_refresh(void)
{
    spider_refresh_colours();
    terrarium_rebuild();
    printf("OK refresh\n");
}

/* Open a menu by name, so menu rendering can be tested without tapping.
 *
 * The menu names go through game_open_menu(), which calls the same menu_open()
 * the UI calls. That matters for "wifi": opening that page starts a scan, which
 * is the path that reported "radio unavailable".
 *
 * This runs on the CONSOLE task, so it must NOT call lv_* directly. Opening a
 * menu creates and deletes LVGL objects; doing that off the LVGL thread
 * corrupted LVGL's event list and crashed with "LoadProhibited in
 * lv_event_mark_deleted". game_ui_open_menu() marshals it onto the LVGL task. */
static void cmd_open(const char *name)
{
    if (!strcmp(name, "gift")) {
        game_ui_open_menu("gift");
        printf("OK open gift\n");
        return;
    }
    if (!strcmp(name, "chrome")) {
        game_ui_open_menu("chrome");
        printf("OK open chrome\n");
        return;
    }
    if (!strcmp(name, "nochrome")) {
        game_ui_open_menu("nochrome");
        printf("OK open nochrome\n");
        return;
    }

    if (game_ui_open_menu(name)) {
        printf("OK open %s\n", name);
    } else {
        printf("ERR unknown menu \"%s\" "
               "(main settings wifi sd theme sound ach reset gift chrome nochrome)\n",
               name);
    }
}

static void cmd_reset(void)
{
    save_erase();
    printf("OK reset (reboot to take effect)\n");
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
    else if (!strcmp(line, "save"))     cmd_save();
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
            if (len > 0) {
                line[len] = '\0';
                run_line(line);
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
        }
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
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    ESP_LOGI(TAG, "usb-jtag driver: %s", esp_err_to_name(err));
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "console input unavailable");
        return;
    }

    /* Its own task, because reading blocks and the LVGL task must never block. */
    BaseType_t ok = xTaskCreate(console_task, "spidarium_cmd", 3072, NULL, 2, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "console task create failed");
        return;
    }
    ESP_LOGI(TAG, "serial test console ready");
}
