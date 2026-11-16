#pragma once

#include "sd.h"

#define ALBUM_CAPACITY 8

typedef struct {
    bool occupied; /* An invalid/unreadable file still reserves its slot. */
    bool valid;
    uint16_t width, height;
} album_slot_t;

typedef struct {
    uint8_t *pixels;
    uint32_t width, height, stride;
    uint8_t slot;
} album_image_t;

/* Nonblocking LVGL requests. Capture borrows immutable RGB565 pixels until
 * !sd_busy(); its caller remains responsible for destroying the snapshot. */
bool album_request_scan(void);
bool album_request_capture(const sd_screenshot_t *image);
bool album_request_load(uint8_t slot);
/* Call only after explicit user confirmation. Deletes exactly this slot. */
bool album_request_delete(uint8_t slot);
bool album_busy(void);
uint32_t album_generation(void); /* Completed worker operations only. */
bool album_last_success(void);  /* Queue acceptance is not success. */
const char *album_status_text(void);

/* Read metadata only when !album_busy(). Occupied includes damaged files. */
const album_slot_t *album_slots(void);
unsigned album_count(void); /* Number of valid photos. */

/* Transfers the completed immutable PSRAM image to LVGL. Detach every LVGL
 * reference before release. New requests reject an unclaimed loaded image. */
bool album_take_image(album_image_t *image);
void album_release_image(album_image_t *image);

/* Existing SD persistence worker only; never calls LVGL. */
bool album_run_job(uint8_t operation, bool mounted);
void album_fail_job(const char *reason);
