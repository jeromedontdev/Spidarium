"""Execute the production album worker against real host BMP files, not LVGL."""
from pathlib import Path
import shutil
import subprocess
import unittest
import uuid


ROOT = Path(__file__).resolve().parents[1]


class AlbumNativeTests(unittest.TestCase):
    def test_sd_restore_snapshot_ownership(self):
        compiler = shutil.which("gcc")
        if not compiler:
            self.skipTest("gcc is required for native SD ownership tests")
        work = ROOT / "release" / ("sd-host-" + uuid.uuid4().hex)
        work.mkdir(parents=True)
        self.addCleanup(shutil.rmtree, work)
        sd = (ROOT / "main" / "sd.c").read_text()
        take = sd[sd.index("bool sd_take_restored("):sd.index("bool sd_request(")]
        restore_start = sd.index("    if (ok && operation == SD_JOB_RESTORE)")
        restore = sd[restore_start:sd.index("    if (ok && operation == SD_JOB_SCREENSHOT)", restore_start)]
        source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef struct { char bytes[3096]; } game_state_t;
typedef struct { char bytes[6]; } decor_item_t;
static game_state_t *s_restored_state;
static decor_item_t *s_restored_extra;
static bool s_restored, busy, oom, restore_ok = true, load_ok = true;
static const char *s_status;
static unsigned allocated, released, adopted;
#define TAG ""
#define ESP_LOGE(...) ((void)0)
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define SD_JOB_RESTORE 3
static void *heap_caps_malloc(size_t size, unsigned caps) {
    assert(caps == 3); if (oom) return NULL;
    ++allocated; return malloc(size);
}
static void heap_caps_free(void *ptr) { if (ptr) ++released; free(ptr); }
static bool sd_busy(void) { return busy; }
static bool sd_restore_save(void) { return restore_ok; }
static bool save_load_snapshot(game_state_t *state, size_t size, decor_item_t **extra) {
    assert(size == sizeof *state);
    if (!load_ok) return false;
    memset(state, 0x5a, size);
    *extra = malloc(sizeof **extra);
    return true;
}
static void decor_restore_extra(decor_item_t *extra) { ++adopted; free(extra); }
'''
        source += take
        source += "\nstatic bool run(void) { bool ok = true; unsigned operation = SD_JOB_RESTORE;\n"
        source += restore + "\nreturn ok; }\n"
        source += r'''
