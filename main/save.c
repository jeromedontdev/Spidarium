/*
 * Spidarium — versioned persistence (NVS).
 */
#include "save.h"

#include "game_config.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <string.h>

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
    if (hdr.version != SAVE_VERSION) {
        ESP_LOGW(TAG, "save version %u != %u; rejecting",
                 (unsigned)hdr.version, (unsigned)SAVE_VERSION);
        return false;
    }
    if (hdr.size != size) {
        ESP_LOGW(TAG, "save size %u != %u; rejecting",
                 (unsigned)hdr.size, (unsigned)size);
        return false;
    }
    if (len != sizeof(save_header_t) + size) {
        ESP_LOGW(TAG, "save blob length %u != %u; rejecting",
                 (unsigned)len, (unsigned)(sizeof(save_header_t) + size));
        return false;
    }

    memcpy(dst, buf + sizeof(save_header_t), size);
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
 * backup from a different firmware version is rejected instead of being loaded
 * as garbage. */

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

bool save_write_raw(const void *src, size_t len)
{
    if (!src || len < sizeof(save_header_t) || len > SAVE_MAX_PAYLOAD) {
        return false;
    }

    /* Validate before writing, so a corrupt file cannot replace a good save. */
    save_header_t hdr;
    memcpy(&hdr, src, sizeof hdr);

    if (hdr.magic != SAVE_MAGIC) {
        ESP_LOGW(TAG, "raw write: bad magic 0x%08x", (unsigned)hdr.magic);
        return false;
    }
    if (hdr.version != SAVE_VERSION) {
        ESP_LOGW(TAG, "raw write: version %u != %u",
                 (unsigned)hdr.version, (unsigned)SAVE_VERSION);
        return false;
    }
    if (len != sizeof(save_header_t) + hdr.size) {
        ESP_LOGW(TAG, "raw write: length %u != %u",
                 (unsigned)len, (unsigned)(sizeof(save_header_t) + hdr.size));
        return false;
    }

    if (!ensure_nvs()) return false;

    nvs_handle_t h;
    if (nvs_open(SAVE_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return false;

    esp_err_t err = nvs_set_blob(h, SAVE_KEY, src, len);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);

    return err == ESP_OK;
}
