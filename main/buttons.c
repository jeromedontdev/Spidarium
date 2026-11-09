/*
 * Spidarium — physical side buttons.
 *
 * Waveshare ESP32-S3-Touch-AMOLED-2.16 button map, per the vendor docs:
 *
 *   Left    GPIO0   BOOT strap
 *   Middle  GPIO16  PWR
 *   Right   GPIO18  user button, pulled up to VCC3V3 via R18 10K,
 *                   shorted to GND when pressed
 *
 * The BSP declares BSP_CAPS_BUTTONS 0 and never configures them, so this module
 * owns the GPIO setup.
 *
 * Short press  -> the app action (volume down, lock, volume up).
 * Long press   -> nothing. The button is left to its hardware role: holding
 *                 BOOT still enters the ROM downloader and holding PWR still
 *                 cuts power. We never drive these pins as outputs and never
 *                 hold them low.
 *
 * GPIO0 and GPIO16 are read-only inputs here. GPIO16 is not a normal GPIO on
 * every ESP32-S3 board revision (it is the PSRAM/SPIHD pin on octal-PSRAM
 * parts), so each pin is configured defensively: if it cannot be read as an
 * input the module logs it and carries on with the others.
 */
#include "buttons.h"

#include "audio.h"
#include "game.h"
#include "game_config.h"
#include "power.h"

#include "driver/gpio.h"
#include "esp_log.h"

#include <stdatomic.h>

static const char *TAG = "spidarium.buttons";

#define BTN_LEFT_GPIO   GPIO_NUM_0    /* BOOT  - volume down */
#define BTN_MID_GPIO    GPIO_NUM_16   /* PWR   - lock/unlock */
#define BTN_RIGHT_GPIO  GPIO_NUM_18   /* user  - volume up   */

/* Debounce: a level must be stable for this long before it counts. */
#define BTN_DEBOUNCE_MS 30u
/* A press shorter than this is a short press. Anything longer is left to the
 * hardware role and produces no app action. */
#define BTN_LONG_MS     1200u
/* Repeat interval while a volume button is held, after the long-press window. */
#define BTN_REPEAT_MS   140u
/* Volume step per press. */
#define BTN_VOLUME_STEP 5

typedef struct {
    gpio_num_t pin;
    bool usable;        /* pin configured and readable */
    bool stable;        /* debounced level, true = pressed */
    bool raw;           /* last raw sample */
    bool long_fired;    /* long press already handled for this hold */
    uint32_t changed_at;
    uint32_t pressed_at;
    uint32_t next_repeat;
} button_t;

static button_t s_buttons[] = {
    { .pin = BTN_LEFT_GPIO },
    { .pin = BTN_MID_GPIO },
    { .pin = BTN_RIGHT_GPIO },
};
#define BTN_COUNT (sizeof s_buttons / sizeof s_buttons[0])

static atomic_bool s_locked;
static bool s_ready;

static bool read_pressed(gpio_num_t pin)
{
    return gpio_get_level(pin) == 0;
}

bool buttons_init(void)
{
    for (unsigned i = 0; i < BTN_COUNT; i++) {
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << s_buttons[i].pin,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t err = gpio_config(&cfg);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "gpio %d unavailable: %s", (int)s_buttons[i].pin,
                     esp_err_to_name(err));
            s_buttons[i].usable = false;
            continue;
        }
        s_buttons[i].usable = true;
        s_buttons[i].raw = read_pressed(s_buttons[i].pin);
        s_buttons[i].stable = s_buttons[i].raw;
        s_buttons[i].long_fired = false;
        s_buttons[i].changed_at = 0;
        s_buttons[i].pressed_at = 0;
        s_buttons[i].next_repeat = 0;
    }
    s_ready = true;
    ESP_LOGI(TAG, "buttons ready: left=%d mid=%d right=%d",
             (int)BTN_LEFT_GPIO, (int)BTN_MID_GPIO, (int)BTN_RIGHT_GPIO);
    return true;
}

bool buttons_locked(void) { return atomic_load(&s_locked); }

void buttons_set_locked(bool locked)
{
    if (atomic_load(&s_locked) == locked) return;
    atomic_store(&s_locked, locked);
    if (locked) power_sleep();
    else power_wake();
    ESP_LOGI(TAG, "screen %s", locked ? "locked" : "unlocked");
}

static void volume_step(int delta)
{
    int volume = (int)audio_volume() + delta;
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    if (volume == (int)audio_volume()) return;
    g.volume = (uint8_t)volume;
    audio_set_volume(g.volume);
    ESP_LOGI(TAG, "volume=%d", volume);
}

/* The app action for a short press. */
static void short_press(unsigned index)
{
    switch (index) {
    case 0: volume_step(-BTN_VOLUME_STEP); break;        /* left  */
    case 1: buttons_set_locked(!buttons_locked()); break; /* mid   */
    case 2: volume_step(+BTN_VOLUME_STEP); break;         /* right */
    default: break;
    }
}

void buttons_tick(uint32_t now_ms)
{
    if (!s_ready) return;

    for (unsigned i = 0; i < BTN_COUNT; i++) {
        button_t *b = &s_buttons[i];
        if (!b->usable) continue;

        bool pressed = read_pressed(b->pin);
        if (pressed != b->raw) {
            b->raw = pressed;
            b->changed_at = now_ms;
            continue;
        }
        if (pressed == b->stable) {
            /* Level unchanged. While held, decide between short and long. */
            if (!b->stable) continue;
            uint32_t held = now_ms - b->pressed_at;
            if (!b->long_fired && held >= BTN_LONG_MS) {
                /* Long press: hand the button back to its hardware role. No
                 * app action, and no repeat. */
                b->long_fired = true;
                b->next_repeat = 0;
                ESP_LOGI(TAG, "button %u (gpio %d) long press: hardware role",
                         i, (int)b->pin);
            } else if (b->long_fired && b->next_repeat &&
                       (int32_t)(now_ms - b->next_repeat) >= 0) {
                /* Only volume buttons repeat, and only after the long-press
                 * window has passed without becoming a long press. */
                volume_step(i == 0 ? -BTN_VOLUME_STEP : +BTN_VOLUME_STEP);
                b->next_repeat = now_ms + BTN_REPEAT_MS;
            }
            continue;
        }
        if (now_ms - b->changed_at < BTN_DEBOUNCE_MS) continue;

        b->stable = pressed;
        if (pressed) {
            b->pressed_at = now_ms;
            b->long_fired = false;
            b->next_repeat = 0;
            ESP_LOGI(TAG, "button %u (gpio %d) down", i, (int)b->pin);
        } else {
            uint32_t held = now_ms - b->pressed_at;
            if (!b->long_fired && held < BTN_LONG_MS) {
                ESP_LOGI(TAG, "button %u (gpio %d) short press (%ums)",
                         i, (int)b->pin, (unsigned)held);
                short_press(i);
            }
            b->long_fired = false;
            b->next_repeat = 0;
        }
    }
}