int main(void) {
    game_state_t state;
    assert(!sd_take_restored(&state));
    oom = true; assert(!run()); assert(!s_restored_state); oom = false;
    restore_ok = false; assert(!run()); assert(!s_restored_state); restore_ok = true;
    load_ok = false; assert(!run()); assert(!s_restored_state); load_ok = true;
    assert(run()); assert(s_restored && s_restored_state);
    busy = true; assert(!sd_take_restored(&state)); busy = false;
    assert(!sd_take_restored(NULL));
    assert(sd_take_restored(&state));
    assert((unsigned char)state.bytes[3095] == 0x5a);
    assert(!s_restored_state && !s_restored_extra && !s_restored);
    assert(allocated == released && adopted == 1);
    assert(!sd_take_restored(&state));
    return 0;
}
'''
        path = work / "sd_host.c"
        path.write_text(source)
        binary = work / "sd_host.exe"
        compiled = subprocess.run(
            [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
             str(path), "-o", str(binary)], capture_output=True, text=True,
        )
        self.assertEqual(compiled.returncode, 0, compiled.stderr)
        result = subprocess.run([str(binary)], cwd=work, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_worker_files_and_pixels(self):
        compiler = shutil.which("gcc")
        if not compiler:
            self.skipTest("gcc is required for native album tests")
        work = ROOT / "release" / ("album-host-" + uuid.uuid4().hex)
        work.mkdir(parents=True)
        self.addCleanup(shutil.rmtree, work)
        # Substitute only ESP allocation/logging and the SD queue boundary.
        # All BMP validation, actual file operations and public album APIs below
        # are the production implementation.
        album = (ROOT / "main" / "album.c").read_text()
        album = "\n".join(
            line for line in album.splitlines()
            if line not in {
                '#include "album.h"', '#include "esp_heap_caps.h"',
                '#include "esp_log.h"',
            }
        )
        header = (ROOT / "main" / "album.h").read_text().replace(
            '#include "sd.h"', ""
        ).replace("#pragma once", "")
        source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#define real_sync _commit
#else
#include <unistd.h>
#define real_sync fsync
#endif
static bool sync_fail;
static int host_sync(int fd) { return sync_fail ? -1 : real_sync(fd); }
#define fsync host_sync
#define ALBUM_CAPACITY 8
#define SD_MOUNT_POINT "."
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static bool oom;
static void *heap_caps_malloc(size_t size, unsigned caps) {
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return oom ? NULL : malloc(size);
}
#define heap_caps_free free
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))
enum {SD_JOB_MOUNT=1, SD_JOB_BACKUP, SD_JOB_RESTORE, SD_JOB_SCREENSHOT,
      SD_JOB_ALBUM_SCAN, SD_JOB_ALBUM_CAPTURE, SD_JOB_ALBUM_LOAD, SD_JOB_ALBUM_DELETE};
typedef struct { uint8_t *pixels; uint32_t width,height,stride; } sd_screenshot_t;
static uint8_t queued;
static bool reject_queue;
static bool sd_request(uint8_t operation) {
    if (reject_queue) return false;
    queued = operation;
    return true;
}
'''
        source += header + "\n" + album
        source += r'''
static bool finish(bool mounted) {
    uint8_t op = queued;
    queued = 0;
    return album_run_job(op, mounted);
}
static void patch32(unsigned slot, long offset, uint32_t value) {
    char path[80]; photo_path(path, sizeof path, slot);
    FILE *f = fopen(path, "r+b"); assert(f);
    uint8_t bytes[4]; put32(bytes, value);
    assert(fseek(f, offset, SEEK_SET) == 0);
    assert(fwrite(bytes, 1, 4, f) == 4); assert(fclose(f) == 0);
}
int main(void) {
    assert(album_request_scan()); assert(!album_last_success());
    assert(!finish(false)); assert(!album_busy()); assert(album_generation() == 1);
    assert(strstr(album_status_text(), "SD Missing"));
    reject_queue = true;
    assert(!album_request_scan()); assert(album_generation() == 1);
    reject_queue = false;
    assert(album_request_scan()); assert(finish(true));
    assert(album_count() == 0);
    assert(strstr(album_status_text(), "0 / 8"));
    uint8_t pixels[16] = {0x0a,0xf8, 0xe0,7, 0x1f,0, 0xaa,0xbb,
                          0xff,0xff, 0,0, 0xef,0x7b, 0xcc,0xdd};
    sd_screenshot_t shot = {pixels, 3, 2, 8};
    assert(!album_request_capture(NULL));
    sd_screenshot_t invalid = shot; invalid.width = 481;
    assert(!album_request_capture(&invalid));
    sync_fail = true;
    assert(album_request_capture(&shot)); assert(!finish(true));
    assert(album_count() == 0 && !album_slots()[0].occupied);
    assert(strstr(album_status_text(), "Write / Readback Failed"));
    sync_fail = false;
    assert(album_request_capture(&shot)); assert(album_busy());
    assert(!album_request_capture(&shot));
    assert(!album_last_success()); assert(finish(true));
    assert(album_last_success()); assert(album_count() == 1);
    assert(strstr(album_status_text(), "Saved and Verified"));
    assert(album_slots()[0].width == 3 && album_slots()[0].height == 2);
    assert(album_request_load(0)); assert(finish(true));
    assert(!album_request_scan()); /* Must adopt the pending buffer first. */
    album_image_t image = {0};
    assert(album_take_image(&image)); assert(!album_take_image(&(album_image_t){0}));
    assert(image.stride == 6 && image.width == 3 && image.height == 2);
    assert(!memcmp(image.pixels, pixels, 6));
    assert(!memcmp(image.pixels + 6, pixels + 8, 6));
    album_release_image(&image); assert(!image.pixels);
    patch32(0, 22, 2); /* bottom-up rows must be inverted on load. */
    assert(album_request_load(0)); assert(finish(true));
    assert(album_take_image(&image));
    assert(!memcmp(image.pixels, pixels + 8, 6));
    assert(!memcmp(image.pixels + 6, pixels, 6));
    album_release_image(&image);
    oom = true;
    assert(album_request_load(0)); assert(!finish(true)); oom = false;
    assert(strstr(album_status_text(), "PSRAM"));
    patch32(0, 18, 0xffffffffu);
    assert(album_request_load(0)); assert(!finish(true));
    assert(album_request_scan()); assert(!finish(true));
    assert(album_slots()[0].occupied && !album_slots()[0].valid);
    assert(album_request_capture(&shot)); assert(!finish(true));
    patch32(0, 18, 3); /* Recover fixture without deleting user-like files. */
    for (unsigned slot = 1; slot < 8; ++slot) {
        assert(album_request_capture(&shot)); assert(finish(true));
        assert(album_slots()[slot].valid);
    }
    assert(album_count() == 8);
    assert(album_request_capture(&shot)); assert(!finish(true));
    assert(strstr(album_status_text(), "Full"));
    assert(album_count() == 8);
    memset(s_slots, 0, sizeof s_slots); /* Simulated cold RAM; files persist. */
    assert(album_request_scan()); assert(finish(true));
    assert(album_count() == 8 && strstr(album_status_text(), "8 / 8"));
    unsigned generation = album_generation();
    assert(!album_request_delete(8));
    reject_queue = true;
    assert(!album_request_delete(3)); assert(album_generation() == generation);
    reject_queue = false;
    assert(album_request_delete(3)); assert(!album_last_success());
    assert(finish(true)); assert(album_last_success());
    assert(!album_slots()[3].occupied && album_count() == 7);
    assert(strstr(album_status_text(), "Photo 4 Deleted"));
    assert(album_slots()[2].valid && album_slots()[4].valid);
    assert(album_request_delete(3)); assert(!finish(true)); /* Missing is not success. */
    assert(strstr(album_status_text(), "Delete Failed"));
    assert(album_request_capture(&shot)); assert(finish(true)); /* Freed slot is reusable. */
    assert(album_slots()[3].valid && album_count() == 8);
    patch32(7, 2, 1); /* Length mismatch rejects otherwise well-formed file. */
    assert(album_request_load(7)); assert(!finish(true));
    patch32(7, 2, 82);
    patch32(7, 54, 0x7c00); /* RGB555 must not masquerade as RGB565. */
    assert(album_request_load(7)); assert(!finish(true));
    assert(!album_request_load(8));
    assert(album_request_scan());
    album_fail_job("Restore Pending / Try Again");
    assert(!album_busy() && !album_last_success());
    assert(strstr(album_status_text(), "Restore Pending"));
    queued = 0;
    /* A large valid file allocates exactly the bounded RGB565 payload. */
    assert(album_request_delete(7)); assert(finish(true)); /* Explicitly remove damaged slot. */
    assert(!album_slots()[7].occupied);
    patch32(0, 22, 0u - 2u);
    uint8_t *large = malloc(480u * 480u * 2u); assert(large);
    memset(large, 0x5a, 480u * 480u * 2u);
    sd_screenshot_t full = {large, 480, 480, 960};
    assert(album_request_capture(&full)); assert(finish(true));
    assert(album_request_load(7)); assert(finish(true));
    assert(album_take_image(&image));
    assert(image.width == 480 && image.height == 480 && image.stride == 960);
    assert(!memcmp(image.pixels, large, 480u * 480u * 2u));
    album_release_image(&image); free(large);
    puts("album native filesystem, pixels, queue, bounds, full, reboot: PASS");
    return 0;
}
'''
        path = work / "album_host.c"
        path.write_text(source)
        binary = work / "album_host.exe"
        compile_result = subprocess.run(
            [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
             str(path), "-o", str(binary)], capture_output=True, text=True,
        )
        self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
        result = subprocess.run(
            [str(binary)], cwd=work, capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("PASS", result.stdout)


if __name__ == "__main__":
    unittest.main()
