idf_component_register(
    SRCS "main.c"
         "game.c"
         "save.c"
         "assets.c"
         "sprites.c"
         "terrarium.c"
         "spider.c"
         "cspider.c"
         "behaviour.c"
         "prey.c"
         "needs.c"
         "gift.c"
         "species.c"
         "theme.c"
         "mind.c"
         "ui_hud.c"
         "ui_menu.c"
         "ui_setup.c"
         "audio.c"
         "net.c"
         "sd.c"
         "meminfo.c"
         "test_console.c"
    INCLUDE_DIRS "."
    REQUIRES esp_mmap_assets esp_lv_fs esp_lv_decoder esp_codec_dev
    PRIV_REQUIRES nvs_flash esp_lvgl_adapter
                  esp_wifi esp_netif esp_event
                  sdmmc fatfs app_update
                  driver
)

# Same as Waveshare's lvgl demo: allows plain #include "lvgl.h"
idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(${LVGL_LIB} PRIVATE -DLV_LVGL_H_INCLUDE_SIMPLE)

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
