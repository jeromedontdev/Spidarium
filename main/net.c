/*
 * Spidarium — WiFi station connection, implementation.
 *
 * One saved network, station mode only. The point is OTA, so this does the
 * minimum that makes an OTA possible: associate, get an IP, and stay associated.
 *
 * Credentials are stored in their own NVS namespace rather than in the game
 * save. The game save is versioned and disposable -- a factory reset erases it
 * -- and forgetting the WiFi password because the player started a new spider
 * would be obnoxious.
 */
#include "net.h"

#include "game.h"          /* game_pause / game_resume */
#include "meminfo.h"       /* mem_report_detail */

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "spidarium.net";

#define NVS_NS      "spidarium_net"
#define KEY_SSID    "ssid"
#define KEY_PASS    "pass"

/* Reconnect backoff. A failed association is usually a wrong password or an
 * out-of-range AP, neither of which fixes itself in 100 ms, so back off to a
 * slow retry rather than hammering the radio. */
#define RETRY_MIN_MS   5000u
#define RETRY_MAX_MS   60000u

/* WiFi work runs on its OWN task, not the LVGL task.
 *
 * esp_wifi_init() and esp_wifi_scan_start() both need far more stack than the
 * LVGL adapter's 8 KB thread provides -- calling them from a menu callback
 * overflowed the stack and crashed the device. Everything that touches the
 * radio is therefore marshalled onto this task, which has room for it. */
#define NET_TASK_STACK  12288
#define NET_TASK_PRIO   4

static net_state_t s_state = NET_OFF;
static char        s_ssid[33];
static char        s_pass[65];
static char        s_ip[16];
static char        s_status[48];

static uint32_t    s_retry_at;
static uint32_t    s_retry_ms = RETRY_MIN_MS;
static bool        s_started;
static bool        s_have_ip;

/* True once esp_netif_create_default_wifi_sta() and esp_wifi_init() have run.
 * Those two are NOT idempotent -- a second call leaks and can fail -- so they
 * are guarded separately from s_started, which only tracks esp_wifi_start(). */
static bool        s_wifi_inited;

/* True once the station netif exists. Tracked separately from s_wifi_inited
 * because esp_netif_create_default_wifi_sta() ASSERTS on a duplicate rather
 * than returning an error, so a partial failure must not be retried blindly. */
static bool        s_netif_created;

/* Blocking implementations, defined below. */
static bool net_scan_start_blocking(void);
static void net_connect_blocking(void);
static bool radio_up(void);
static bool radio_start(void);

/* ------------------------------------------------------------------ */
/* Exclusive execution                                                 */
/* ------------------------------------------------------------------ */
/* WiFi runs as the ONLY thing happening, and the UI NEVER waits for it.
 *
 * Two separate constraints, and getting either wrong breaks the device:
 *
 *   1. The radio must not run concurrently with the game. It allocates
 *      internal DMA-capable RAM and runs its own high-priority tasks, which
 *      starved the LCD SPI driver -- "Draw bitmap failed: ESP_ERR_NO_MEM" and a
 *      corrupted screen.
 *
 *   2. The LVGL task must never block. It has an 8 KB stack and drives the
 *      display; blocking it for the length of a WiFi scan hangs the UI and the
 *      task watchdog then reboots the device. That is exactly what the previous
 *      version did -- it waited on a semaphore for up to 15 seconds.
 *
 * So the job is handed to a worker task and the caller returns IMMEDIATELY. The
 * worker pauses the game, does the radio work, and resumes. The UI polls
 * net_busy() to know when it is done.
 *
 * The pause therefore happens on the worker, not the caller. That is the whole
 * fix: game_pause() is cheap and safe from any task, but the WAIT is what must
 * not happen on the LVGL thread. */
static TaskHandle_t s_task;

typedef enum {
    NET_JOB_NONE = 0,
    NET_JOB_SCAN,
    NET_JOB_CONNECT
} net_job_t;

static volatile net_job_t s_job;
static volatile bool      s_busy;

/* Incremented every time a job finishes. The UI remembers the last value it
 * saw and rebuilds the menu when it changes. A counter rather than a busy-edge
 * because rebuilding the menu calls net_init(), and an edge test would then
 * re-trigger itself forever. */
