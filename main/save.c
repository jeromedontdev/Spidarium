/*
 * Spidarium — versioned persistence (NVS).
 */
#include "save.h"

#include "game_config.h"
#include "net.h"
#include "sd.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/idf_additions.h"

#include <string.h>
#include <stdatomic.h>
#include <math.h>

static const char *TAG = "spidarium.save";

/* On-flash layout: header immediately followed by the payload. */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t size;
} save_header_t;

#define SAVE_MAX_PAYLOAD 1024

static bool s_nvs_ready;
typedef struct {
    size_t size;
    bool erase;
    bool raw;
    uint8_t sd_operation;
    bool background;
    uint8_t background_id;
    sd_screenshot_t screenshot;
    uint8_t payload[SAVE_MAX_PAYLOAD];
} save_job_t;
static QueueHandle_t s_queue;
static atomic_uint s_pending;
static atomic_bool s_writing;
static atomic_bool s_last_ok = true;
static atomic_bool s_restore_pending;
static bool background_write(uint8_t id);

static void migrate_automation(game_state_t *state, uint16_t version)
{
    if (version == 2) {
        state->auto_feed = 0;
        state->auto_hunt = 0;
    } else {
        if (state->auto_feed > 1) state->auto_feed = 0;
        if (state->auto_hunt > 1) state->auto_hunt = 0;
    }
    /* Volume arrived in v4. Older blobs leave the byte zeroed, which would be
     * silent, so fall back to the compiled default. */
    if (version < 4 || state->volume > 100) state->volume = AUDIO_VOLUME;
}

static void save_worker(void *arg)
{
    (void)arg;
    save_job_t job;
    for (;;) {
        xQueueReceive(s_queue, &job, portMAX_DELAY);
        /* A queued save must not start during a radio-exclusive operation.
         * Publish active before checking busy so the net worker can drain us. */
        for (;;) {
            atomic_store(&s_writing, true);
            if (!net_busy()) break;
            atomic_store(&s_writing, false);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        game_state_t snapshot;
        if (job.sd_operation == SD_JOB_BACKUP) memcpy(&snapshot, job.payload, sizeof snapshot);
        bool ok = job.raw ? save_write_raw(job.payload, job.size) :
                  job.background ? background_write(job.background_id) :
                  job.sd_operation ? sd_run_job(job.sd_operation,
                      job.sd_operation == SD_JOB_BACKUP ? &snapshot : NULL,
                      job.sd_operation == SD_JOB_SCREENSHOT ? &job.screenshot : NULL) :
                  job.erase ? save_erase() : save_save(job.payload, job.size);
        atomic_store(&s_last_ok, ok);
        if (job.sd_operation == SD_JOB_SCREENSHOT) heap_caps_free(job.screenshot.pixels);
        if (job.raw && !ok) atomic_store(&s_restore_pending, false);
        atomic_store(&s_writing, false);
        atomic_fetch_sub(&s_pending, 1);
        if (!ok) ESP_LOGE(TAG, "queued persistence operation failed");
    }
}

bool save_init(void)
{
    if (!s_queue) {
        s_queue = xQueueCreate(4, sizeof(save_job_t));
        if (!s_queue) { ESP_LOGE(TAG, "save queue allocation failed"); return false; }
        if (xTaskCreateWithCaps(save_worker, "spidarium_save", 4096, NULL, 2, NULL,
                               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) != pdPASS) {
            vQueueDelete(s_queue);
            s_queue = NULL;
            ESP_LOGE(TAG, "save worker allocation failed");
            return false;
        }
    }
    return true;
}

static bool queue_save(const save_job_t *job)
{
    if (!job->raw && atomic_load(&s_restore_pending)) {
        ESP_LOGW(TAG, "save request rejected: restored save awaiting reboot");
        return false;
    }
    if (!s_queue) {
        ESP_LOGE(TAG, "save request rejected: worker not initialized");
        return false;
    }
    atomic_fetch_add(&s_pending, 1);
    if (xQueueSend(s_queue, job, 0) != pdPASS) {
        atomic_fetch_sub(&s_pending, 1);
        ESP_LOGE(TAG, "save request rejected: queue full");
        return false;
    }
    return true;
}

bool save_request(const void *src, size_t size)
{
    if (!src || !size || size > SAVE_MAX_PAYLOAD) {
        ESP_LOGE(TAG, "save request rejected: invalid payload");
        return false;
    }
    save_job_t job = { .size = size };
    memcpy(job.payload, src, size);
    return queue_save(&job);
}
bool save_request_erase(void) { return queue_save(&(save_job_t){ .erase = true }); }
bool save_request_sd(uint8_t operation)
{
    save_job_t job = { .sd_operation = operation };
    if (operation == SD_JOB_BACKUP) memcpy(job.payload, &g, sizeof g);
    return queue_save(&job);
}
bool save_request_background(uint8_t id)
{
    return queue_save(&(save_job_t){ .background = true, .background_id = id });
}

bool save_request_screenshot(const sd_screenshot_t *image)
{
    return queue_save(&(save_job_t){ .sd_operation = SD_JOB_SCREENSHOT, .screenshot = *image });
}
bool save_busy(void) { return atomic_load(&s_pending) != 0; }
bool save_writing(void) { return atomic_load(&s_writing); }
bool save_last_ok(void) { return atomic_load(&s_last_ok); }
bool save_restore_pending(void) { return atomic_load(&s_restore_pending); }
bool save_request_raw(const void *src, size_t len)
{
    if (!save_raw_valid(src, len)) return false;
    bool expected = false;
    if (!atomic_compare_exchange_strong(&s_restore_pending, &expected, true)) {
        ESP_LOGW(TAG, "raw restore rejected: reboot required");
        return false;
    }
    save_job_t job = { .size = len, .raw = true };
    memcpy(job.payload, src, len);
    if (queue_save(&job)) return true;
    atomic_store(&s_restore_pending, false);
    return false;
}

static bool ensure_nvs(void)
{
    if (s_nvs_ready) return true;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* Partition is unusable; wipe and retry once. */
        if (nvs_flash_erase() != ESP_OK) {
            ESP_LOGE(TAG, "nvs_flash_erase failed");
            return false;
        }
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed (%s)", esp_err_to_name(err));
        return false;
    }
    s_nvs_ready = true;
    return true;
}

