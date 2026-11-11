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
#include "esp_http_client.h"
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
#include "cJSON.h"
#include "mbedtls/sha256.h"

#include <ctype.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define SCAN_MAX 12
#define NET_STACK 12288
#define NVS_NS "spidarium_net"
#define RELEASE_BASE "https://github.com/jeromedontdev/Spidarium/releases/"
#define LATEST_OTA RELEASE_BASE "latest/download/spidarium-ota.json"

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
        .max_redirection_count = 5,
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

typedef struct {
    char version[32];
    char url[256];
    size_t size, storage_size;
    uint8_t hash[32], storage_hash[32];
} ota_release_t;

static bool read_hash(const cJSON *item, uint8_t hash[32])
{
    if (!cJSON_IsString(item) || strlen(item->valuestring) != 64) return false;
    for (int i = 0; i < 32; i++) {
        unsigned value = 0;
        for (int j = 0; j < 2; j++) {
            unsigned char c = (unsigned char)item->valuestring[i * 2 + j];
            if (!isxdigit(c)) return false;
            value = value * 16 + (isdigit(c) ? c - '0' : tolower(c) - 'a' + 10);
        }
        hash[i] = (uint8_t)value;
    }
    return true;
}

static bool parse_release(const char *text, ota_release_t *release)
{
    cJSON *json = cJSON_Parse(text);
    if (!json) return false;
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(json, "version");
    const cJSON *url = cJSON_GetObjectItemCaseSensitive(json, "url");
    const cJSON *size = cJSON_GetObjectItemCaseSensitive(json, "size");
    const cJSON *storage = cJSON_GetObjectItemCaseSensitive(json, "storage");
    const cJSON *storage_size = cJSON_GetObjectItemCaseSensitive(storage, "size");
    unsigned major, minor, patch;
    char extra;
    bool ok = cJSON_IsString(version) && strlen(version->valuestring) < sizeof release->version &&
        sscanf(version->valuestring, "%u.%u.%u%c", &major, &minor, &patch, &extra) == 3 &&
        cJSON_IsString(url) && cJSON_IsNumber(size) && size->valuedouble >= 112 &&
        size->valuedouble <= 0x300000 && size->valuedouble == size->valueint &&
        cJSON_IsNumber(storage_size) && storage_size->valuedouble > 0 &&
        storage_size->valuedouble <= 0x600000 && storage_size->valuedouble == storage_size->valueint &&
        read_hash(cJSON_GetObjectItemCaseSensitive(json, "sha256"), release->hash) &&
        read_hash(cJSON_GetObjectItemCaseSensitive(storage, "sha256"), release->storage_hash);
    if (ok) {
        char expected[256];
        snprintf(expected, sizeof expected, RELEASE_BASE "download/v%s/spidarium.bin", version->valuestring);
        ok = !strcmp(url->valuestring, expected);
        if (ok) {
            snprintf(release->version, sizeof release->version, "%s", version->valuestring);
            snprintf(release->url, sizeof release->url, "%s", expected);
            release->size = (size_t)size->valueint;
            release->storage_size = (size_t)storage_size->valueint;
        }
    }
    cJSON_Delete(json);
    return ok;
}

typedef struct {
    size_t offset, size;
    bool valid;
} release_range_t;

static esp_err_t release_headers(esp_http_client_event_t *event)
{
    release_range_t *range = event->user_data;
    if (range && event->event_id == HTTP_EVENT_ON_HEADER &&
        !strcasecmp(event->header_key, "Content-Range")) {
        unsigned start, end, size;
        char extra;
        range->valid = sscanf(event->header_value, "bytes %u-%u/%u%c",
            &start, &end, &size, &extra) == 3 && start == range->offset &&
            size == range->size && end == size - 1;
    }
    return ESP_OK;
}