static volatile uint32_t s_gen;

static void net_task(void *arg)
{
    (void)arg;

    for (;;) {
        /* Wait to be handed a job. */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        net_job_t job = s_job;
        s_job = NET_JOB_NONE;

        if (job == NET_JOB_NONE) {
            s_busy = false;
            continue;
        }

        /* The game is NOT paused.
         *
         * Pausing was the cause of the freeze: game_tick_render() returned
         * early while paused, so if the radio work blocked, the UI stopped
         * rendering permanently. A scan does not need the game stopped -- it
         * needs the radio, and the radio has its own task. */
        ESP_LOGI(TAG, "job %d start", (int)job);
        mem_report_detail("wifi before");

        if (job == NET_JOB_SCAN) {
            net_scan_start_blocking();
        } else if (job == NET_JOB_CONNECT) {
            net_connect_blocking();
        }

        mem_report_detail("wifi after");
        ESP_LOGI(TAG, "job %d done, stack free %u", (int)job,
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));

        s_gen++;
        s_busy = false;
    }
}

static void ensure_task(void)
{
    if (s_task) return;

    BaseType_t ok = xTaskCreate(net_task, "spidarium_net", NET_TASK_STACK,
                                NULL, NET_TASK_PRIO, &s_task);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "net task create failed (stack %d)", NET_TASK_STACK);
        s_task = NULL;
        return;
    }
    ESP_LOGI(TAG, "net task started");
}

/* Queue a job and return immediately. Returns false if one is already running,
 * so a double-tap on "scan" cannot queue two. */
static bool queue_job(net_job_t job)
{
    ensure_task();

    if (!s_task) {
        ESP_LOGE(TAG, "queue_job(%d): no task", (int)job);
        return false;
    }

    if (s_busy) {
        ESP_LOGW(TAG, "queue_job(%d): busy, ignored", (int)job);
        return false;
    }

    ESP_LOGI(TAG, "queue_job(%d)", (int)job);
    s_busy = true;
    s_job  = job;
    xTaskNotifyGive(s_task);
    return true;
}

bool net_busy(void)
{
    return s_busy;
}

/* True while a radio job is running.
 *
 * The tank must not render while this is true. The radio takes internal
 * DMA-capable RAM that the LCD driver needs for its draw buffers, so drawing
 * the tank at the same time is what corrupted the screen. Pocket Tank's rule is
 * the same one: the tank pauses, turns its radio on, does the check, and the
 * radio goes off again.
 *
 * Deliberately keyed on s_busy and NOT on s_started/s_wifi_inited: those stay
 * true for as long as the driver exists, and freezing the tank on them would
 * leave it frozen forever after a connect. */
bool net_radio_active(void)
{
    return s_busy;
}

uint32_t net_generation(void)
{
    return s_gen;
}



/* ------------------------------------------------------------------ */
/* Credentials                                                         */
/* ------------------------------------------------------------------ */
/* Copy `src` into a fixed-size wifi_config field.
 *
 * strncpy() into these fields trips -Werror=stringop-truncation, because the
 * compiler can see that the destination may not be NUL-terminated. The fields
 * are fixed-size arrays that the driver treats as NUL-terminated, so the copy
 * is bounded to one less than the field and the terminator is written
 * explicitly. */
static void copy_field(uint8_t *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n > cap - 1) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void creds_load(void)
{
    s_ssid[0] = '\0';
    s_pass[0] = '\0';

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;

    size_t len = sizeof s_ssid;
    if (nvs_get_str(h, KEY_SSID, s_ssid, &len) != ESP_OK) s_ssid[0] = '\0';

    len = sizeof s_pass;
    if (nvs_get_str(h, KEY_PASS, s_pass, &len) != ESP_OK) s_pass[0] = '\0';

    nvs_close(h);
}

static void creds_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;

    if (s_ssid[0]) {
        nvs_set_str(h, KEY_SSID, s_ssid);
        nvs_set_str(h, KEY_PASS, s_pass);
    } else {
        nvs_erase_key(h, KEY_SSID);
        nvs_erase_key(h, KEY_PASS);
    }
    nvs_commit(h);
    nvs_close(h);
}

