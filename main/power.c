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
/* Set by power_sleep() (the middle button), cleared by power_wake(). The idle
 * timer cannot clear it, so a manual lock survives the next tick. */
static bool s_locked;

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
    s_locked = false;
    lv_obj_set_hidden(s_sleep_screen, true);
    atomic_store(&s_requested, g.brightness);
}

void power_wake(void)
{
    if (s_sleep_screen) wake(NULL);
    else lv_display_trigger_activity(NULL);
}

/* Lock the screen from code (the top button). The overlay is created lazily by
 * power_tick(), so this only records the request and shows it if it exists. */
void power_sleep(void)
{
    s_locked = true;
    /* Do not set s_sleeping here: power_tick() compares it against the computed
     * state to decide whether to show the overlay. Setting it early made the
     * comparison false, so the overlay never appeared. */
    if (s_sleep_screen) {
        lv_obj_set_hidden(s_sleep_screen, false);
        lv_obj_move_foreground(s_sleep_screen);
    }
    atomic_store(&s_requested, 0);
}

void power_tick(void)
{
    if (!s_sleep_screen) {
        s_sleep_screen = lv_obj_create(power_rotation_layer(ui.screen));
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
    /* A manual lock (the middle button) must blank the panel too. Without this
     * the overlay was shown but the next tick recomputed the idle-based target
     * and immediately restored the backlight, so the screen only looked black
     * in a dark room. */
    if (s_locked) target = 0;
    atomic_store(&s_requested, target);
    /* A manual lock (the middle button) is sticky: the idle timer must not
     * clear it on the next tick. Only a touch or power_wake() releases it. */
    bool asleep = s_locked || (idle >= 300000 && !net_busy() && lv_screen_active() == ui.screen);
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
/* True while the screen is locked by the middle button. This is the single
 * source of truth: a touch on the sleep overlay clears it, so a second copy in
 * the button module would go stale and leave the tank hidden. */
bool power_locked(void) { return s_locked; }
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

/* ------------------------------------------------------------------ */
/* Runtime estimate                                                    */
/* ------------------------------------------------------------------ */
/* The board has no coulomb counter, so this is a stated heuristic: the PMIC
 * percentage is scaled by a nominal full-charge runtime, and the display is
 * the dominant load, so brightness scales it. It is deliberately coarse and
 * labelled as an estimate in the UI. */
#define BATTERY_FULL_MINUTES 300u   /* ~5 h at 50% brightness */

void power_runtime_text(char *text, unsigned size)
{
    power_reading_t reading = power_reading();
    if (!reading.valid) { snprintf(text, size, "Battery Status Unavailable"); return; }
    if (!reading.present) {
        snprintf(text, size, "%s", reading.usb ? "On USB Power" : "External Power");
        return;
    }
    if (reading.charging) { snprintf(text, size, "Charging"); return; }
    if (reading.usb) { snprintf(text, size, "On USB Power"); return; }
    if (reading.percent > 100) { snprintf(text, size, "Gauge Unknown"); return; }

    /* Brightness 50% is the reference; 10%..100% maps to 1.6x..0.6x. */
    unsigned brightness = g.brightness < 10 ? 10 : g.brightness > 100 ? 100 : g.brightness;
    unsigned minutes = (unsigned)reading.percent * BATTERY_FULL_MINUTES / 100u;
    minutes = minutes * 100u / (60u + brightness * 8u / 10u);
    if (minutes < 1) { snprintf(text, size, "About 1 m Left"); return; }
    if (minutes >= 60)
        snprintf(text, size, "About %u h %u m Left", minutes / 60u, minutes % 60u);
    else
        snprintf(text, size, "About %u m Left", minutes);
}

/* ------------------------------------------------------------------ */
/* Screen rotation                                                     */
/* ------------------------------------------------------------------ */
/* The adapter does not rotate a PANEL_IF_OTHER panel, and the BSP's
 * bsp_display_rotation_set() only writes a MADCTL register that the adapter
 * never re-reads, so it has no visible effect. Rotation is therefore applied
 * as an LVGL transform on the active screen: the whole UI turns inside the
 * square panel. The panel is square, so no resolution change is needed and
 * taps still map to the same coordinates. */
static uint8_t s_rotation;

static const char *const k_rotation_names[] = {
    "Upright", "Sideways Left", "Upside Down", "Sideways Right",
};

uint8_t power_rotation(void) { return s_rotation; }

const char *power_rotation_name(uint8_t rotation)
{
    return k_rotation_names[rotation & 3];
}

/* Rotate the active screen about its centre. Called on the LVGL task.
 *
 * A screen object does not honour transform styles, so rotation is applied to
 * a full-size wrapper that holds every UI layer. The wrapper is created once
 * and reused; rotating it turns the whole UI inside the square panel. */
static lv_obj_t *s_rotor;

static void apply_rotation(lv_obj_t *screen, uint8_t rotation)
{
    if (!screen) return;
    if (!s_rotor || lv_obj_get_parent(s_rotor) != screen) {
        s_rotor = lv_obj_create(screen);
        lv_obj_remove_style_all(s_rotor);
        lv_obj_set_size(s_rotor, lv_pct(100), lv_pct(100));
        lv_obj_set_pos(s_rotor, 0, 0);
        lv_obj_set_style_bg_opa(s_rotor, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_rotor, 0, 0);
        lv_obj_set_style_pad_all(s_rotor, 0, 0);
        lv_obj_set_scrollable(s_rotor, false);
    }
    lv_obj_set_style_transform_pivot_x(s_rotor, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(s_rotor, lv_pct(50), 0);
    lv_obj_set_style_transform_rotation(s_rotor, (int32_t)rotation * 900, 0);
    lv_obj_invalidate(s_rotor);
}

bool power_rotation_set(uint8_t rotation)
{
    rotation &= 3;
    lv_obj_t *screen = lv_screen_active();
    if (!screen) {
        ESP_LOGW("spidarium.power", "rotation %u: no active screen", rotation);
        return false;
    }
    apply_rotation(screen, rotation);
    s_rotation = rotation;
    ESP_LOGI("spidarium.power", "rotation=%u (%s)", rotation, k_rotation_names[rotation]);
    return true;
}

void power_rotation_cycle(void)
{
    power_rotation_set((uint8_t)((s_rotation + 1) & 3));
}

/* Re-apply the stored rotation to a freshly created screen. */
void power_rotation_apply(lv_obj_t *screen)
{
    apply_rotation(screen, s_rotation);
}

/* The object every UI layer should be parented to. */
lv_obj_t *power_rotation_layer(lv_obj_t *screen)
{
    apply_rotation(screen, s_rotation);
    return s_rotor ? s_rotor : screen;
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
