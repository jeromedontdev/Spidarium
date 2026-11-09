/*
 * Spidarium — physical side buttons.
 *
 * Pin map for the Waveshare ESP32-S3-Touch-AMOLED-2.16. The BSP does not
 * declare these (BSP_CAPS_BUTTONS is 0), so they are defined here. All three
 * are active-low with the board's external pull-ups; the internal pull-up is
 * enabled as a belt-and-braces measure so a missing external resistor still
 * reads idle-high.
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

#define BTN_LOCK_GPIO   GPIO_NUM_0    /* top middle */
#define BTN_DOWN_GPIO   GPIO_NUM_18   /* left       */
#define BTN_UP_GPIO     GPIO_NUM_17   /* right      */

/* Debounce: a level must be stable for this long before it counts. */
#define BTN_DEBOUNCE_MS 30u
/* Repeat interval while a volume button is held. */
#define BTN_REPEAT_MS   140u
/* Volume step per press. */
#define BTN_VOLUME_STEP 5

typedef struct {
    gpio_num_t pin;
    bool stable;        /* debounced level, true = pressed */
    bool raw;           /* last raw sample */
    uint32_t changed_at;
    uint32_t next_repeat;
} button_t;

static button_t s_buttons[] = {
    { .pin = BTN_LOCK_GPIO },
    { .pin = BTN_DOWN_GPIO },
    { .pin = BTN_UP_GPIO },
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
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << BTN_LOCK_GPIO) | (1ULL << BTN_DOWN_GPIO) |
                        (1ULL << BTN_UP_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "button GPIO config failed: %s", esp_err_to_name(err));
        return false;
    }
    for (unsigned i = 0; i < BTN_COUNT; i++) {
        s_buttons[i].raw = read_pressed(s_buttons[i].pin);
        s_buttons[i].stable = s_buttons[i].raw;
        s_buttons[i].changed_at = 0;
        s_buttons[i].next_repeat = 0;
    }
    s_ready = true;
    ESP_LOGI(TAG, "buttons ready: lock=%d down=%d up=%d",
             (int)BTN_LOCK_GPIO, (int)BTN_DOWN_GPIO, (int)BTN_UP_GPIO);
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

void buttons_tick(uint32_t now_ms)
{
    if (!s_ready) return;

    for (unsigned i = 0; i < BTN_COUNT; i++) {
        button_t *b = &s_buttons[i];
        bool pressed = read_pressed(b->pin);
        if (pressed != b->raw) {
            b->raw = pressed;
            b->changed_at = now_ms;
            continue;
        }
        if (pressed == b->stable) continue;
        if (now_ms - b->changed_at < BTN_DEBOUNCE_MS) continue;

        b->stable = pressed;
        if (!pressed) {
            b->next_repeat = 0;
            continue;
        }

        switch (i) {
        case 0: buttons_set_locked(!buttons_locked()); break;
        case 1: volume_step(-BTN_VOLUME_STEP); b->next_repeat = now_ms + BTN_REPEAT_MS; break;
        case 2: volume_step(+BTN_VOLUME_STEP); b->next_repeat = now_ms + BTN_REPEAT_MS; break;
        default: break;
        }
    }

    /* Held volume buttons repeat. The lock button is edge-only. */
    for (unsigned i = 1; i < BTN_COUNT; i++) {
        button_t *b = &s_buttons[i];
        if (!b->stable || b->next_repeat == 0) continue;
        if ((int32_t)(now_ms - b->next_repeat) < 0) continue;
        volume_step(i == 1 ? -BTN_VOLUME_STEP : +BTN_VOLUME_STEP);
        b->next_repeat = now_ms + BTN_REPEAT_MS;
    }
}