/* ------------------------------------------------------------------ */
/* Status text                                                         */
/* ------------------------------------------------------------------ */
static void status_update(void)
{
    switch (s_state) {
    case NET_CONNECTED:
        snprintf(s_status, sizeof s_status, "connected %s", s_ip);
        break;
    case NET_CONNECTING:
        snprintf(s_status, sizeof s_status, "connecting...");
        break;
    case NET_FAILED:
        snprintf(s_status, sizeof s_status, "failed");
        break;
    case NET_OFF:
    default:
        snprintf(s_status, sizeof s_status, "off");
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */
static void on_wifi_event(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    (void)arg;
    (void)base;

    if (id == WIFI_EVENT_STA_START) {
        /* Only associate if there is something to associate with.
         *
         * Scanning starts the radio with no credentials. Connecting then fails
         * immediately, which raises STA_DISCONNECTED and kicks off the retry
         * backoff -- and that churn is what made the first scan fail. */
        if (s_ssid[0]) {
            esp_wifi_connect();
            s_state = NET_CONNECTING;
            status_update();
        }
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        s_have_ip = false;
        s_ip[0] = '\0';
        s_state = NET_FAILED;
        status_update();

        /* Back off, then retry. net_tick() drives the actual retry so this
         * handler never blocks the event task. */
        s_retry_at = xTaskGetTickCount() * portTICK_PERIOD_MS + s_retry_ms;
        s_retry_ms *= 2u;
        if (s_retry_ms > RETRY_MAX_MS) s_retry_ms = RETRY_MAX_MS;
        ESP_LOGW(TAG, "disconnected, retry in %lu ms",
                 (unsigned long)(s_retry_ms / 2u));
    }
}

static void on_ip_event(void *arg, esp_event_base_t base,
                        int32_t id, void *data)
{
    (void)arg;
    (void)base;

    if (id != IP_EVENT_STA_GOT_IP) return;

    ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
    snprintf(s_ip, sizeof s_ip, IPSTR, IP2STR(&ev->ip_info.ip));

    s_have_ip  = true;
    s_state    = NET_CONNECTED;
    s_retry_ms = RETRY_MIN_MS;
    status_update();

    ESP_LOGI(TAG, "connected, ip %s", s_ip);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */
/* Bring the radio up in station mode.
 *
 * Split into two steps because they have different idempotency rules:
 *
 *   radio_up()    -- netif, event loop, esp_wifi_init(). NOT idempotent, so it
 *                    runs exactly once and is guarded by s_wifi_inited.
 *   radio_start() -- esp_wifi_start(). Safe to call again after a stop, and
 *                    guarded by s_started.
 *
 * The previous version had this logic duplicated in net_init_blocking() and
 * ensure_radio(), and the two disagreed: one required credentials, the other
 * did not. Scanning therefore took a different path from connecting, and the
 * first scan failed because the radio was started without the event handlers
 * being in a consistent state. */
static bool radio_up(void)
{
    if (s_wifi_inited) return true;

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "netif init failed: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop failed: %s", esp_err_to_name(err));
        return false;
    }

    /* Create the netif EXACTLY ONCE.
     *
     * esp_netif_create_default_wifi_sta() ASSERTS if a station netif already
     * exists -- it does not return an error. That made a failed esp_wifi_init()
     * fatal on the retry: the netif had been created, the init had not, so
     * s_wifi_inited stayed false and the next attempt created the netif a
     * second time and aborted the device.
     *
     * Guarded separately so a partial failure can be retried safely. */
    if (!s_netif_created) {
        if (!esp_netif_create_default_wifi_sta()) {
            ESP_LOGE(TAG, "netif create failed");
            return false;
        }
        s_netif_created = true;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi init failed: %s", esp_err_to_name(err));

        /* A failed init leaves the driver half-constructed. Deinit it so the
         * next attempt starts clean -- otherwise the retry hits a duplicate
         * netif key and asserts, which is a reboot rather than a failed scan. */
        esp_wifi_deinit();
        return false;
    }

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        on_wifi_event, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        on_ip_event, NULL, NULL);

    s_wifi_inited = true;
    return true;
}

