idf_component_register(
    SRCS "main.c"
         "game.c"
         "companion.c"
         "life.c"
         "album.c"
         "save.c"
         "assets.c"
         "sprites.c"
         "terrarium.c"
         "ambient.c"
         "spider.c"
         "cspider.c"
         "behaviour.c"
         "buttons.c"
         "prey.c"
         "needs.c"
         "gift.c"
         "decor.c"
         "species.c"
         "theme.c"
         "mind.c"
         "ui_hud.c"
         "ui_menu.c"
         "ui_setup.c"
         "audio.c"
         "net.c"
         "rtc.c"
         "power.c"
         "sd.c"
         "meminfo.c"
         "test_console.c"
    INCLUDE_DIRS "."
    REQUIRES esp_mmap_assets esp_lv_fs esp_lv_decoder esp_codec_dev
    PRIV_REQUIRES nvs_flash esp_lvgl_adapter esp_timer
                  esp_wifi esp_netif esp_event esp_https_ota mbedtls json
                  sdmmc fatfs app_update
                  driver
)

# Same as Waveshare's lvgl demo: allows plain #include "lvgl.h"
idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(${LVGL_LIB} PRIVATE -DLV_LVGL_H_INCLUDE_SIMPLE)

# LVGL keeps at most LV_INV_BUF_SIZE dirty rectangles per frame (default 32).
# When a frame produces more, lv_refr.c throws the whole list away and redraws
# the ENTIRE screen -- see the `inv_p >= LV_INV_BUF_SIZE` branch in lv_refr.c.
# This tank invalidates ~100 areas per frame (16 motes + 6 spores + 18 strands +
# 48 particles + decor + spider), so every frame overflowed and paid for a full
# 480x480 software redraw: 76 ms of draw time, which capped the game at ~9 Hz.
# 128 rectangles costs 128 * sizeof(lv_area_t) = 2 KB of BSS and lets the
# refresh draw only what actually changed.
target_compile_options(${LVGL_LIB} PRIVATE -DLV_INV_BUF_SIZE=128)

# Silence printf-style warnings that LVGL's printf formatting triggers
target_compile_options(${LVGL_LIB} PRIVATE -Wno-format)

# Pack assets/ into the `storage` partition as QOI and generate
# mmap_generate_storage.h. QOI is mutually exclusive with SJPG/SPNG.
spiffs_create_partition_assets(
    storage
    "${CMAKE_CURRENT_LIST_DIR}/../assets"
    MMAP_FILE_SUPPORT_FORMAT ".png"
    MMAP_SUPPORT_QOI
    FLASH_IN_PROJECT
    IMPORT_INC_PATH "${CMAKE_CURRENT_LIST_DIR}"
)

# The packer also writes the enum header consumed by sprites.c.
add_dependencies(${COMPONENT_LIB} assets_storage_bin)
