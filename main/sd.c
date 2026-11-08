/*
 * Spidarium — SD card access, implementation.
 *
 * Mounts the card at /sdcard via SDMMC 1-bit, and provides save backup and
 * restore. Everything degrades to a no-op when no card is present, so the game
 * never depends on one.
 */
#include "sd.h"

#include "save.h"

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "ff.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <stdatomic.h>
#include <unistd.h>
#include <errno.h>

static const char *TAG = "spidarium.sd";

/* Board wiring: SDMMC 1-bit on GPIO 3 (D0), 1 (CMD), 2 (CLK). */
#define SD_PIN_D0   3
#define SD_PIN_CMD  1
#define SD_PIN_CLK  2

#define BACKUP_PATH SD_MOUNT_POINT "/spidarium-save.bin"

static bool         s_mounted;
static const char  *s_status = "Not Checked";
static sdmmc_card_t *s_card;
static atomic_bool s_busy;
static atomic_uint s_generation;
static bool s_backup_exists;
static bool s_restored;
static game_state_t s_restored_state;

bool sd_busy(void) { return atomic_load(&s_busy); }
uint32_t sd_generation(void) { return atomic_load(&s_generation); }
bool sd_take_restored(game_state_t *state)
{
    if (!state) { ESP_LOGE(TAG, "restore copy rejected: null state"); return false; }
    if (sd_busy() || !s_restored) return false;
    *state = s_restored_state;
    s_restored = false;
    return true;
}
bool sd_request(uint8_t operation)
{
    if (operation < SD_JOB_MOUNT || operation > SD_JOB_RESTORE) {
        ESP_LOGW(TAG, "SD request rejected: invalid operation");
        return false;
    }
    bool expected = false;
    if (!atomic_compare_exchange_strong(&s_busy, &expected, true)) {
        ESP_LOGW(TAG, "SD request rejected: busy");
        return false;
    }
    if (!save_request_sd(operation)) {
        atomic_store(&s_busy, false);
        return false;
    }
    return true;
}
bool sd_run_job(uint8_t operation, const game_state_t *snapshot)
{
    s_restored = false;
    bool ok = sd_mount();
    if (ok && operation == SD_JOB_BACKUP) {
        ok = snapshot && save_save(snapshot, sizeof *snapshot);
        if (ok) ok = sd_backup_save();
        s_status = ok ? "Backup Saved and Verified" : "Backup Failed; See Serial Log";
    }
    if (ok && operation == SD_JOB_RESTORE) {
        ok = sd_restore_save();
        if (ok) ok = save_load(&s_restored_state, sizeof s_restored_state);
        s_restored = ok;
        s_status = ok ? "Save Restored" : "Restore Failed; See Serial Log";
    }
    struct stat info;
    s_backup_exists = s_mounted && stat(BACKUP_PATH, &info) == 0 && info.st_size > 0;
    atomic_store(&s_busy, false);
    atomic_fetch_add(&s_generation, 1);
    return ok;
}

void sd_init(void)
{
    /* Do NOT probe at boot.
     *
     * Mounting an absent card logs a burst of sdmmc errors, and worse, the
     * mount attempt allocates DMA-capable buffers that the LCD SPI driver then
     * cannot get -- which showed up as "Draw bitmap failed: ESP_ERR_NO_MEM" and
     * a corrupted first frame. The card is only mounted when the player
     * actually opens the SD menu, so a unit with no card never pays for it. */
    s_status = "Not Checked";
}

