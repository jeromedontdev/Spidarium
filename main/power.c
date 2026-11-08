#include "power.h"

#include "game.h"
#include "net.h"
#include "save.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "driver/i2c_master.h"
#include "esp_lv_adapter.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include <stdatomic.h>
#include <stdio.h>

static i2c_master_dev_handle_t s_pmic;
static atomic_uint s_sample;
static atomic_int s_requested = 50, s_applied = -1;
static atomic_bool s_writing;
static lv_obj_t *s_sleep_screen;
static bool s_sleeping, s_low_saved;

/* Snapshot packs voltage, percentage, presence, USB and charge flags together. */
static esp_err_t read_reg(uint8_t reg, uint8_t *data, size_t size)
{
    return i2c_master_transmit_receive(s_pmic, &reg, 1, data, size, 100);
}

static void power_worker(void *arg)
{
    (void)arg;
    unsigned errors = 0;
    for (;;) {
        if (!net_busy()) {
            uint8_t status[2], voltage[2], percent;
            esp_err_t err = read_reg(0, status, sizeof status);
            if (err == ESP_OK) err = read_reg(0x34, voltage, sizeof voltage);
            if (err == ESP_OK) err = read_reg(0xa4, &percent, 1);
            if (err == ESP_OK) {
                bool present = (status[0] & 8) != 0;
                bool usb = (status[0] & 32) && !(status[1] & 8);
                unsigned mv = ((voltage[0] & 31) << 8) | voltage[1];
                unsigned value = mv | ((percent <= 100 ? percent : 127u) << 13) |
                    (present << 20) | (usb << 21) | (((status[1] >> 5) == 1) << 22) |
                    (1u << 23);
                atomic_store(&s_sample, value);
                errors = 0;
            } else {
                atomic_store(&s_sample, 0);
                if (errors++ % 12 == 0)
                    ESP_LOGW("spidarium.power", "battery read: %s", esp_err_to_name(err));
            }
            int requested = atomic_load(&s_requested);
            if (requested != atomic_load(&s_applied) && !save_writing()) {
                atomic_store(&s_writing, true);
                if (net_busy()) {
                    atomic_store(&s_writing, false);
                    continue;
                }
                err = esp_lv_adapter_pause(1500);
                if (err == ESP_OK) err = bsp_display_brightness_set(requested);
                esp_err_t resume = esp_lv_adapter_resume();
                if (err == ESP_OK && resume == ESP_OK) {
                    atomic_store(&s_applied, requested);
                    ESP_LOGI("spidarium.power", "brightness=%d", requested);
                } else {
                    ESP_LOGW("spidarium.power", "display power change: %s / resume %s",
                             esp_err_to_name(err), esp_err_to_name(resume));
                }
                atomic_store(&s_writing, false);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

bool power_init(void)
{
    esp_err_t err = bsp_i2c_init();
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x34,
        .scl_speed_hz = 100000,
    };
    if (err == ESP_OK)
        err = i2c_master_bus_add_device(bsp_i2c_get_handle(), &config, &s_pmic);
    uint8_t chip = 0, adc;
    if (err == ESP_OK) err = read_reg(3, &chip, 1);
    if (err == ESP_OK && chip != 0x4a) err = ESP_ERR_INVALID_VERSION;
    if (err == ESP_OK) err = read_reg(0x30, &adc, 1);
    if (err == ESP_OK) {
        uint8_t command[] = {0x30, adc | 1};
        err = i2c_master_transmit(s_pmic, command, sizeof command, 100);
    }
    if (err != ESP_OK) {
        ESP_LOGW("spidarium.power", "AXP2101 unavailable: %s", esp_err_to_name(err));
        return false;
    }
    if (xTaskCreateWithCaps(power_worker, "pet_power", 3072, NULL, 1, NULL,
                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGE("spidarium.power", "power worker allocation failed");
        return false;
    }
    ESP_LOGI("spidarium.power", "AXP2101 identified; charger configuration unchanged");
    return true;
}

static int desired_brightness(unsigned sample, uint32_t idle, int normal)
{
    bool valid = sample & (1u << 23), present = sample & (1u << 20);
    bool usb = sample & (1u << 21);
    unsigned percent = (sample >> 13) & 127;
    if (idle >= 300000) return 0;
    if (valid && present && !usb && percent <= 10) return 5;
    if (idle >= 60000) return normal < 15 ? normal : 15;
    return normal;
}

static void wake(lv_event_t *event)
{
    (void)event;
    lv_display_trigger_activity(NULL);
    s_sleeping = false;
    lv_obj_set_hidden(s_sleep_screen, true);
    atomic_store(&s_requested, g.brightness);
}

void power_wake(void)
{
    if (s_sleep_screen) wake(NULL);
    else lv_display_trigger_activity(NULL);
}

void power_tick(void)
{
    if (!s_sleep_screen) {
        s_sleep_screen = lv_obj_create(ui.screen);
        lv_obj_remove_style_all(s_sleep_screen);
        lv_obj_set_size(s_sleep_screen, ui.W, ui.H);
        lv_obj_set_style_bg_color(s_sleep_screen, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(s_sleep_screen, LV_OPA_COVER, 0);
        lv_obj_set_scrollable(s_sleep_screen, false);
        lv_obj_add_event_cb(s_sleep_screen, wake, LV_EVENT_PRESSED, NULL);
        lv_obj_set_hidden(s_sleep_screen, true);
    }
    unsigned sample = atomic_load(&s_sample);
    uint32_t idle = lv_display_get_inactive_time(NULL);
    int target = desired_brightness(sample, idle, g.brightness);
    atomic_store(&s_requested, target);
    bool asleep = idle >= 300000 && !net_busy() && lv_screen_active() == ui.screen;
    if (asleep != s_sleeping) {
        s_sleeping = asleep;
        lv_obj_set_hidden(s_sleep_screen, !asleep);
        if (asleep) {
            lv_obj_move_foreground(s_sleep_screen);
            if (!game_save_now()) ESP_LOGW("spidarium.power", "sleep save rejected");
        }
    }
    bool low = target == 5 && (sample & (1u << 23)) && ((sample >> 13) & 127) <= 10;
    if (low && !s_low_saved) {
        if (game_save_now()) s_low_saved = true;
        ESP_LOGW("spidarium.power", "low battery: dimming; save requested");
    } else if (!low) s_low_saved = false;
}

bool power_sleeping(void) { return s_sleeping; }
bool power_writing(void) { return atomic_load(&s_writing); }
power_reading_t power_reading(void)
{
    unsigned value = atomic_load(&s_sample);
    return (power_reading_t){
        .valid = (value & (1u << 23)) != 0,
        .present = (value & (1u << 20)) != 0,
        .usb = (value & (1u << 21)) != 0,
        .charging = (value & (1u << 22)) != 0,
        .millivolts = value & 8191,
        .percent = (value >> 13) & 127,
    };
}
void power_status(char *text, unsigned size)
{
    unsigned value = atomic_load(&s_sample);
    if (!(value & (1u << 23))) snprintf(text, size, "Battery Status Unavailable");
    else if (!(value & (1u << 20)))
        snprintf(text, size, "No Battery / %s", value & (1u << 21) ? "USB Power" : "External Power");
    else {
        unsigned percent = (value >> 13) & 127;
        char charge[20];
        if (percent <= 100) snprintf(charge, sizeof charge, "%u%%", percent);
        else snprintf(charge, sizeof charge, "Gauge Unknown");
        snprintf(text, size, "%s / %u mV / %s", charge, value & 8191,
                 value & (1u << 22) ? "Charging" : value & (1u << 21) ? "USB Power" : "On Battery");
    }
}
bool power_test(void)
{
    unsigned battery = (1u << 23) | (1u << 20) | (9u << 13) | 3700;
    return desired_brightness(battery, 0, 50) == 5 &&
        desired_brightness(battery | (1u << 21), 0, 50) == 50 &&
        desired_brightness(0, 60000, 50) == 15 &&
        desired_brightness(0, 300000, 50) == 0 &&
        desired_brightness(battery, 300000, 50) == 0 &&
        desired_brightness(0, 0, 50) == 50;
}

bool power_ui_test(void)
{
    if (!s_sleep_screen) return false;
    s_sleeping = true;
    lv_obj_set_hidden(s_sleep_screen, false);
    lv_obj_send_event(s_sleep_screen, LV_EVENT_PRESSED, NULL);
    return !s_sleeping && lv_obj_is_hidden(s_sleep_screen) &&
           lv_display_get_inactive_time(NULL) < 1000 && power_test();
}