static bool radio_start(void)
{
    if (s_started) return true;
    if (!radio_up()) return false;

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_mode failed: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_start failed: %s (largest internal %u)",
                 esp_err_to_name(err),
                 (unsigned)heap_caps_get_largest_free_block(
                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        return false;
    }

    s_started = true;
    return true;
}

/* Apply the stored credentials to the radio. Runs on the net task only. */
static void net_connect_blocking(void)
{
    if (!s_ssid[0]) {
        /* Clearing credentials stops the radio rather than leaving it hunting
         * for a network that will never be found. */
        if (s_started) esp_wifi_stop();
        s_started = false;
        s_state   = NET_OFF;
        s_ip[0]   = '\0';
        status_update();
        return;
    }

    if (!radio_start()) {
        s_state = NET_FAILED;
        status_update();
        return;
    }

    wifi_config_t wc;
    memset(&wc, 0, sizeof wc);
    copy_field(wc.sta.ssid, sizeof wc.sta.ssid, s_ssid);
    copy_field(wc.sta.password, sizeof wc.sta.password, s_pass);
    wc.sta.threshold.authmode = WIFI_AUTH_OPEN;

    esp_wifi_set_config(WIFI_IF_STA, &wc);
    esp_wifi_disconnect();
    esp_wifi_connect();

    s_state = NET_CONNECTING;
    status_update();
}

void net_set_credentials(const char *ssid, const char *pass)
{
    if (!ssid) ssid = "";
    if (!pass) pass = "";

    strncpy(s_ssid, ssid, sizeof s_ssid - 1);
    s_ssid[sizeof s_ssid - 1] = '\0';
    strncpy(s_pass, pass, sizeof s_pass - 1);
    s_pass[sizeof s_pass - 1] = '\0';

    creds_save();

    /* Queue the radio work. This function is called from the LVGL thread, so it
     * must not touch esp_wifi_* directly -- doing so was the same class of bug
     * as calling lv_timer_pause() from the worker. */
    queue_job(NET_JOB_CONNECT);
}

bool net_has_credentials(void)
{
    return s_ssid[0] != '\0';
}

const char *net_ssid(void)
{
    return s_ssid;
}

net_state_t net_state(void)
{
    return s_state;
}

const char *net_status_text(void)
{
    return s_status;
}

const char *net_ip(void)
{
    return s_ip;
}

void net_tick(uint32_t now_ms)
{
    if (!s_started || s_state == NET_CONNECTED) return;
    if (!s_ssid[0]) return;

    if (s_state == NET_FAILED && (int32_t)(now_ms - s_retry_at) >= 0) {
        ESP_LOGI(TAG, "retrying");
        s_state = NET_CONNECTING;
        status_update();
        esp_wifi_connect();
    }
}

/* ------------------------------------------------------------------ */
/* Scanning                                                            */
/* ------------------------------------------------------------------ */
/* Results are copied out of the driver immediately, because the driver's own
 * list is freed by the next scan or by esp_wifi_clear_ap_list(). Holding a
 * pointer into it across a menu rebuild would be a use-after-free. */
#define SCAN_MAX 12

static wifi_ap_record_t s_scan[SCAN_MAX];
static int              s_scan_n;

/* Why the last scan failed, or empty on success. Shown in the menu, because a
 * scan that silently returns nothing is indistinguishable from a scan that
 * found nothing -- and that ambiguity is what made this take several rounds. */
static char             s_scan_err[40];

/* The radio must be started before it can scan. Scanning is allowed to bring
 * the stack up with no credentials -- it simply will not associate, because
 * on_wifi_event() only connects when an SSID is set. */
