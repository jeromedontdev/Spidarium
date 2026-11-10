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
#include "net.h"
#include "save.h"
#include "theme.h"
#include "power.h"
#include "buttons.h"
#include "test_console.h"

static const char *TAG = "spidarium";

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(save_init() ? ESP_OK : ESP_FAIL);
    theme_init();

    /* Restore RTC and run one bounded sync only if credentials exist.
     * The display is not started until the radio has been deinitialized. */
    net_boot_init();

    ESP_LOGI(TAG, "Starting display + LVGL");
    bsp_display_cfg_t display_cfg = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_0,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
        .touch_flags = {.swap_xy = 1, .mirror_x = 0, .mirror_y = 1},
    };
    display_cfg.lv_adapter_cfg.stack_in_psram = true;
    ESP_ERROR_CHECK(bsp_display_start_with_config(&display_cfg) ? ESP_OK : ESP_FAIL);
    net_display_ready();

    /* Mount the QOI sprite partition. If this fails the game still runs with
     * primitive rendering, so a missing/blank storage partition is not fatal. */
    if (!assets_init()) {
        ESP_LOGW(TAG, "assets unavailable; falling back to primitive rendering");
    }

    ESP_LOGI(TAG, "Spidarium booting");
    game_init();
    if (!power_init()) ESP_LOGW(TAG, "battery monitoring unavailable");
    if (!buttons_init()) ESP_LOGW(TAG, "physical buttons unavailable");

    /* Serial test console. Started last, so every command has a fully
     * initialised game, power monitor and button driver to talk to. It used to
     * start inside game_init(), which meant "buttons" ran before the GPIOs were
     * configured and reported every pin as unusable. */
    test_console_init();
}
