#include "album.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "spidarium.album";
static atomic_bool s_busy, s_success;
static atomic_uint s_generation;
static album_slot_t s_slots[ALBUM_CAPACITY];
static sd_screenshot_t s_capture;
static uint8_t s_slot;
static album_image_t s_loaded;
static char s_status[96] = "Insert SD Card and Refresh";

typedef struct {
    uint32_t width, height, row_bytes;
    bool top_down;
} bmp_info_t;

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = value >> (8 * i);
}

static void photo_path(char *path, size_t size, unsigned slot)
{
    snprintf(path, size, SD_MOUNT_POINT "/spidarium-photo-%u.bmp", slot);
}

static bool fail(const char *status)
{
    snprintf(s_status, sizeof s_status, "%s", status);
    ESP_LOGE(TAG, "%s (errno=%d)", status, errno);
    return false;
}

/* Accept only bounded, uncompressed RGB565 BITFIELDS BMPs. Both row orders
 * are supported; no dimensions/offsets from disk become allocation sizes
 * before the complete header and exact on-disk length have been checked. */
static bool read_header(FILE *file, bmp_info_t *info)
{
    uint8_t h[66];
    struct stat st;
    if (fstat(fileno(file), &st) != 0 ||
        fread(h, 1, sizeof h, file) != sizeof h) return false;
    uint32_t width = get32(h + 18), raw_height = get32(h + 22);
    bool top_down = (raw_height & 0x80000000u) != 0;
    uint32_t height = top_down ? 0u - raw_height : raw_height;
    if (h[0] != 'B' || h[1] != 'M' || get32(h + 6) != 0 ||
        get32(h + 10) != 66 || get32(h + 14) != 40 ||
        !width || width > 480 || !height || height > 480 ||
        h[26] != 1 || h[27] != 0 || h[28] != 16 || h[29] != 0 ||
        get32(h + 30) != 3 || get32(h + 54) != 0xf800 ||
        get32(h + 58) != 0x07e0 || get32(h + 62) != 0x001f)
        return false;
    uint32_t row_bytes = (width * 2 + 3) & ~3u;
    uint32_t size = 66 + row_bytes * height;
    if (st.st_size != (off_t)size || get32(h + 2) != size ||
        get32(h + 34) != row_bytes * height) return false;
    *info = (bmp_info_t){width, height, row_bytes, top_down};
    return true;
}

static bool scan_slots(void)
{
    bool ok = true;
    for (unsigned i = 0; i < ALBUM_CAPACITY; ++i) {
        char path[80];
        photo_path(path, sizeof path, i);
        s_slots[i] = (album_slot_t){0};
        struct stat st;
        if (stat(path, &st) != 0) {
            if (errno == ENOENT) continue;
            s_slots[i].occupied = true;
            ESP_LOGE(TAG, "photo %u stat failed (errno=%d)", i, errno);
            ok = false;
            continue;
        }
        s_slots[i].occupied = true;
        FILE *file = fopen(path, "rb");
        bmp_info_t info;
        bool valid = file && read_header(file, &info);
        if (file && fclose(file) != 0) valid = false;
        if (valid) {
            s_slots[i].valid = true;
            s_slots[i].width = info.width;
            s_slots[i].height = info.height;
        } else {
            ESP_LOGW(TAG, "photo %u is unreadable or invalid; preserving file", i);
            ok = false;
        }
    }
    return ok;
}

static unsigned count_photos(void)
{
    unsigned count = 0;
    for (unsigned i = 0; i < ALBUM_CAPACITY; ++i) count += s_slots[i].valid;
    return count;
}

static bool delete_photo(void)
{
    char path[80];
    photo_path(path, sizeof path, s_slot);
    if (unlink(path) != 0) return fail("Photo Delete Failed; Refresh Album");
    struct stat st;
    if (stat(path, &st) == 0 || errno != ENOENT)
        return fail("Photo Deletion Could Not Be Verified");
    s_slots[s_slot] = (album_slot_t){0};
    snprintf(s_status, sizeof s_status, "Photo %u Deleted / Slot Available", s_slot + 1);
    ESP_LOGI(TAG, "%s", s_status);
    return true;
}

