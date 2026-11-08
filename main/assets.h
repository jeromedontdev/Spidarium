/*
 * Spidarium — sprite asset access.
 *
 * The build packs assets/ into the `storage` SPIFFS partition as QOI and
 * generates mmap_generate_storage.h, which exposes every file as an enum
 * member (MMAP_STORAGE_SPIDER_N_WALK_0, ...). This module mounts that
 * partition and hands out LVGL image descriptors.
 */
#pragma once

#include "lvgl.h"

#include <stdbool.h>
#include <stdint.h>

/* Mount the asset partition and register the LVGL filesystem + decoder.
 * Safe to call once from game_init(); returns false if assets are missing,
 * in which case the game falls back to primitive rendering. */
bool assets_init(void);

/* True once assets_init() has succeeded. */
bool assets_ready(void);

/* Look up an image by its generated enum index. Returns NULL if assets are
 * not mounted or the index is out of range. The persistent descriptor and
 * decoded RGB565A8 pixels live in PSRAM and must not be freed. */
const lv_image_dsc_t *assets_image(int index);

/* Convenience: number of files in the asset partition. */
int assets_count(void);
