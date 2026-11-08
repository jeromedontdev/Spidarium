/*
 * Spidarium — versioned persistence.
 *
 * A raw struct blob is unsafe: any change to game_state_t silently reinterprets
 * old bytes as new fields. Every save is therefore wrapped in a header carrying
 * magic, version and payload size. The current size and the explicitly
 * supported append-only legacy size are accepted; other layouts are rejected.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sd.h"

/* Allocate the queue and internal flash-writing stack before starting LVGL. */
bool save_init(void);

/* Load `size` bytes into `dst`. Returns true only if a blob was present and
 * its magic, version and size all matched. On any mismatch `dst` is left
 * untouched so the caller can apply defaults. */
bool save_load(void *dst, size_t size);

/* Persist `size` bytes from `src`, stamping the current magic/version/size. */
bool save_save(const void *src, size_t size);

/* Snapshot and enqueue on the UI task; flash commits use an internal-stack worker. */
bool save_request(const void *src, size_t size);
bool save_request_erase(void);
bool save_busy(void);
bool save_writing(void);
bool save_last_ok(void);
bool save_request_sd(uint8_t operation);
bool save_request_screenshot(const sd_screenshot_t *image);
bool save_request_background(uint8_t id);
bool save_background_load(uint8_t *id);
bool save_request_raw(const void *src, size_t len);
bool save_raw_valid(const void *src, size_t len);
bool save_restore_pending(void);
bool save_format_test(void);

/* Erase the stored blob (used by tests and a future reset action). */
bool save_erase(void);

/* Raw blob access, for SD backup and restore.
 *
 * These expose the on-flash blob verbatim, header included, so a backup is a
 * byte-for-byte copy. save_write_raw() re-validates the header, so a backup
 * from a different firmware version is rejected rather than loaded as garbage.
 *
 * save_read_raw() returns the number of bytes copied, or 0 if there is no save
 * or the buffer is too small. */
size_t save_read_raw(void *dst, size_t cap);
bool   save_write_raw(const void *src, size_t len);
