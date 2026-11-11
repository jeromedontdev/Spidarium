/*
 * On-demand network worker. A job owns the radio and pauses the display adapter
 * with acknowledgement; it never calls lv_* or keeps WiFi running afterwards.
 */
#include "net.h"
#include "rtc.h"
#include "meminfo.h"
#include "save.h"
#include "power.h"
#include "audio.h"

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define SCAN_MAX 12
#define NET_STACK 12288
#define NVS_NS "spidarium_net"

static const char *TAG = "spidarium.net";
typedef struct { char ssid[33]; char pass[65]; } credential_t;
typedef struct {
    uint32_t version;
    uint32_t count;
    credential_t networks[NET_SAVED_MAX];
} credentials_t;
typedef enum { JOB_SCAN, JOB_JOIN, JOB_SYNC, JOB_FORGET, JOB_OTA, JOB_PRIME } job_kind_t;
typedef struct {
    job_kind_t kind;
    int index;
    credential_t credential;
    char url[256];
} job_t;

static TaskHandle_t s_task;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static atomic_bool s_busy;
static atomic_bool s_display_ready;
static atomic_uint s_generation;
static atomic_bool s_have_ip;
static atomic_bool s_disconnected;
static job_t s_job;
static credentials_t s_credentials;
static wifi_ap_record_t s_scan[SCAN_MAX];
static int s_scan_count;
static char s_error[80];
static char s_status[96] = "radio off";
static char s_ip[16];
static net_state_t s_state;
static bool s_inited;
static bool s_started;
static esp_netif_t *s_netif;
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;

static bool fail(const char *operation, esp_err_t error)
{
    snprintf(s_error, sizeof s_error, "%s: %s", operation, esp_err_to_name(error));
    snprintf(s_status, sizeof s_status, "%s", s_error);
    s_state = NET_FAILED;
    ESP_LOGE(TAG, "%s", s_error);
    return false;
}

static void load_credentials(void)
{
    s_credentials = (credentials_t){ .version = 1 };
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return;
    if (err != ESP_OK) { fail("open credentials", err); return; }
    size_t size = sizeof s_credentials;
    err = nvs_get_blob(h, "networks", &s_credentials, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* Migrate the original single-network format without erasing it. */
        size_t len = sizeof s_credentials.networks[0].ssid;
        err = nvs_get_str(h, "ssid", s_credentials.networks[0].ssid, &len);
        if (err == ESP_OK && s_credentials.networks[0].ssid[0]) {
            len = sizeof s_credentials.networks[0].pass;
            err = nvs_get_str(h, "pass", s_credentials.networks[0].pass, &len);
            if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
            if (err == ESP_OK) s_credentials.count = 1;
        } else if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    } else if (err == ESP_OK &&
               (size != sizeof s_credentials || s_credentials.version != 1 ||
                s_credentials.count > NET_SAVED_MAX)) {
        err = ESP_ERR_INVALID_SIZE;
    }
    nvs_close(h);
    for (unsigned i = 0; err == ESP_OK && i < s_credentials.count; i++) {
        if (!memchr(s_credentials.networks[i].ssid, 0, 33) ||
            !memchr(s_credentials.networks[i].pass, 0, 65) ||
            !s_credentials.networks[i].ssid[0]) err = ESP_ERR_INVALID_ARG;
    }
    if (err != ESP_OK) {
        s_credentials = (credentials_t){ .version = 1 };
        fail("read credentials", err);
    }
}

static bool commit_credentials(const credentials_t *credentials)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return fail("open credentials", err);
    err = nvs_set_blob(h, "networks", credentials, sizeof *credentials);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) return fail("save credentials", err);
    s_credentials = *credentials;
    return true;
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    if (id == WIFI_EVENT_STA_DISCONNECTED) {
        atomic_store(&s_have_ip, false);
        atomic_store(&s_disconnected, true);
    }
}

static void on_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base;
    if (id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        snprintf(s_ip, sizeof s_ip, IPSTR, IP2STR(&event->ip_info.ip));
        atomic_store(&s_have_ip, true);
    }
}

static bool radio_up(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return fail("netif", err);
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return fail("event loop", err);
    if (!s_netif) {
        s_netif = esp_netif_create_default_wifi_sta();
        if (!s_netif) return fail("station netif", ESP_ERR_NO_MEM);
    }
    if (!s_wifi_handler) {
        err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                 on_wifi, NULL, &s_wifi_handler);
        if (err != ESP_OK) return fail("WiFi event handler", err);
    }
    if (!s_ip_handler) {
        err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                 on_ip, NULL, &s_ip_handler);
        if (err != ESP_OK) return fail("IP event handler", err);
    }
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&config);
    if (err != ESP_OK) return fail("WiFi init", err);
    s_inited = true;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err != ESP_OK) return fail("WiFi start", err);
    s_started = true;
    return true;
}