bool save_background_load(uint8_t *id)
{
    if (!id || !ensure_nvs()) return false;
    nvs_handle_t h;
    esp_err_t err = nvs_open(SAVE_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return false;
    if (err == ESP_OK) {
        err = nvs_get_u8(h, "background", id);
        nvs_close(h);
    }
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        ESP_LOGE(TAG, "background load failed: %s", esp_err_to_name(err));
    return err == ESP_OK;
}

static bool background_write(uint8_t id)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(SAVE_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u8(h, "background", id);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) ESP_LOGE(TAG, "background save failed: %s", esp_err_to_name(err));
    return err == ESP_OK;
}

bool save_load(void *dst, size_t size)
{
    if (!dst || size == 0 || size > SAVE_MAX_PAYLOAD) return false;
    if (!ensure_nvs()) return false;

    nvs_handle_t h;
    if (nvs_open(SAVE_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "no save namespace yet; using defaults");
        return false;
    }

    uint8_t buf[sizeof(save_header_t) + SAVE_MAX_PAYLOAD];
    size_t len = sizeof buf;
    esp_err_t err = nvs_get_blob(h, SAVE_KEY, buf, &len);
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGI(TAG, "no save blob; using defaults");
        return false;
    }

    if (len < sizeof(save_header_t)) {
        ESP_LOGW(TAG, "save blob too short (%u); rejecting", (unsigned)len);
        return false;
    }

    save_header_t hdr;
    memcpy(&hdr, buf, sizeof hdr);

    if (hdr.magic != SAVE_MAGIC) {
        ESP_LOGW(TAG, "save magic 0x%08x != 0x%08x; rejecting",
                 (unsigned)hdr.magic, (unsigned)SAVE_MAGIC);
        return false;
    }
    if (hdr.version != SAVE_VERSION && hdr.version != 2) {
        ESP_LOGW(TAG, "save version %u != %u; rejecting",
                 (unsigned)hdr.version, (unsigned)SAVE_VERSION);
        return false;
    }
    bool legacy = size == sizeof(game_state_t) &&
                  hdr.size == offsetof(game_state_t, silk);
    if (hdr.size != size && !legacy) {
        ESP_LOGW(TAG, "save size %u != %u; rejecting",
                 (unsigned)hdr.size, (unsigned)size);
        return false;
    }
    if (len != sizeof(save_header_t) + hdr.size) {
        ESP_LOGW(TAG, "save blob length %u != %u; rejecting",
                 (unsigned)len, (unsigned)(sizeof(save_header_t) + size));
        return false;
    }

    memset(dst, 0, size);
    memcpy(dst, buf + sizeof(save_header_t), hdr.size);
    if (size == sizeof(game_state_t)) {
        game_state_t *state = dst;
        migrate_automation(state, hdr.version);
        if (hdr.version == 2) {
            ESP_LOGI(TAG, "migrated v2 automation: feed=0 hunt=0");
        }
    }
    if (legacy) {
        game_state_t *state = dst;
        state->silk = 20;
        state->brightness = 50;
        if (state->gift.used && state->gift.kind < GIFT_KINDS)
            state->gifts_owned = 1u << state->gift.kind;
        ESP_LOGI(TAG, "migrated pet: starter Silk=20; placed gift retained");
    }
    ESP_LOGI(TAG, "loaded %u bytes (v%u)", (unsigned)size, (unsigned)hdr.version);
    return true;
}

