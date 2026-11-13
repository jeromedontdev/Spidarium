#include "power.h"

#include "game.h"
#include "net.h"
#include "save.h"
#include "audio.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
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
static atomic_bool s_sleep_pending;
static atomic_uint s_usb_chime;
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

typedef struct {
    bool connected;
    uint8_t stable;
} usb_edge_t;

/* Two consecutive PMIC samples confirm each edge; errors never re-arm it. */
static int usb_changed(usb_edge_t *edge, bool valid, bool usb)
{
    if (!valid || usb == edge->connected) {
        edge->stable = 0;
        return 0;
    }
    if (++edge->stable < 2) return 0;
    edge->connected = usb;
    edge->stable = 0;
    return usb ? 1 : -1;
}

static void power_worker(void *arg)
{
    (void)arg;
    unsigned errors = 0;
    usb_edge_t usb_edge = {0};
    for (;;) {
        if (!net_busy() && !power_sleep_pending()) {
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
                int change = usb_changed(&usb_edge, true, usb);
                if (change) {
                    ESP_LOGI("spidarium.power", "USB %s; chime pending", change > 0 ? "attached" : "detached");
                    atomic_store(&s_usb_chime, change > 0 ? SFX_CHARGE : SFX_UNPLUG);
                }
                errors = 0;
            } else {
                atomic_store(&s_sample, 0);
                usb_changed(&usb_edge, false, false);
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

static const uint32_t k_timeouts[] = {30000, 60000, 120000, 300000, 0};
static const uint32_t k_delays[] = {0, 300000, 900000, 1800000, 3600000};
static const uint32_t k_wakes[] = {900, 3600, 21600};
static const char *const k_modes[] = {"Balanced", "Battery Saver", "RTC Sleep"};
static const char *const k_timeout_names[] = {"30 Seconds", "1 Minute", "2 Minutes", "5 Minutes", "Never"};
static const char *const k_delay_names[] = {"Never", "5 Minutes Idle", "15 Minutes Idle", "30 Minutes Idle", "1 Hour Idle"};
static const char *const k_wake_names[] = {"15 Minutes", "1 Hour", "6 Hours"};
const char *power_mode_name(uint8_t value) { return k_modes[value < 3 ? value : 0]; }
const char *power_timeout_name(uint8_t value) { return k_timeout_names[value < 5 ? value : 3]; }
const char *power_delay_name(uint8_t value) { return k_delay_names[value < 5 ? value : 0]; }
const char *power_wake_name(uint8_t value) { return k_wake_names[value < 3 ? value : 0]; }
bool power_sleep_pending(void) { return atomic_load(&s_sleep_pending); }
static bool s_sleep_input_held;

bool power_request_deep_sleep(uint32_t seconds)
{
    if (seconds < 5 || seconds > 86400 || net_busy() || save_busy() || save_restore_pending() ||
        power_sleep_pending()) {
        ESP_LOGW("spidarium.power", "deep sleep rejected: invalid duration or worker busy");
        return false;
    }
    atomic_store(&s_sleep_pending, true);
    if (!save_request_sleep(seconds)) {
        atomic_store(&s_sleep_pending, false);
        return false;
    }
    lv_indev_enable(NULL, false);
    s_sleep_input_held = true;
    return true;
}

void power_sleep_failed(void)
{
    ESP_LOGE("spidarium.power", "deep sleep failed; device remains awake");
    atomic_store(&s_sleep_pending, false);
}

bool power_enter_deep_sleep(uint32_t seconds)
{
    uint32_t start = xTaskGetTickCount();
    while (audio_playing() || power_writing()) {
        if (net_busy() || (xTaskGetTickCount() - start) > pdMS_TO_TICKS(3000)) {
            ESP_LOGE("spidarium.power", "deep sleep drain rejected: busy or timeout");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (net_busy() || gpio_get_level(GPIO_NUM_18) == 0) {
        ESP_LOGW("spidarium.power", "deep sleep rejected: radio busy or wake button held");
        return false;
    }
    esp_err_t err = esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000u);
    if (err == ESP_OK)
        err = esp_sleep_enable_ext1_wakeup_io(1ULL << GPIO_NUM_18, ESP_EXT1_WAKEUP_ANY_LOW);
    if (err == ESP_OK) err = esp_lv_adapter_pause(1500);
    if (err != ESP_OK) {
        ESP_LOGE("spidarium.power", "deep sleep setup failed: %s", esp_err_to_name(err));
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        return false;
    }
    err = bsp_display_backlight_off();
    if (err != ESP_OK) {
        ESP_LOGE("spidarium.power", "deep sleep display off failed: %s", esp_err_to_name(err));
        esp_lv_adapter_resume();
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        return false;
    }
    ESP_LOGI("spidarium.power", "deep sleep committed; RTC timer=%lu s; right button GPIO18 wakes",
             (unsigned long)seconds);
    vTaskDelay(pdMS_TO_TICKS(80));
    esp_deep_sleep_start();
    return false;
}

static int brightness_policy(unsigned sample, uint32_t idle, int normal,
                             uint8_t mode, uint8_t timeout_id)
{
    bool valid = sample & (1u << 23), present = sample & (1u << 20);
    bool usb = sample & (1u << 21);
    unsigned percent = (sample >> 13) & 127;
    uint32_t timeout = k_timeouts[timeout_id < 5 ? timeout_id : 3];
    if (timeout && idle >= timeout) return 0;
    if (valid && present && !usb && percent <= 10) return 5;
    if (mode && !usb && normal > (mode == 1 ? 35 : 20))
        normal = mode == 1 ? 35 : 20;
    uint32_t dim = timeout && timeout / 2 < 60000 ? timeout / 2 : 60000;
    if (idle >= dim) return normal < 15 ? normal : 15;
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
    if (power_sleep_pending()) return;
    if (s_sleep_input_held) {
        lv_indev_enable(NULL, true);
        s_sleep_input_held = false;
    }
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
    unsigned chime = atomic_exchange(&s_usb_chime, 0);
    if (chime) {
        if (audio_muted() || audio_volume() == 0)
            ESP_LOGI("spidarium.power", "USB chime silenced by sound settings");
        else if (!audio_play((sfx_t)chime))
            ESP_LOGW("spidarium.power", "USB chime rejected; see audio log");
    }
    uint32_t idle = lv_display_get_inactive_time(NULL);
    int target = brightness_policy(sample, idle, g.brightness, g.power_mode, g.screen_timeout);
    /* A manual lock (the middle button) must blank the panel too. Without this
     * the overlay was shown but the next tick recomputed the idle-based target
     * and immediately restored the backlight, so the screen only looked black
     * in a dark room. */
    if (s_locked) target = 0;
    atomic_store(&s_requested, target);
    /* A manual lock (the middle button) is sticky: the idle timer must not
     * clear it on the next tick. Only a touch or power_wake() releases it. */
    uint32_t timeout = k_timeouts[g.screen_timeout < 5 ? g.screen_timeout : 3];
    bool asleep = s_locked || (timeout && idle >= timeout && !net_busy() && lv_screen_active() == ui.screen);
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
    uint32_t delay = k_delays[g.deep_sleep_after < 5 ? g.deep_sleep_after : 0];
    if (g.power_mode == 2 && delay && idle >= delay && (sample & (1u << 23)) &&
        (sample & (1u << 20)) && !(sample & (1u << 21)) &&
        lv_screen_active() == ui.screen && !save_busy() && !net_busy())
        power_request_deep_sleep(k_wakes[g.rtc_wake < 3 ? g.rtc_wake : 0]);
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
    usb_edge_t edge = {0};
    bool edges = usb_changed(&edge, true, false) == 0 &&
        usb_changed(&edge, true, true) == 0 &&
        usb_changed(&edge, true, true) == 1 &&
        usb_changed(&edge, true, true) == 0 &&
        usb_changed(&edge, false, false) == 0 &&
        usb_changed(&edge, true, true) == 0 &&
        usb_changed(&edge, true, false) == 0 &&
        usb_changed(&edge, true, true) == 0 &&
        usb_changed(&edge, true, false) == 0 &&
        usb_changed(&edge, true, false) == -1 &&
        usb_changed(&edge, true, false) == 0 &&
        usb_changed(&edge, true, true) == 0 &&
        usb_changed(&edge, false, true) == 0 &&
        usb_changed(&edge, true, true) == 0 &&
        usb_changed(&edge, true, true) == 1;
    ESP_LOGI("spidarium.power", "USB chime edges: boot/held/error/bounce/reconnect %s",
             edges ? "PASS" : "FAIL");
    unsigned battery = (1u << 23) | (1u << 20) | (9u << 13) | 3700;
    return edges && brightness_policy(battery, 0, 50, 0, 3) == 5 &&
        brightness_policy(battery | (1u << 21), 0, 50, 0, 3) == 50 &&
        brightness_policy(0, 60000, 50, 0, 3) == 15 &&
        brightness_policy(0, 300000, 50, 0, 3) == 0 &&
        brightness_policy(battery, 300000, 50, 0, 3) == 0 &&
        brightness_policy(0, 0, 50, 0, 3) == 50 &&
        brightness_policy(0, 0, 50, 1, 0) == 35 &&
        brightness_policy(0, 0, 50, 2, 0) == 20 &&
        brightness_policy(0, 30000, 50, 0, 0) == 0 &&
        brightness_policy(0, 300000, 50, 0, 4) == 15;
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