static esp_http_client_handle_t open_release_url(const char *url, release_range_t *range)
{
    esp_http_client_config_t config = {
        .url = url, .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000, .buffer_size = 2048, .buffer_size_tx = 1024,
        .user_agent = "Spidarium-updater", .disable_auto_redirect = true,
        .event_handler = release_headers, .user_data = range,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { fail("update HTTP allocation", ESP_ERR_NO_MEM); return NULL; }
    if (range && range->offset) {
        char value[40];
        snprintf(value, sizeof value, "bytes=%u-", (unsigned)range->offset);
        esp_err_t err = esp_http_client_set_header(client, "Range", value);
        if (err != ESP_OK) {
            fail("update range request", err); esp_http_client_cleanup(client); return NULL;
        }
    }
    for (int redirects = 0; redirects <= 5; redirects++) {
        if (range) range->valid = false;
        esp_err_t err = esp_http_client_open(client, 0);
        if (err != ESP_OK) { fail("update HTTPS connect", err); break; }
        int64_t length = esp_http_client_fetch_headers(client);
        if (length < 0 && !esp_http_client_is_chunked_response(client)) {
            fail("update HTTPS headers", ESP_FAIL); break;
        }
        int status = esp_http_client_get_status_code(client);
        if (range && range->offset && status == 206 && range->valid) return client;
        if ((!range || !range->offset) && status == 200) return client;
        if (redirects == 5 || (status != 301 && status != 302 && status != 303 &&
                              status != 307 && status != 308)) {
            ESP_LOGE(TAG, "update HTTP status %d", status);
            fail("update HTTP response", ESP_FAIL); break;
        }
        err = esp_http_client_set_redirection(client);
        if (err != ESP_OK || esp_http_client_get_transport_type(client) != HTTP_TRANSPORT_OVER_SSL) {
            fail("update HTTPS redirect", err == ESP_OK ? ESP_ERR_INVALID_ARG : err); break;
        }
        esp_http_client_close(client);
    }
    esp_http_client_cleanup(client);
    return NULL;
}

static int release_read(esp_http_client_handle_t client, char *buffer, int size,
                        TickType_t deadline)
{
    for (;;) {
        if ((int32_t)(deadline - xTaskGetTickCount()) <= 0) return -ESP_ERR_TIMEOUT;
        int got = esp_http_client_read(client, buffer, size);
        if (got != -ESP_ERR_HTTP_EAGAIN) return got;
        ESP_LOGW(TAG, "update receive timeout; retrying within deadline");
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static bool ota_latest(void)
{
    char *text = heap_caps_calloc(1, 4097, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!text) return fail("update manifest allocation", ESP_ERR_NO_MEM);
    esp_http_client_handle_t client = open_release_url(LATEST_OTA, NULL);
    if (!client) { free(text); return false; }
    int used = 0, got = 0;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(30000);
    while (used < 4096 && (got = release_read(client, text + used, 4096 - used, deadline)) > 0)
        used += got;
    bool complete = got >= 0 && used < 4096 && esp_http_client_is_complete_data_received(client);
    esp_http_client_cleanup(client);
    ota_release_t release = {0};
    bool valid = complete && parse_release(text, &release);
    free(text);
    if (!valid) return fail("invalid latest release manifest", ESP_ERR_INVALID_ARG);
    const char *current = esp_app_get_description()->version;
    if (!strcmp(release.version, current)) {
        snprintf(s_status, sizeof s_status, "GitHub latest %s; already up to date", release.version);
        ESP_LOGI(TAG, "%s", s_status);
        return true;
    }
    unsigned a = 0, b = 0, c = 0, x = 0, y = 0, z = 0;
    if (sscanf(current, "%u.%u.%u", &a, &b, &c) != 3 ||
        sscanf(release.version, "%u.%u.%u", &x, &y, &z) != 3 ||
        x < a || (x == a && y < b) || (x == a && y == b && z < c))
        return fail("latest release is older; update refused", ESP_ERR_INVALID_VERSION);
    const esp_partition_t *storage = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_ANY, "storage");
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!storage || !target || release.storage_size > storage->size || release.size > target->size)
        return fail("update partition missing or too small", ESP_ERR_INVALID_SIZE);
    uint8_t *buffer = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) return fail("update buffer", ESP_ERR_NO_MEM);
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    uint8_t digest[32];
    esp_err_t err = mbedtls_sha256_starts(&sha, 0) == 0 ? ESP_OK : ESP_FAIL;
    for (size_t offset = 0; err == ESP_OK && offset < release.storage_size;) {
        size_t count = release.storage_size - offset;
        if (count > 4096) count = 4096;
        err = esp_partition_read(storage, offset, buffer, count);
        if (err == ESP_OK && mbedtls_sha256_update(&sha, buffer, count) != 0) err = ESP_FAIL;
        offset += count;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (err == ESP_OK && mbedtls_sha256_finish(&sha, digest) != 0) err = ESP_FAIL;
    if (err == ESP_OK && memcmp(digest, release.storage_hash, sizeof digest))
        err = ESP_ERR_INVALID_CRC;
    if (err != ESP_OK) {
        mbedtls_sha256_free(&sha); free(buffer);
        return fail("art differs; use USB updater", err);
    }
    release_range_t range = { .size = release.size };
    client = open_release_url(release.url, &range);
    if (!client) { mbedtls_sha256_free(&sha); free(buffer); return false; }
    esp_ota_handle_t handle = 0;
    bool started = false;
    size_t received = 0;
    err = mbedtls_sha256_starts(&sha, 0) == 0 ? ESP_OK : ESP_FAIL;
    deadline = xTaskGetTickCount() + pdMS_TO_TICKS(300000);
    if (err == ESP_OK) {
        err = esp_ota_begin(target, release.size, &handle);
        started = err == ESP_OK;
    }
    size_t reported = 0;
    unsigned reconnects = 0;
    while (err == ESP_OK && received < release.size) {
        if ((int32_t)(deadline - xTaskGetTickCount()) <= 0) { err = ESP_ERR_TIMEOUT; break; }
        got = esp_http_client_read(client, (char *)buffer, 4096);
        if (got == -ESP_ERR_HTTP_EAGAIN && reconnects++ < 16) {
            ESP_LOGW(TAG, "update stalled at %u; reopening verified range", (unsigned)received);
            esp_http_client_cleanup(client);
            range.offset = received;
            client = open_release_url(release.url, &range);
            if (!client) { err = ESP_FAIL; break; }
            continue;
        }
        if (got <= 0) { err = got < 0 ? (esp_err_t)-got : ESP_ERR_INVALID_SIZE; break; }
        if (received + got > release.size) {
            err = ESP_ERR_INVALID_SIZE; break;
        }
        if (mbedtls_sha256_update(&sha, buffer, got) != 0) { err = ESP_FAIL; break; }
        err = esp_ota_write(handle, buffer, got);
        received += got;
        if (received - reported >= 131072 || received == release.size) {
            ESP_LOGI(TAG, "update received %u/%u bytes", (unsigned)received, (unsigned)release.size);
            reported = received;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (err == ESP_OK && (received != release.size ||
                          !esp_http_client_is_complete_data_received(client)))
        err = ESP_ERR_INVALID_SIZE;
    if (err != ESP_OK)
        ESP_LOGE(TAG, "update transfer stopped at %u/%u bytes; read=%d complete=%d",
                 (unsigned)received, (unsigned)release.size, got,
                 client && esp_http_client_is_complete_data_received(client));
    if (client) esp_http_client_cleanup(client);
    if (err == ESP_OK && mbedtls_sha256_finish(&sha, digest) != 0) err = ESP_FAIL;
    if (err == ESP_OK && memcmp(digest, release.hash, sizeof digest)) err = ESP_ERR_INVALID_CRC;
    mbedtls_sha256_free(&sha);
    free(buffer);
    if (err != ESP_OK) {
        if (started) esp_ota_abort(handle);
        return fail("latest OTA download/hash", err);
    }
    err = esp_ota_end(handle);
    esp_app_desc_t candidate;
    if (err == ESP_OK) err = esp_ota_get_partition_description(target, &candidate);
    if (err == ESP_OK && (strcmp(candidate.version, release.version) ||
        strcmp(candidate.project_name, esp_app_get_description()->project_name)))
        err = ESP_ERR_INVALID_VERSION;
    if (err == ESP_OK) err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK) return fail("latest OTA verify/select", err);
    snprintf(s_status, sizeof s_status, "GitHub %s installed; RESET to apply", release.version);
    ESP_LOGI(TAG, "%s; NVS and artwork unchanged", s_status);
    return true;
}

bool net_ota_test(void)
{
    ota_release_t release = {0};
    const char *valid = "{\"version\":\"1.2.3\",\"url\":\"" RELEASE_BASE
        "download/v1.2.3/spidarium.bin\",\"size\":1024,"
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"storage\":{\"size\":2048,"
        "\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}";
    bool ok = parse_release(valid, &release) && release.size == 1024 &&
        release.storage_size == 2048 && release.storage_hash[0] == 0xaa;
    ok = ok && !parse_release("{}", &release) && !parse_release("{", &release);
    release_range_t range = { .offset = 128, .size = 1024 };
    char values[][40] = { "bytes 128-1023/1024", "bytes 0-1023/1024",
        "bytes 128-1022/1024", "bytes 128-1023/2048", "bytes 128-1023/1024x" };
    esp_http_client_event_t event = { .event_id = HTTP_EVENT_ON_HEADER,
        .user_data = &range, .header_key = "content-range" };
    for (unsigned i = 0; i < sizeof values / sizeof values[0]; i++) {
        event.header_value = values[i];
        release_headers(&event);
        ok = ok && range.valid == (i == 0);
    }
    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "OTA running version=%s partition=%s",
             esp_app_get_description()->version, running ? running->label : "missing");
    ESP_LOGI(TAG, "OTA release manifest validation: %s", ok ? "PASS" : "FAIL");
    return ok;
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
                            wifi_ap_record_t ap;
                            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
                                ESP_LOGI(TAG, "OTA link RSSI=%d channel=%u", ap.rssi, ap.primary);
                            if (time(NULL) >= 1577836800 || sync_time()) {
                                if (job.url[0]) ota(job.url);
                                else ota_latest();
                            }
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

bool net_ota_latest(void)
{
    if (!net_has_credentials()) {
        ESP_LOGW(TAG, "latest OTA rejected: save a network first");
        return false;
    }
    return queue(&(job_t){.kind = JOB_OTA, .index = 0});
}