static bool radio_down(void)
{
    bool ok = true;
    if (s_started) {
        esp_err_t err = esp_wifi_stop();
        if (err != ESP_OK) ok = fail("WiFi stop", err);
        else s_started = false;
    }
    if (s_inited && !s_started) {
        esp_err_t err = esp_wifi_deinit();
        if (err != ESP_OK) ok = fail("WiFi deinit", err);
        else s_inited = false;
    }
    atomic_store(&s_have_ip, false);
    return ok;
}

static bool scan(void)
{
    s_scan_count = 0;
    if (!radio_up()) return false;
    wifi_scan_config_t config = { .show_hidden = false };
    esp_err_t err = esp_wifi_scan_start(&config, true);
    if (err != ESP_OK) return fail("scan", err);
    uint16_t count = SCAN_MAX;
    err = esp_wifi_scan_get_ap_records(&count, s_scan);
    if (err != ESP_OK) return fail("scan results", err);
    /* Discard any unreturned driver records before teardown. */
    err = esp_wifi_clear_ap_list();
    if (err != ESP_OK) return fail("clear scan results", err);
    s_scan_count = count;
    snprintf(s_status, sizeof s_status, "%u networks; radio off", count);
    ESP_LOGI(TAG, "scan found %u networks", count);
    return true;
}

static bool join(const credential_t *credential)
{
    if (!radio_up()) return false;
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, credential->ssid, strlen(credential->ssid));
    memcpy(config.sta.password, credential->pass, strlen(credential->pass));
    config.sta.pmf_cfg.capable = true;
    s_state = NET_CONNECTING;
    atomic_store(&s_disconnected, false);
    atomic_store(&s_have_ip, false);
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err == ESP_OK) err = esp_wifi_connect();
    if (err != ESP_OK) return fail("connect", err);
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(15000);
    while (!atomic_load(&s_have_ip) && !atomic_load(&s_disconnected) &&
           (int32_t)(deadline - xTaskGetTickCount()) > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!atomic_load(&s_have_ip))
        return fail("connect", atomic_load(&s_disconnected) ? ESP_FAIL : ESP_ERR_TIMEOUT);
    s_state = NET_CONNECTED;
    return true;
}

static atomic_bool s_time_received;
static void on_time(struct timeval *value)
{
    (void)value;
    atomic_store(&s_time_received, true);
}