static bool net_scan_start_blocking(void)
{
    s_scan_n = 0;
    s_scan_err[0] = '\0';

    if (!radio_start()) {
        snprintf(s_scan_err, sizeof s_scan_err, "radio unavailable");
        ESP_LOGW(TAG, "scan: radio not available");
        return false;
    }

    wifi_scan_config_t sc;
    memset(&sc, 0, sizeof sc);
    sc.show_hidden = false;

    /* BLOCKING scan.
     *
     * The asynchronous form returns immediately and the results have to be
     * polled, which meant the menu was rebuilt before the scan had finished and
     * the driver's AP list was read while it was still being written. Blocking
     * here is safe because this runs on the net task, not the LVGL task. */
    esp_err_t err = esp_wifi_scan_start(&sc, true);
    if (err != ESP_OK) {
        snprintf(s_scan_err, sizeof s_scan_err, "%s", esp_err_to_name(err));
        ESP_LOGW(TAG, "scan failed: %s", esp_err_to_name(err));
        goto release;
    }

    uint16_t n = 0;
    err = esp_wifi_scan_get_ap_num(&n);
    if (err != ESP_OK) {
        snprintf(s_scan_err, sizeof s_scan_err, "%s", esp_err_to_name(err));
        ESP_LOGW(TAG, "scan count failed: %s", esp_err_to_name(err));
        goto release;
    }

    if (n > SCAN_MAX) n = SCAN_MAX;
    if (n > 0) {
        esp_wifi_scan_get_ap_records(&n, s_scan);
    }
    s_scan_n = (int)n;
    ESP_LOGI(TAG, "scan found %d networks", s_scan_n);

release:
    /* FULL TEARDOWN, ON EVERY PATH. This is the whole point.
     *
     * The results are already copied into s_scan, so nothing from the driver is
     * needed any more. esp_wifi_stop() alone is NOT enough: it stops the radio
     * but leaves the driver's RX/TX pools allocated, so the internal
     * DMA-capable RAM stays gone for the rest of the session. That is what
     * produced the "screen rendering goes berserk" report -- the LCD could no
     * longer allocate its draw buffers.
     *
     * esp_wifi_deinit() releases those pools. Together with the stop it returns
     * every byte the radio took, so the display is back to its full budget the
     * moment the scan ends.
     *
     * This runs on the FAILURE paths too, and that matters: the early returns
     * used to skip it, so every failed scan left the radio up and the next
     * attempt stacked another driver on top. A few taps was enough to starve
     * the display completely. */
    esp_wifi_stop();
    esp_wifi_deinit();

    s_started     = false;
    s_wifi_inited = false;

    mem_report_detail("wifi released");

    return s_scan_err[0] == '\0';
}

int net_scan_count(void)
{
    /* The scan is blocking, so by the time this is called the results are
     * already in s_scan. No polling needed. */
    return s_scan_n;
}

const char *net_scan_error(void)
{
    return s_scan_err;
}

const char *net_scan_ssid(int i)
{
    if (i < 0 || i >= s_scan_n) return "";
    return (const char *)s_scan[i].ssid;
}

int net_scan_rssi(int i)
{
    if (i < 0 || i >= s_scan_n) return 0;
    return s_scan[i].rssi;
}

bool net_scan_secure(int i)
{
    if (i < 0 || i >= s_scan_n) return false;
    return s_scan[i].authmode != WIFI_AUTH_OPEN;
}

void net_connect_to(const char *ssid, const char *pass)
{
    net_set_credentials(ssid, pass);
}

/* ------------------------------------------------------------------ */
/* Public entry points                                                 */
/* ------------------------------------------------------------------ */
/* These are what the UI calls. Each one QUEUES the job and returns at once --
 * they never block. The worker pauses the game, does the radio work, and
 * resumes. Poll net_busy() to know when it has finished. */

/* Load the saved credentials and refresh the status text.
 *
 * This deliberately does NOT touch the radio and does NOT queue a job. It used
 * to queue a NET_JOB_INIT, which set s_busy -- and because the WiFi menu calls
 * net_init() when it opens, tapping "scan" before that job finished hit the
 * "already busy" guard in queue_job() and the scan was silently dropped. That
 * was the "first press does nothing" bug.
 *
 * The radio is now brought up lazily by radio_start(), inside whichever job
 * actually needs it. */
void net_init(void)
{
    creds_load();
    status_update();
}

bool net_scan_start(void)
{
    ESP_LOGI(TAG, "net_scan_start");
    return queue_job(NET_JOB_SCAN);
}
