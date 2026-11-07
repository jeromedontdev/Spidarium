/*
 * Spidarium — versioned persistence.
 *
 * A raw struct blob is unsafe: any change to game_state_t silently reinterprets
 * old bytes as new fields. Every save is therefore wrapped in a header carrying
 * magic, version and payload size, and a blob that does not match exactly is
 * rejected rather than loaded.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Load `size` bytes into `dst`. Returns true only if a blob was present and
 * its magic, version and size all matched. On any mismatch `dst` is left
 * untouched so the caller can apply defaults. */
bool save_load(void *dst, size_t size);

/* Persist `size` bytes from `src`, stamping the current magic/version/size. */
bool save_save(const void *src, size_t size);

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
