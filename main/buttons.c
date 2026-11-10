/*
 * Spidarium — physical side buttons.
 *
 * Waveshare ESP32-S3-Touch-AMOLED-2.16 button map, decoded from the vendor
 * schematic (KEYS block) rather than from the marketing pin list:
 *
 *   Left    GPIO0   BOOT strap, R9/R10 10K pull-ups, C12/C13 100nF debounce.
 *                   Shorted to GND when pressed -> active low.
 *   Middle  GPIO16  NOT a plain button input. GPIO16 is the SYS_OUT net: R8
 *                   10K pulls it to VCC3V3 and T1 (BSS138) pulls it to GND.
 *                   T1's gate is driven through R11 1K from the PWRON node,
 *                   which the middle key shorts to GND. So the key turns T1
 *                   OFF, SYS_OUT rises to VCC3V3 and GPIO16 reads HIGH while
 *                   the key is held. It is therefore active HIGH, and it idles
 *                   LOW — which is why an active-low read saw it as "held"
 *                   from the first tick and never produced a short press.
 *   Right   GPIO18  user button, pulled up to VCC3V3 via R18 10K, shorted to
 *                   GND when pressed -> active low.
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
 * Each pin is configured defensively: if it cannot be read as an input the
 * module logs it and carries on with the others.
 */
#include "buttons.h"

#include "audio.h"
#include "game.h"
#include "game_config.h"
#include "power.h"
#include "ui_menu.h"

#include "driver/gpio.h"
#include "esp_log.h"

#include <stdatomic.h>
#include <stdio.h>

static const char *TAG = "spidarium.buttons";

#define BTN_LEFT_GPIO   GPIO_NUM_0    /* BOOT  - volume down */
#define BTN_MID_GPIO    GPIO_NUM_16   /* SYS_OUT - lock/unlock, active HIGH */
#define BTN_RIGHT_GPIO  GPIO_NUM_18   /* user  - volume up   */

/* Debounce: a level must be stable for this long before it counts. */
#define BTN_DEBOUNCE_MS 30u
/* A press shorter than this is a short press. Anything longer is left to the
 * hardware role and produces no app action. Kept well under a second so a
 * normal tap is never mistaken for a hold. */
#define BTN_LONG_MS     1000u
/* Repeat interval while a volume button is held, after the long-press window. */
#define BTN_REPEAT_MS   140u
/* Volume step per press. */
#define BTN_VOLUME_STEP 5

typedef struct {
    gpio_num_t pin;
    bool active_high;   /* true = the pin reads 1 while the key is held */
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
    /* GPIO16 is the SYS_OUT node, not a switch to GND: see the header. */
    { .pin = BTN_MID_GPIO, .active_high = true },
    { .pin = BTN_RIGHT_GPIO },
};
#define BTN_COUNT (sizeof s_buttons / sizeof s_buttons[0])

static bool s_ready;

static bool read_pressed(const button_t *b)
{
    bool level = gpio_get_level(b->pin) != 0;
    return b->active_high ? level : !level;
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
        s_buttons[i].raw = read_pressed(&s_buttons[i]);
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

bool buttons_locked(void) { return power_locked(); }

void buttons_report(void)
{
    for (unsigned i = 0; i < BTN_COUNT; i++) {
        const button_t *b = &s_buttons[i];
        if (!b->usable) {
            printf("BUTTON %u gpio=%d unusable\n", i, (int)b->pin);
            continue;
        }
        printf("BUTTON %u gpio=%d level=%d active_high=%d pressed=%d stable=%d\n",
               i, (int)b->pin, gpio_get_level(b->pin), (int)b->active_high,
               (int)read_pressed(b), (int)b->stable);
    }
}

void buttons_set_locked(bool locked)
{
    if (power_locked() == locked) return;
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
    /* If the Sound menu is open, its slider and percentage must follow the
     * hardware buttons. This runs on the LVGL task, so it is safe. */
    game_volume_changed();
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

        bool pressed = read_pressed(b);
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