bool save_save(const void *src, size_t size)
{
    if (!src || size == 0 || size > SAVE_MAX_PAYLOAD) return false;
    if (!ensure_nvs()) return false;

    uint8_t buf[sizeof(save_header_t) + SAVE_MAX_PAYLOAD];
    save_header_t hdr = {
        .magic    = SAVE_MAGIC,
        .version  = SAVE_VERSION,
        .reserved = 0,
        .size     = (uint32_t)size,
    };
    memcpy(buf, &hdr, sizeof hdr);
    memcpy(buf + sizeof hdr, src, size);

    nvs_handle_t h;
    if (nvs_open(SAVE_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed");
        return false;
    }

    esp_err_t err = nvs_set_blob(h, SAVE_KEY, buf, sizeof hdr + size);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save failed (%s)", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool save_erase(void)
{
    if (!ensure_nvs()) return false;

    nvs_handle_t h;
    if (nvs_open(SAVE_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return false;

    esp_err_t err = nvs_erase_key(h, SAVE_KEY);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);

    return err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND;
}

/* ------------------------------------------------------------------ */
/* Raw blob access (SD backup / restore)                               */
/* ------------------------------------------------------------------ */
/* These expose the on-flash blob verbatim, header included, so a backup is a
 * byte-for-byte copy rather than a re-serialisation. That matters: the header
 * carries the magic and version, and save_write_raw() re-validates them, so a
 * unsupported layouts are rejected. The explicitly compatible v2 payload
 * migrates on load with automation disabled. */

size_t save_read_raw(void *dst, size_t cap)
{
    if (!dst || cap == 0) return 0;
    if (!ensure_nvs()) return 0;

    nvs_handle_t h;
    if (nvs_open(SAVE_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return 0;

    size_t len = cap;
    esp_err_t err = nvs_get_blob(h, SAVE_KEY, dst, &len);
    nvs_close(h);

    if (err != ESP_OK) return 0;
    return len;
}

bool save_raw_valid(const void *src, size_t len)
{
    if (!src || len < sizeof(save_header_t) || len > SAVE_MAX_PAYLOAD) {
        ESP_LOGW(TAG, "raw save rejected: invalid length");
        return false;
    }

    /* Validate before writing, so a corrupt file cannot replace a good save. */
    save_header_t hdr;
    memcpy(&hdr, src, sizeof hdr);

    if (hdr.magic != SAVE_MAGIC) {
        ESP_LOGW(TAG, "raw write: bad magic 0x%08x", (unsigned)hdr.magic);
        return false;
    }
    if (hdr.version != SAVE_VERSION && hdr.version != 2) {
        ESP_LOGW(TAG, "raw write: version %u != %u",
                 (unsigned)hdr.version, (unsigned)SAVE_VERSION);
        return false;
    }
    if (hdr.size != sizeof(game_state_t) && hdr.size != offsetof(game_state_t, silk)) {
        ESP_LOGW(TAG, "raw write: game-state size %u != %u",
                 (unsigned)hdr.size, (unsigned)sizeof(game_state_t));
        return false;
    }
    if (len != sizeof(save_header_t) + hdr.size) {
        ESP_LOGW(TAG, "raw write: length %u != %u",
                 (unsigned)len, (unsigned)(sizeof(save_header_t) + hdr.size));
        return false;
    }
    game_state_t state = {0};
    memcpy(&state, (const uint8_t *)src + sizeof hdr, hdr.size);
    if (state.hunger > 100 || state.mood > 100 || state.energy > 100 ||
        state.hygiene > 100 || state.stage > STAGE_ADULT || state.sick > 1 ||
        state.muted > 1 || state.gift.used > 1 ||
        (state.gift.used && state.gift.kind >= GIFT_KINDS) ||
        state.bond > 100 || !memchr(state.name, 0, sizeof state.name) ||
        !isfinite(state.spider_heading) || state.state > SPIDER_SICK ||
        (hdr.version == SAVE_VERSION && (state.auto_feed > 1 || state.auto_hunt > 1)) ||
        (hdr.version >= 4 && state.volume > 100) ||
        (hdr.size == sizeof state && (state.brightness < 10 || state.brightness > 100))) {        ESP_LOGW(TAG, "raw save rejected: invalid pet fields");
        return false;
    }
    return true;
}

bool save_write_raw(const void *src, size_t len)
{
    if (!save_raw_valid(src, len)) return false;
    if (!ensure_nvs()) return false;

    nvs_handle_t h;
    esp_err_t err = nvs_open(SAVE_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "raw restore open failed: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(h, SAVE_KEY, src, len);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);

    if (err != ESP_OK) ESP_LOGE(TAG, "raw restore failed: %s", esp_err_to_name(err));
    return err == ESP_OK;
}

bool save_format_test(void)
{
    uint8_t blob[sizeof(save_header_t) + sizeof(game_state_t)];
    save_header_t header = { .magic = SAVE_MAGIC, .version = SAVE_VERSION, .size = sizeof g };
    memcpy(blob, &header, sizeof header);
    memcpy(blob + sizeof header, &g, sizeof g);
    bool ok = save_raw_valid(blob, sizeof blob);
    game_state_t state = g;
    state.auto_feed = 2;
    memcpy(blob + sizeof header, &state, sizeof state);
    ok = !save_raw_valid(blob, sizeof blob) && ok;
    header.version = 2;
    memcpy(blob, &header, sizeof header);
    ok = save_raw_valid(blob, sizeof blob) && ok;
    state.auto_feed = 0xA5;
    state.auto_hunt = 0x5A;
    memcpy(blob + sizeof header, &state, sizeof state);
    ok = save_raw_valid(blob, sizeof blob) && ok;
    migrate_automation(&state, header.version);
    ok = state.auto_feed == 0 && state.auto_hunt == 0 && ok;
    header.version = SAVE_VERSION;
    state = g;
    state.auto_hunt = 2;
    memcpy(blob, &header, sizeof header);
    memcpy(blob + sizeof header, &state, sizeof state);
    ok = !save_raw_valid(blob, sizeof blob) && ok;
    state = g;
    state.volume = 101;
    memcpy(blob + sizeof header, &state, sizeof state);
    ok = !save_raw_valid(blob, sizeof blob) && ok;
    state.volume = 0;
    memcpy(blob + sizeof header, &state, sizeof state);
    ok = save_raw_valid(blob, sizeof blob) && ok;
    memcpy(blob + sizeof header, &g, sizeof g);
    header.size = offsetof(game_state_t, silk);
    memcpy(blob, &header, sizeof header);
    ok = save_raw_valid(blob, sizeof header + header.size) && ok;
    ok = !save_raw_valid(blob, sizeof header + header.size - 1) && ok;
    return ok;
}