/* Mount on demand. Called from the SD menu, not from boot. */
bool sd_mount(void)
{
    if (s_mounted) return true;

    esp_vfs_fat_sdmmc_mount_config_t mcfg = {
        .format_if_mount_failed = false,
        .max_files              = 4,
        .allocation_unit_size   = 16 * 1024,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    /* 1-bit mode: the board only breaks out D0. */
    host.flags = SDMMC_HOST_FLAG_1BIT;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk   = SD_PIN_CLK;
    slot.cmd   = SD_PIN_CMD;
    slot.d0    = SD_PIN_D0;
    /* The board has no card-detect line, so the card is assumed present and a
     * failed mount is the only signal that it is not. */
    slot.cd    = SDMMC_SLOT_NO_CD;
    slot.wp    = SDMMC_SLOT_NO_WP;

    esp_err_t err = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot,
                                            &mcfg, &s_card);
    if (err != ESP_OK) {
        /* Not an error worth shouting about: most units ship without a card. */
        ESP_LOGI(TAG, "no card (%s)", esp_err_to_name(err));
        s_mounted = false;
        s_status  = "Card Unavailable; Check Card";
        return false;
    }

    s_mounted = true;
    s_status  = "Card Ready";
    ESP_LOGI(TAG, "card mounted at %s", SD_MOUNT_POINT);
    return true;
}

bool sd_present(void)
{
    return !sd_busy() && s_mounted;
}

const char *sd_status_text(void)
{
    return sd_busy() ? "Working; Please Wait" : s_status;
}

uint64_t sd_free_bytes(void)
{
    if (!s_mounted) return 0;

    FATFS *fs = NULL;
    DWORD  free_clusters = 0;
    if (f_getfree("0:", &free_clusters, &fs) != FR_OK || !fs) return 0;

    return (uint64_t)free_clusters * fs->csize * 512u;
}

bool sd_backup_exists(void)
{
    return !sd_busy() && s_backup_exists;
}

bool sd_backup_save(void)
{
    if (!s_mounted) return false;

    /* Read the raw save blob through the same API the game uses, so the backup
     * is byte-identical to what NVS holds. */
    uint8_t buf[512];
    size_t  n = save_read_raw(buf, sizeof buf);
    if (n == 0) {
        ESP_LOGW(TAG, "backup: no save to copy");
        return false;
    }

    FILE *f = fopen(BACKUP_PATH, "wb");
    if (!f) {
        ESP_LOGE(TAG, "backup: cannot open %s (errno=%d)", BACKUP_PATH, errno);
        return false;
    }

    size_t wrote = fwrite(buf, 1, n, f);
    bool ok = wrote == n;
    if (fflush(f) != 0) ok = false;
    if (fsync(fileno(f)) != 0) ok = false;
    if (fclose(f) != 0) ok = false;
    if (!ok) {
        ESP_LOGE(TAG, "backup: write/flush/close failed (%u of %u, errno=%d)",
                 (unsigned)wrote, (unsigned)n, errno);
        return false;
    }
    f = fopen(BACKUP_PATH, "rb");
    if (!f) { ESP_LOGE(TAG, "backup: cannot reopen for verification"); return false; }
    uint8_t check[64];
    size_t offset = 0;
    while (offset < n) {
        size_t chunk = n - offset < sizeof check ? n - offset : sizeof check;
        if (fread(check, 1, chunk, f) != chunk || memcmp(check, buf + offset, chunk)) {
            ok = false;
            break;
        }
        offset += chunk;
    }
    if (fgetc(f) != EOF || ferror(f)) ok = false;
    if (fclose(f) != 0) ok = false;
    if (!ok) { ESP_LOGE(TAG, "backup: readback mismatch or I/O failure"); return false; }
    ESP_LOGI(TAG, "backup: wrote and verified %u bytes", (unsigned)n);
    return true;
}

bool sd_restore_save(void)
{
    if (!s_mounted) return false;

    FILE *f = fopen(BACKUP_PATH, "rb");
    if (!f) {
        ESP_LOGW(TAG, "restore: no backup file");
        return false;
    }

    uint8_t buf[512];
    size_t  n = fread(buf, 1, sizeof buf, f);
    fclose(f);

    if (n == 0) {
        ESP_LOGW(TAG, "restore: backup is empty");
        return false;
    }

    /* save_write_raw validates the header, so a corrupt or foreign file is
     * rejected rather than loaded as garbage. */
    if (!save_write_raw(buf, n)) {
        ESP_LOGW(TAG, "restore: backup rejected by save layer");
        return false;
    }

    ESP_LOGI(TAG, "restore: loaded %u bytes", (unsigned)n);
    return true;
}
