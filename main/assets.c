/*
 * Spidarium — sprite asset access (mmap + QOI).
 */
#include "assets.h"

#include "game_config.h"

#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_lv_fs.h"
#include "esp_mmap_assets.h"

#include "mmap_generate_storage.h"

#include <string.h>

static const char *TAG = "spidarium.assets";

static mmap_assets_handle_t s_assets;
static esp_lv_fs_handle_t   s_fs;
static bool                 s_ready;

/* Persistent per-asset descriptors. See assets_image(). */
#define ASSET_MAX 128
static lv_image_dsc_t s_dsc[ASSET_MAX];
static bool           s_dsc_used[ASSET_MAX];

bool assets_init(void)
{
    if (s_ready) return true;

    const mmap_assets_config_t cfg = {
        .partition_label = "storage",
        .max_files       = MMAP_STORAGE_FILES,
        .checksum        = MMAP_STORAGE_CHECKSUM,
        .flags = {
            .mmap_enable = true,
        },
    };

    esp_err_t err = mmap_assets_new(&cfg, &s_assets);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mmap_assets_new failed (%s) - is the storage partition flashed?",
                 esp_err_to_name(err));
        return false;
    }

    const fs_cfg_t fs_cfg = {
        .fs_letter = 'A',
        .fs_nums   = MMAP_STORAGE_FILES,
        .fs_assets = s_assets,
    };

    err = esp_lv_adapter_fs_mount(&fs_cfg, &s_fs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lv_adapter_fs_mount failed (%s)", esp_err_to_name(err));
        mmap_assets_del(s_assets);
        s_assets = NULL;
        return false;
    }

    s_ready = true;
    ESP_LOGI(TAG, "assets mounted: %d files", MMAP_STORAGE_FILES);
    return true;
}

bool assets_ready(void)
{
    return s_ready;
}

int assets_count(void)
{
    return s_ready ? MMAP_STORAGE_FILES : 0;
}

const lv_image_dsc_t *assets_image(int index)
{
    if (!s_ready || index < 0 || index >= MMAP_STORAGE_FILES) return NULL;
    if (index >= ASSET_MAX) return NULL;

    /* One persistent descriptor PER asset. This must not be a single shared
     * static: lv_image_set_src() stores the pointer, not a copy, so a shared
     * descriptor is overwritten by the next lookup and every sprite object
     * ends up rendering whichever asset was requested last. That was the
     * "assets switched around" bug. */
    if (!s_dsc_used[index]) {
        const uint8_t *mem = mmap_assets_get_mem(s_assets, index);
        if (!mem) return NULL;

        lv_image_dsc_t *d = &s_dsc[index];
        memset(d, 0, sizeof *d);
        d->header.magic = LV_IMAGE_HEADER_MAGIC;
        d->header.cf    = LV_COLOR_FORMAT_RAW;
        d->header.w     = (uint32_t)mmap_assets_get_width(s_assets, index);
        d->header.h     = (uint32_t)mmap_assets_get_height(s_assets, index);
        d->data         = mem;
        d->data_size    = (uint32_t)mmap_assets_get_size(s_assets, index);

        s_dsc_used[index] = true;
    }

    return &s_dsc[index];
}