static bool verify_capture(const char *path, const uint8_t *header, uint32_t row_bytes)
{
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    bmp_info_t info;
    bool ok = read_header(file, &info) && info.top_down &&
              info.width == s_capture.width && info.height == s_capture.height;
    uint8_t bytes[128], expected_header[66];
    if (ok) {
        rewind(file);
        ok = fread(expected_header, 1, 66, file) == 66 &&
             memcmp(expected_header, header, 66) == 0;
    }
    for (uint32_t y = 0; ok && y < s_capture.height; ++y) {
        for (uint32_t x = 0; ok && x < s_capture.width * 2; x += sizeof bytes) {
            uint32_t n = s_capture.width * 2 - x;
            if (n > sizeof bytes) n = sizeof bytes;
            ok = fread(bytes, 1, n, file) == n &&
                 memcmp(bytes, s_capture.pixels + y * s_capture.stride + x, n) == 0;
        }
        uint32_t padding = row_bytes - s_capture.width * 2;
        if (ok && padding) {
            ok = fread(bytes, 1, padding, file) == padding;
            for (uint32_t i = 0; ok && i < padding; ++i) ok = bytes[i] == 0;
        }
    }
    if (fgetc(file) != EOF || ferror(file)) ok = false;
    if (fclose(file) != 0) ok = false;
    return ok;
}

static bool capture_photo(void)
{
    if (!scan_slots()) return fail("SD Read Failed / Damaged Photo; Files Preserved");
    unsigned slot;
    for (slot = 0; slot < ALBUM_CAPACITY && s_slots[slot].occupied; ++slot) {}
    if (slot == ALBUM_CAPACITY) return fail("Album Full (8/8); No Photos Overwritten");
    char path[80];
    photo_path(path, sizeof path, slot);
    FILE *file = fopen(path, "wbx");
    if (!file) return fail("Photo Create Failed; No Files Overwritten");
    uint32_t row_bytes = (s_capture.width * 2 + 3) & ~3u;
    uint8_t header[66] = {'B', 'M'};
    put32(header + 2, 66 + row_bytes * s_capture.height);
    put32(header + 10, 66);
    put32(header + 14, 40);
    put32(header + 18, s_capture.width);
    put32(header + 22, 0u - s_capture.height);
    header[26] = 1;
    header[28] = 16;
    put32(header + 30, 3);
    put32(header + 34, row_bytes * s_capture.height);
    put32(header + 54, 0xf800);
    put32(header + 58, 0x07e0);
    put32(header + 62, 0x001f);
    bool ok = fwrite(header, 1, sizeof header, file) == sizeof header;
    const uint8_t padding[3] = {0};
    for (uint32_t y = 0; ok && y < s_capture.height; ++y) {
        ok = fwrite(s_capture.pixels + y * s_capture.stride, 1,
                    s_capture.width * 2, file) == s_capture.width * 2;
        uint32_t pad = row_bytes - s_capture.width * 2;
        if (ok && pad) ok = fwrite(padding, 1, pad, file) == pad;
    }
    if (fflush(file) != 0) ok = false;
    if (fsync(fileno(file)) != 0) ok = false;
    if (fclose(file) != 0) ok = false;
    if (ok) ok = verify_capture(path, header, row_bytes);
    if (!ok) {
        /* This path was created exclusively by this operation; never remove
         * pre-existing files, including malformed photos found during scan. */
        if (unlink(path) != 0) ESP_LOGE(TAG, "incomplete photo cleanup failed: %s", path);
        scan_slots();
        return fail("Photo Write / Readback Failed");
    }
    s_slots[slot] = (album_slot_t){true, true, s_capture.width, s_capture.height};
    snprintf(s_status, sizeof s_status, "Photo %u Saved and Verified", slot + 1);
    ESP_LOGI(TAG, "%s: %s", s_status, path);
    return true;
}

static bool load_photo(void)
{
    char path[80];
    photo_path(path, sizeof path, s_slot);
    FILE *file = fopen(path, "rb");
    if (!file) {
        s_slots[s_slot] = (album_slot_t){.occupied = errno != ENOENT};
        return fail("Photo Read Failed / File Missing");
    }
    bmp_info_t info;
    if (!read_header(file, &info)) {
        fclose(file);
        s_slots[s_slot] = (album_slot_t){.occupied = true};
        return fail("Photo Invalid / Unsupported BMP; File Preserved");
    }
    s_slots[s_slot] = (album_slot_t){true, true, info.width, info.height};
    uint32_t stride = info.width * 2;
    uint8_t *pixels = heap_caps_malloc(stride * info.height, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pixels) {
        fclose(file);
        return fail("Not Enough PSRAM to Open Photo");
    }
    bool ok = true;
    uint8_t padding[3];
    for (uint32_t y = 0; ok && y < info.height; ++y) {
        uint32_t dest_y = info.top_down ? y : info.height - y - 1;
        ok = fread(pixels + dest_y * stride, 1, stride, file) == stride;
        uint32_t pad = info.row_bytes - stride;
        if (ok && pad) ok = fread(padding, 1, pad, file) == pad;
    }
    if (fgetc(file) != EOF || ferror(file)) ok = false;
    if (fclose(file) != 0) ok = false;
    if (!ok) {
        heap_caps_free(pixels);
        s_slots[s_slot].valid = false;
        return fail("Photo Read Failed; File Preserved");
    }
    s_loaded = (album_image_t){pixels, info.width, info.height, stride, s_slot};
    snprintf(s_status, sizeof s_status, "Photo %u Loaded", s_slot + 1);
    ESP_LOGI(TAG, "%s (%lux%lu)", s_status, (unsigned long)info.width, (unsigned long)info.height);
    return true;
}

