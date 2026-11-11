/*
 * Spidarium — sprite asset access (mmap + QOI).
 */
#include "assets.h"

#include "game_config.h"

#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_lv_fs.h"
#include "esp_mmap_assets.h"
#include "esp_heap_caps.h"

#include "mmap_generate_storage.h"

#include <string.h>

static const char *TAG = "spidarium.assets";

static mmap_assets_handle_t s_assets;
static esp_lv_fs_handle_t   s_fs;
static bool                 s_ready;

/* Persistent per-asset descriptors. See assets_image(). */
static lv_image_dsc_t s_dsc[MMAP_STORAGE_FILES];
static bool           s_dsc_used[MMAP_STORAGE_FILES];
static lv_image_dsc_t s_background;
static int s_background_index = -1;

static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static bool decode_qoi_rgb565a8(const uint8_t *qoi, size_t qoi_size,
                                uint32_t width, uint32_t height,
                                uint8_t *pixels_out, bool opaque)
{
    if (!qoi || !pixels_out || qoi_size < 22 ||
        memcmp(qoi, "qoif", 4) != 0 ||
        read_be32(qoi + 4) != width || read_be32(qoi + 8) != height ||
        (qoi[12] != 3 && qoi[12] != 4)) {
        return false;
    }

    size_t pixel_count = (size_t)width * height;
    if (height != 0 && pixel_count / height != width) return false;

    const size_t data_end = qoi_size - 8;
    const uint8_t *marker = qoi + data_end;
    bool zero_marker = memcmp(marker, "\0\0\0\0\0\0\0\0", 8) == 0;
    if (data_end < 14 ||
        (!zero_marker && memcmp(marker, "\0\0\0\0\0\0\0\1", 8) != 0)) {
        return false;
    }

    uint8_t index[64][4] = {{0}};
    uint8_t r = 0, g = 0, b = 0, a = 255;
    size_t input = 14;
    uint8_t run = 0;

    for (size_t px = 0; px < pixel_count; px++) {
        if (run != 0) {
            run--;
        } else {
            if (input >= data_end) {
                /* The bundled qoi-conv encoder terminates with eight zero
                 * bytes and drops a final one-pixel run. Its output therefore
                 * needs one final copy of the previous pixel when exhausted. */
                if (!zero_marker || input != data_end || px != pixel_count - 1) {
                    return false;
                }
            } else {
                uint8_t op = qoi[input++];
                if (op == 0xfe) {
                    if (input + 3 > data_end) return false;
                    r = qoi[input++];
                    g = qoi[input++];
                    b = qoi[input++];
                } else if (op == 0xff) {
                    if (input + 4 > data_end) return false;
                    r = qoi[input++];
                    g = qoi[input++];
                    b = qoi[input++];
                    a = qoi[input++];
                } else if ((op & 0xc0) == 0x00) {
                    const uint8_t *slot = index[op & 0x3f];
                    r = slot[0];
                    g = slot[1];
                    b = slot[2];
                    a = slot[3];
                } else if ((op & 0xc0) == 0x40) {
                    r = (uint8_t)(r + ((op >> 4) & 0x03) - 2);
                    g = (uint8_t)(g + ((op >> 2) & 0x03) - 2);
                    b = (uint8_t)(b + (op & 0x03) - 2);
                } else if ((op & 0xc0) == 0x80) {
                    if (input >= data_end) return false;
                    uint8_t next = qoi[input++];
                    int dg = (int)(op & 0x3f) - 32;
                    int dr = dg + (int)(next >> 4) - 8;
                    int db = dg + (int)(next & 0x0f) - 8;
                    r = (uint8_t)(r + dr);
                    g = (uint8_t)(g + dg);
                    b = (uint8_t)(b + db);
                } else {
                    run = op & 0x3f;
                    if ((size_t)run > pixel_count - px - 1) return false;
                }
            }
        }

        uint8_t *rgb = pixels_out + px * 2;
        uint16_t color = (uint16_t)(((uint16_t)(r & 0xf8) << 8) |
                                    ((uint16_t)(g & 0xfc) << 3) |
                                    ((uint16_t)b >> 3));
        rgb[0] = (uint8_t)color;
        rgb[1] = (uint8_t)(color >> 8);
        if (opaque && a != 255) return false;
        if (!opaque) pixels_out[pixel_count * 2 + px] = a;

        uint8_t hash = (uint8_t)((r * 3u + g * 5u + b * 7u + a * 11u) & 0x3f);
        index[hash][0] = r;
        index[hash][1] = g;
        index[hash][2] = b;
        index[hash][3] = a;
    }

    return true;
}

static void clear_decoded_assets(void)
{
    heap_caps_free((void *)s_background.data);
    memset(&s_background, 0, sizeof s_background);
    s_background_index = -1;
    for (int i = 0; i < MMAP_STORAGE_FILES; i++) {
        if (s_dsc_used[i]) {
            heap_caps_free((void *)s_dsc[i].data);
            memset(&s_dsc[i], 0, sizeof s_dsc[i]);
            s_dsc_used[i] = false;
        }
    }
}