static bool sync_time(void)
{
    atomic_store(&s_time_received, false);
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(on_time);
    esp_sntp_init();
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(10000);
    while (!atomic_load(&s_time_received) &&
           (int32_t)(deadline - xTaskGetTickCount()) > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    esp_sntp_stop();
    if (!atomic_load(&s_time_received)) return fail("NTP sync", ESP_ERR_TIMEOUT);
    esp_err_t err = pet_rtc_write(time(NULL));
    if (err != ESP_OK) return fail("write RTC", err);
    snprintf(s_status, sizeof s_status, "RTC synced; radio off");
    return true;
}

static bool ota(const char *url)
{
    if (!esp_ota_get_next_update_partition(NULL))
        return fail("OTA partition missing", ESP_ERR_NOT_FOUND);
    esp_http_client_config_t http = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
        .disable_auto_redirect = true,
    };
    esp_https_ota_config_t config = { .http_config = &http };
    esp_https_ota_handle_t handle = NULL;
    esp_err_t err = esp_https_ota_begin(&config, &handle);
    if (err != ESP_OK) return fail("OTA begin", err);
    esp_app_desc_t candidate;
    err = esp_https_ota_get_img_desc(handle, &candidate);
    const esp_app_desc_t *current = esp_app_get_description();
    if (err == ESP_OK && strncmp(candidate.project_name, current->project_name,
                                 sizeof candidate.project_name) != 0)
        err = ESP_ERR_INVALID_ARG;
    if (err == ESP_OK && strncmp(candidate.version, current->version,
                                 sizeof candidate.version) == 0) {
        esp_https_ota_abort(handle);
        snprintf(s_status, sizeof s_status, "already up to date; radio off");
        return false;
    }
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(120000);
    if (err == ESP_OK) {
        do {
            err = esp_https_ota_perform(handle);
            if ((int32_t)(deadline - xTaskGetTickCount()) <= 0) {
                err = ESP_ERR_TIMEOUT;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        } while (err == ESP_ERR_HTTPS_OTA_IN_PROGRESS);
    }
    if (err != ESP_OK || !esp_https_ota_is_complete_data_received(handle)) {
        esp_https_ota_abort(handle);
        return fail("OTA download", err == ESP_OK ? ESP_ERR_INVALID_SIZE : err);
    }
    err = esp_https_ota_finish(handle);
    if (err != ESP_OK) return fail("OTA verify", err);
    snprintf(s_status, sizeof s_status, "update installed; reboot to apply");
    return true;
}

static void worker(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        job_t job = s_job;
        s_error[0] = 0;
        s_state = NET_OFF;
        bool paused = false;
        bool proceed = true;
        TickType_t power_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(3000);
        while (power_writing()) {
            if ((int32_t)(power_deadline - xTaskGetTickCount()) <= 0) {
                fail("display power drain", ESP_ERR_TIMEOUT);
                proceed = false;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (proceed && atomic_load(&s_display_ready)) {
            /* Adapter API owns the handshake and drains its current handler.
             * The worker waits; LVGL never waits for the worker. */
            esp_err_t err = esp_lv_adapter_pause(3000);
            paused = err == ESP_OK;
            if (!paused) {
                fail("display pause", err);
                /* Clear a timed-out pause request too. */
                err = esp_lv_adapter_resume();
                if (err != ESP_OK) {
                    fail("cancel display pause", err);
                    esp_restart();
                }
                proceed = false;
            }
        }
        mem_report_detail("network before");
        TickType_t drain_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(3000);
        while (save_writing() || audio_playing()) {
            if ((int32_t)(drain_deadline - xTaskGetTickCount()) <= 0) {
                fail("background drain", ESP_ERR_TIMEOUT);
                proceed = false;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (proceed) {
            if (job.kind == JOB_PRIME) {
                if (radio_up()) snprintf(s_status, sizeof s_status, "ready; radio off");
            } else if (job.kind == JOB_SCAN) scan();
            else if (job.kind == JOB_FORGET) {
                credentials_t updated = s_credentials;
                for (unsigned i = job.index; i + 1 < updated.count; i++)
                    updated.networks[i] = updated.networks[i + 1];
                updated.networks[--updated.count] = (credential_t){0};
                if (commit_credentials(&updated))
                    snprintf(s_status, sizeof s_status, "network forgotten; radio off");
            } else {
                credential_t credential = job.kind == JOB_JOIN ?
                    job.credential : s_credentials.networks[job.index];
                if (join(&credential)) {
                    bool saved = true;
                    if (job.kind == JOB_JOIN) {
                        credentials_t updated = s_credentials;
                        unsigned index = updated.count;
                        for (unsigned i = 0; i < updated.count; i++)
                            if (!strcmp(updated.networks[i].ssid, credential.ssid)) index = i;
                        if (index == updated.count) updated.count++;
                        updated.networks[index] = credential;
                        saved = commit_credentials(&updated);
                    }
                    if (saved) {
                        if (job.kind == JOB_OTA) {
                            if (time(NULL) >= 1577836800 || sync_time()) ota(job.url);
                        }
                        else sync_time();
                    }
                }
            }
        }
        bool released = radio_down();
        if (!released) {
            /* Do not restart DMA drawing with a driver that still owns RAM. */
            ESP_LOGE(TAG, "radio teardown failed; rebooting to recover DMA ownership");
            esp_restart();
        }
        if (released && s_state != NET_FAILED) s_state = NET_OFF;
        mem_report_detail("network released");
        if (paused) {
            esp_err_t err = esp_lv_adapter_resume();
            if (err != ESP_OK) {
                fail("display resume", err);
                esp_restart();
            }
        }
        memset(&job, 0, sizeof job);
        memset(&s_job, 0, sizeof s_job);
        atomic_store(&s_busy, false);
        atomic_fetch_add(&s_generation, 1);
        ESP_LOGI(TAG, "job complete; stack free %u",
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
}

static bool queue(const job_t *job)
{
    if (power_sleep_pending()) {
        ESP_LOGW(TAG, "network job rejected: deep sleep pending");
        return false;
    }
    if (!s_task) {
        ESP_LOGE(TAG, "network worker unavailable");
        return false;
    }
    portENTER_CRITICAL(&s_mux);
    bool accepted = !atomic_load(&s_busy);
    if (accepted) {
        s_job = *job;
        atomic_store(&s_busy, true);
    }
    portEXIT_CRITICAL(&s_mux);
    if (!accepted) { ESP_LOGW(TAG, "network job rejected: busy"); return false; }
    xTaskNotifyGive(s_task);
    return true;
}

void net_boot_init(void)
{
    pet_rtc_init();
    load_credentials();
    if (xTaskCreateWithCaps(worker, "spidarium_net", NET_STACK, NULL, 4,
                            &s_task, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) != pdPASS) {
        s_task = NULL;
        fail("network worker", ESP_ERR_NO_MEM);
        return;
    }
    ESP_LOGI(TAG, "network worker stack internal for NVS and OTA flash writes");
    /* Establish persistent netif/event allocations before LCD DMA allocations,
     * but never join a saved network without a player request. */
    job_t prime = {.kind = JOB_PRIME};
    if (queue(&prime)) {
        while (net_busy()) vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void net_display_ready(void) { atomic_store(&s_display_ready, true); }
void net_init(void) { /* Initialized once before display startup. */ }
void net_tick(uint32_t now_ms) { (void)now_ms; /* No background reconnects. */ }
bool net_busy(void) { return atomic_load(&s_busy); }
bool net_radio_active(void) { return net_busy(); }
uint32_t net_generation(void) { return atomic_load(&s_generation); }
net_state_t net_state(void) { return net_busy() ? NET_CONNECTING : s_state; }
const char *net_status_text(void) { return net_busy() ? "working; please wait" : s_status; }
const char *net_ip(void) { return net_busy() ? "" : s_ip; }
int net_saved_count(void) { return net_busy() ? 0 : s_credentials.count; }
const char *net_saved_ssid(int i)
{
    return i >= 0 && i < net_saved_count() ? s_credentials.networks[i].ssid : "";
}
bool net_has_credentials(void) { return net_saved_count() != 0; }
const char *net_ssid(void) { return net_saved_ssid(0); }
int net_scan_count(void) { return net_busy() ? 0 : s_scan_count; }
const char *net_scan_error(void) { return net_busy() ? "" : s_error; }
const char *net_scan_ssid(int i)
{
    return i >= 0 && i < net_scan_count() ? (const char *)s_scan[i].ssid : "";
}
int net_scan_rssi(int i) { return i >= 0 && i < net_scan_count() ? s_scan[i].rssi : 0; }
bool net_scan_secure(int i)
{
    return i >= 0 && i < net_scan_count() && s_scan[i].authmode != WIFI_AUTH_OPEN;
}
bool net_scan_start(void) { return queue(&(job_t){ .kind = JOB_SCAN }); }
bool net_sync_saved(int i)
{
    if (i < 0 || i >= net_saved_count()) {
        ESP_LOGW(TAG, "sync rejected: no saved network at index %d", i);
        return false;
    }
    return queue(&(job_t){ .kind = JOB_SYNC, .index = i });
}
bool net_forget_saved(int i)
{
    if (i < 0 || i >= net_saved_count()) {
        ESP_LOGW(TAG, "forget rejected: invalid index %d", i);
        return false;
    }
    return queue(&(job_t){ .kind = JOB_FORGET, .index = i });
}
bool net_connect_to(const char *ssid, const char *pass)
{
    if (!ssid || !pass || !ssid[0] || strlen(ssid) > 32 || strlen(pass) > 63 ||
        (pass[0] && strlen(pass) < 8)) {
        ESP_LOGW(TAG, "join rejected: SSID 1..32 bytes; password empty or 8..63 bytes");
        return false;
    }
    if (net_busy()) { ESP_LOGW(TAG, "join rejected: busy"); return false; }
    bool exists = false;
    for (int i = 0; i < net_saved_count(); i++)
        if (!strcmp(ssid, net_saved_ssid(i))) exists = true;
    if (!exists && net_saved_count() == NET_SAVED_MAX) {
        ESP_LOGW(TAG, "join rejected: forget a saved network first");
        return false;
    }
    job_t job = { .kind = JOB_JOIN };
    snprintf(job.credential.ssid, sizeof job.credential.ssid, "%s", ssid);
    snprintf(job.credential.pass, sizeof job.credential.pass, "%s", pass);
    return queue(&job);
}
bool net_set_credentials(const char *ssid, const char *pass)
{
    return ssid && !ssid[0] ? net_forget_saved(0) : net_connect_to(ssid, pass);
}
bool net_ota_start(const char *url)
{
    if (!url || strncmp(url, "https://", 8) || strlen(url) < 9 || strlen(url) > 255 ||
        !net_has_credentials()) {
        ESP_LOGW(TAG, "OTA rejected: requires HTTPS URL and a saved network");
        return false;
    }
    job_t job = { .kind = JOB_OTA, .index = 0 };
    snprintf(job.url, sizeof job.url, "%s", url);
    return queue(&job);
}