bool album_busy(void) { return atomic_load(&s_busy); }
uint32_t album_generation(void) { return atomic_load(&s_generation); }
bool album_last_success(void) { return !album_busy() && atomic_load(&s_success); }
const char *album_status_text(void) { return album_busy() ? "Working on SD Card..." : s_status; }
const album_slot_t *album_slots(void) { return s_slots; }
unsigned album_count(void)
{
    if (album_busy()) return 0;
    return count_photos();
}

static bool request(uint8_t operation, const sd_screenshot_t *image, uint8_t slot)
{
    if (album_busy()) {
        ESP_LOGW(TAG, "album request rejected: busy");
        return false;
    }
    if (s_loaded.pixels) return fail("Open Photo Not Yet Adopted; Try Again");
    if (slot >= ALBUM_CAPACITY) return fail("Invalid Photo Slot");
    if (image && (!image->pixels || !image->width || !image->height ||
                  image->width > 480 || image->height > 480 ||
                  image->stride < image->width * 2 || image->stride > 480 * 4))
        return fail("Invalid Capture Pixels");
    atomic_store(&s_success, false);
    s_slot = slot;
    if (image) s_capture = *image;
    atomic_store(&s_busy, true);
    if (sd_request(operation)) return true;
    snprintf(s_status, sizeof s_status, "SD Busy / Photo Request Not Queued");
    memset(&s_capture, 0, sizeof s_capture);
    atomic_store(&s_busy, false);
    return false;
}

bool album_request_scan(void) { return request(SD_JOB_ALBUM_SCAN, NULL, 0); }
bool album_request_capture(const sd_screenshot_t *image)
{
    if (album_busy()) {
        ESP_LOGW(TAG, "capture request rejected: busy");
        return false;
    }
    if (!image) return fail("Invalid Capture Pixels");
    return request(SD_JOB_ALBUM_CAPTURE, image, 0);
}
bool album_request_load(uint8_t slot) { return request(SD_JOB_ALBUM_LOAD, NULL, slot); }
bool album_request_delete(uint8_t slot) { return request(SD_JOB_ALBUM_DELETE, NULL, slot); }
bool album_take_image(album_image_t *image)
{
    if (!image || album_busy() || !s_loaded.pixels) return false;
    *image = s_loaded;
    memset(&s_loaded, 0, sizeof s_loaded);
    return true;
}
void album_release_image(album_image_t *image)
{
    if (!image) return;
    heap_caps_free(image->pixels);
    memset(image, 0, sizeof *image);
}

bool album_run_job(uint8_t operation, bool mounted)
{
    bool ok = false;
    if (!mounted) {
        memset(s_slots, 0, sizeof s_slots);
        fail("SD Missing / Mount Failed; Photo Operation Failed");
    } else if (operation == SD_JOB_ALBUM_SCAN) {
        ok = scan_slots();
        if (ok) snprintf(s_status, sizeof s_status, "%u / 8 Photos on SD Card", count_photos());
        else fail("SD Read Failed / Damaged Photo; Files Preserved");
    } else if (operation == SD_JOB_ALBUM_CAPTURE) {
        ok = capture_photo();
    } else if (operation == SD_JOB_ALBUM_LOAD) {
        ok = load_photo();
    } else if (operation == SD_JOB_ALBUM_DELETE) {
        ok = delete_photo();
    } else {
        fail("Invalid Album Worker Operation");
    }
    memset(&s_capture, 0, sizeof s_capture);
    atomic_store(&s_success, ok);
    atomic_fetch_add(&s_generation, 1);
    atomic_store(&s_busy, false);
    return ok;
}

void album_fail_job(const char *reason)
{
    fail(reason);
    memset(&s_capture, 0, sizeof s_capture);
    atomic_store(&s_success, false);
    atomic_fetch_add(&s_generation, 1);
    atomic_store(&s_busy, false);
}
