/*
 * Spidarium — SD card access.
 *
 * The card is mounted at /sdcard and used for two things: backups of the game
 * save, and dropping firmware images for a future OTA. It is optional -- the
 * device works exactly as before with no card inserted, so every function here
 * is safe to call unconditionally.
 *
 * The board wires the card to SDMMC in 1-bit mode (D0/CMD/CLK on GPIO 3/1/2).
 * 1-bit rather than 4-bit because the other two data lines are not broken out.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "game.h"

#define SD_MOUNT_POINT "/sdcard"

/* Cheap, non-probing init. Does NOT touch the hardware: mounting an absent card
 * logs a burst of sdmmc errors and steals DMA buffers from the LCD driver, so
 * the card is only mounted on demand. */
void sd_init(void);

/* Mount the card now. Called when the player opens the SD menu. Returns false
 * if there is no card. */
bool sd_mount(void);

/* True while a card is mounted. */
bool sd_present(void);

/* A short status for the settings menu: "not present", "mounted", or
 * "mount failed". Never NULL. */
const char *sd_status_text(void);

/* Free space in bytes, or 0 if no card. */
uint64_t sd_free_bytes(void);

/* Copy the game save to the card as /sdcard/spidarium-save.bin. Returns false
 * if there is no card or the write failed. */
bool sd_backup_save(void);

/* Restore the game save from /sdcard/spidarium-save.bin. Returns false if there
 * is no card, no backup, or the file is not a valid save. */
bool sd_restore_save(void);

/* True if a backup file exists on the card. */
bool sd_backup_exists(void);

/* Nonblocking UI requests; I/O runs on the serialized persistence worker. */
enum { SD_JOB_MOUNT = 1, SD_JOB_BACKUP, SD_JOB_RESTORE };
bool sd_request(uint8_t operation);
bool sd_busy(void);
uint32_t sd_generation(void);
bool sd_take_restored(game_state_t *state);
bool sd_run_job(uint8_t operation, const game_state_t *snapshot); /* Persistence-worker-only. */