static bool decode_assets_to_psram(void)
{
    uint8_t *background = heap_caps_malloc(240 * 240 * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!background) {
        ESP_LOGE(TAG, "background workspace: PSRAM exhausted");
        return false;
    }
    s_background = (lv_image_dsc_t){
        .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
                    .w = 240, .h = 240 },
        .data = background, .data_size = 240 * 240 * 2,
    };
    for (int i = 0; i < MMAP_STORAGE_FILES; i++) {
        const uint8_t *qoi = mmap_assets_get_mem(s_assets, i);
        int qoi_size = mmap_assets_get_size(s_assets, i);
        int width = mmap_assets_get_width(s_assets, i);
        int height = mmap_assets_get_height(s_assets, i);
        if (!qoi || qoi_size <= 0 || width <= 0 || height <= 0) {
            ESP_LOGE(TAG, "invalid asset metadata at index %d", i);
            clear_decoded_assets();
            return false;
        }

        size_t pixel_count = (size_t)width * (size_t)height;
        if (pixel_count / (size_t)height != (size_t)width ||
            pixel_count > SIZE_MAX / 3) {
            ESP_LOGE(TAG, "asset dimensions overflow at index %d", i);
            clear_decoded_assets();
            return false;
        }

        bool full = width == 240 && height == 240;
        bool opaque = full && strncmp(mmap_assets_get_name(s_assets, i), "background_", 11) == 0;
        /* Background menus retain thumbnails, not forty-eight full sources. */
        size_t decoded_size = full ? 48 * 48 * (opaque ? 2 : 3) : pixel_count * 3;
        uint8_t *decoded = heap_caps_malloc(decoded_size,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!decoded) {
            ESP_LOGE(TAG, "PSRAM allocation failed for asset %d (%u bytes)",
                     i, (unsigned)decoded_size);
            clear_decoded_assets();
            return false;
        }
        if (!decode_qoi_rgb565a8(qoi, (size_t)qoi_size, (uint32_t)width,
                                 (uint32_t)height, full ? background : decoded, opaque)) {
            ESP_LOGE(TAG, "QOI decode failed for asset %d (%s)",
                     i, mmap_assets_get_name(s_assets, i));
            heap_caps_free(decoded);
            clear_decoded_assets();
            return false;
        }
        if (full) {
            for (int y = 0; y < 48; y++)
                for (int x = 0; x < 48; x++) {
                    ((uint16_t *)decoded)[y * 48 + x] = ((uint16_t *)background)[y * 5 * 240 + x * 5];
                    if (!opaque) decoded[48 * 48 * 2 + y * 48 + x] =
                        background[240 * 240 * 2 + y * 5 * 240 + x * 5];
                }
            s_background_index = i;
        }

        lv_image_dsc_t *d = &s_dsc[i];
        memset(d, 0, sizeof *d);
        d->header.magic = LV_IMAGE_HEADER_MAGIC;
        d->header.cf = opaque ? LV_COLOR_FORMAT_RGB565 : LV_COLOR_FORMAT_RGB565A8;
        d->header.w = full ? 48 : (uint32_t)width;
        d->header.h = full ? 48 : (uint32_t)height;
        d->data = decoded;
        d->data_size = (uint32_t)decoded_size;
        s_dsc_used[i] = true;
    }
    return true;
}

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

    if (!decode_assets_to_psram()) {
        mmap_assets_del(s_assets);
        s_assets = NULL;
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
        clear_decoded_assets();
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
    return s_dsc_used[index] ? &s_dsc[index] : NULL;
}

static const lv_image_dsc_t *full_image(int index, bool opaque)
{
    if (!s_ready || index < 0 || index >= MMAP_STORAGE_FILES ||
        strncmp(mmap_assets_get_name(s_assets, index), opaque ? "background_" : "border_", opaque ? 11 : 7) != 0 ||
        mmap_assets_get_width(s_assets, index) != 240 ||
        mmap_assets_get_height(s_assets, index) != 240) {
        ESP_LOGE(TAG, "invalid background index %d", index);
        return NULL;
    }
    if (s_background_index != index) {
        if (!decode_qoi_rgb565a8(mmap_assets_get_mem(s_assets, index),
                mmap_assets_get_size(s_assets, index), 240, 240,
                (uint8_t *)s_background.data, opaque)) {
            s_background_index = -1;
            ESP_LOGE(TAG, "background decode failed: %d", index);
            return NULL;
        }
        s_background_index = index;
    }
    s_background.header.cf = opaque ? LV_COLOR_FORMAT_RGB565 : LV_COLOR_FORMAT_RGB565A8;
    s_background.data_size = 240 * 240 * (opaque ? 2 : 3);
    return &s_background;
}

const lv_image_dsc_t *assets_background(int index) { return full_image(index, true); }
const lv_image_dsc_t *assets_border(int index) { return full_image(index, false); }
