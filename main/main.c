/*
 * Spidarium — mini jumping-spider vivarium (Waveshare ESP32-S3 Touch AMOLED 2.16")
 *
 * main.c: BSP bring-up only. All UI/game logic lives in game.c.
 *
 * Bring-up checklist (hardware smoke test):
 *   1. Display powers on, dark background appears (AMOLED = pure black bg).
 *   2. Title "Spidarium" + status bars visible (LVGL rendering OK).
 *   3. A little spider wanders the screen (animation timer OK).
 *   4. Touch the Feed / Clean / Decorate buttons (CST9220 touch OK):
 *        Feed     -> a crumb appears, spider walks to it and eats
 *        Clean    -> stats bars refill, dust motes disappear
 *        Decorate -> a random deco pixel-art pops in
 *   5. Stats decay over time; state persists across reboots (NVS).
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

#include "game.h"
#include "assets.h"

static const char *TAG = "spidarium";

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    ESP_LOGI(TAG, "Starting display + LVGL");
    bsp_display_start();

    /* Mount the QOI sprite partition. If this fails the game still runs with
     * primitive rendering, so a missing/blank storage partition is not fatal. */
    if (!assets_init()) {
        ESP_LOGW(TAG, "assets unavailable; falling back to primitive rendering");
    }

    ESP_LOGI(TAG, "Spidarium booting");
    game_init();
}
