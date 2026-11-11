/*
 * Spidarium — synthesized sound effects implementation.
 */
#include "audio.h"
#include "audio_ambience.h"

#include "game_config.h"
#include "net.h"
#include "power.h"

#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/idf_additions.h"

#include <math.h>
#include <string.h>
#include <stdatomic.h>
#include "esp_random.h"

static const char *TAG = "spidarium.audio";

#define PI_F 3.14159265f

/* Longest SFX we synthesize, in samples. */
#define MAX_SAMPLES (AUDIO_SAMPLE_RATE * 400 / 1000)
_Static_assert(AUDIO_SAMPLE_RATE == AMBIENCE_RATE, "ambience sample rate mismatch");

static esp_codec_dev_handle_t s_spk;
static bool                   s_ready;

/* One reusable buffer: SFX are short and playback is serialized. */
static int16_t *s_buf;
static QueueHandle_t s_queue;
static atomic_bool s_playing;
static atomic_bool s_muted;
static atomic_int  s_volume = AUDIO_VOLUME;
static atomic_int  s_volume_applied = -1;

static void play_sfx(sfx_t sfx);

static bool apply_volume(void)
{
    int volume = atomic_load(&s_volume);
    if (volume == atomic_load(&s_volume_applied)) return true;
    int err = esp_codec_dev_set_out_vol(s_spk, volume);
    if (err != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "audio volume write failed (%d)", err);
        return false;
    }
    atomic_store(&s_volume_applied, volume);
    return true;
}

static void audio_worker(void *arg)
{
    (void)arg;
    sfx_t sfx;
    for (;;) {
        xQueueReceive(s_queue, &sfx, portMAX_DELAY);
        for (;;) {
            atomic_store(&s_playing, true);
            if (!net_busy() && !power_sleep_pending()) break;
            atomic_store(&s_playing, false);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        /* Volume changes are codec writes, so they belong here and never on
         * the LVGL task. */
        if (apply_volume() && !atomic_load(&s_muted)) play_sfx(sfx);
        atomic_store(&s_playing, false);
    }
}

/* ------------------------------------------------------------------ */
/* Envelope helpers                                                    */
/* ------------------------------------------------------------------ */
static float env_adsr(float t, float dur, float attack, float release)
{
    if (t < attack) return t / attack;
    if (t > dur - release) {
        float r = (dur - t) / release;
        return r < 0.0f ? 0.0f : r;
    }
    return 1.0f;
}

/* Render a tone with a linear frequency sweep and an ADSR envelope. */
static int render_sweep(float f0, float f1, uint32_t ms, float gain)
{
    int n = (int)((uint64_t)AUDIO_SAMPLE_RATE * ms / 1000u);
    if (n > MAX_SAMPLES) n = MAX_SAMPLES;

    float dur = (float)n / (float)AUDIO_SAMPLE_RATE;
    float phase = 0.0f;

    for (int i = 0; i < n; i++) {
        float t = (float)i / (float)AUDIO_SAMPLE_RATE;
        float u = (float)i / (float)n;
        float f = f0 + (f1 - f0) * u;

        phase += 2.0f * PI_F * f / (float)AUDIO_SAMPLE_RATE;
        if (phase > 2.0f * PI_F) phase -= 2.0f * PI_F;

        float e = env_adsr(t, dur, dur * 0.08f, dur * 0.45f);
        float s = sinf(phase) * e * gain;

        /* Soft clip. */
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;

        s_buf[i] = (int16_t)(s * 32000.0f);
    }
    return n;
}

/* Render a short noise burst (used for the tap click). */
static int render_noise(uint32_t ms, float gain)
{
    int n = (int)((uint64_t)AUDIO_SAMPLE_RATE * ms / 1000u);
    if (n > MAX_SAMPLES) n = MAX_SAMPLES;

    float dur = (float)n / (float)AUDIO_SAMPLE_RATE;
    uint32_t lcg = 0x12345678u;

    for (int i = 0; i < n; i++) {
        float t = (float)i / (float)AUDIO_SAMPLE_RATE;
        lcg = lcg * 1664525u + 1013904223u;
        float r = ((float)((lcg >> 16) & 0xffff) / 32768.0f) - 1.0f;

        float e = env_adsr(t, dur, dur * 0.02f, dur * 0.7f);
        float s = r * e * gain;

        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;

        s_buf[i] = (int16_t)(s * 32000.0f);
    }
    return n;
}

static bool write_pcm(int samples)
{
    int err = esp_codec_dev_write(s_spk, s_buf, samples * (int)sizeof *s_buf);
    if (err != ESP_CODEC_DEV_OK) ESP_LOGE(TAG, "audio write failed (%d)", err);
    return err == ESP_CODEC_DEV_OK;
}

static void play_ambience(bool leafy)
{
    ambience_t sound;
    ambience_init(&sound, leafy, esp_random());
    uint32_t written = 0;
    const char *reason = "complete";
    while (sound.position < sound.length) {
        bool stopping = atomic_load(&s_muted) || net_busy() || power_sleep_pending() ||
                        uxQueueMessagesWaiting(s_queue) != 0;
        int samples = stopping ? AMBIENCE_FADE : AMBIENCE_CHUNK;
        if ((uint32_t)samples > sound.length - sound.position)
            samples = (int)(sound.length - sound.position);
        for (int i = 0; i < samples; i++) {
            int16_t value = ambience_sample(&sound);
            s_buf[i] = stopping ? (int16_t)((int32_t)value * (samples - 1 - i) / samples) : value;
        }
        if (!apply_volume() || !write_pcm(samples)) {
            reason = "codec error";
            break;
        }
        written += samples;
        if (stopping) {
            reason = "interrupted";
            break;
        }
    }
    ESP_LOGI(TAG, "ambience %s: %s samples=%lu/%lu",
             leafy ? "leaves" : "air", reason,
             (unsigned long)written, (unsigned long)sound.length);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */
bool audio_init(void)
{
    if (s_ready) return true;

    if (bsp_audio_init(NULL) != ESP_OK) {
        ESP_LOGW(TAG, "bsp_audio_init failed; audio disabled");
        return false;
    }

    s_spk = bsp_audio_codec_speaker_init();
    if (!s_spk) {
        ESP_LOGW(TAG, "speaker init failed; audio disabled");
        return false;
    }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate     = AUDIO_SAMPLE_RATE,
        .channel         = 1,
        .bits_per_sample = 16,
    };

    if (esp_codec_dev_open(s_spk, &fs) != ESP_OK) {
        ESP_LOGW(TAG, "codec open failed; audio disabled");
        s_spk = NULL;
        return false;
    }

    atomic_store(&s_volume, g.volume <= 100 ? g.volume : AUDIO_VOLUME);
    if (!apply_volume()) {
        esp_codec_dev_close(s_spk);
        return false;
    }

    s_buf = heap_caps_malloc(MAX_SAMPLES * sizeof *s_buf,
                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_queue = s_buf ? xQueueCreate(4, sizeof(sfx_t)) : NULL;
    atomic_store(&s_muted, g.muted != 0);
    if (!s_queue ||
        xTaskCreateWithCaps(audio_worker, "spidarium_audio", 4096, NULL, 2, NULL,
                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        if (s_queue) vQueueDelete(s_queue);
        s_queue = NULL;
        heap_caps_free(s_buf);
        s_buf = NULL;
        esp_codec_dev_close(s_spk);
        ESP_LOGE(TAG, "audio PSRAM buffer or worker allocation failed");
        return false;
    }
    s_ready = true;
    ESP_LOGI(TAG, "audio ready (%d Hz mono)", AUDIO_SAMPLE_RATE);
    return true;
}

void audio_set_muted(bool muted)
{
    g.muted = muted ? 1 : 0;
    atomic_store(&s_muted, muted);
}

bool audio_muted(void)
{
    return g.muted != 0;
}

void audio_set_volume(uint8_t volume)
{
    if (volume > 100) volume = 100;
    g.volume = volume;
    atomic_store(&s_volume, volume);
}

uint8_t audio_volume(void)
{
    return g.volume <= 100 ? g.volume : AUDIO_VOLUME;
}

bool audio_play(sfx_t sfx)
{
    if (!s_ready || !s_spk) {
        ESP_LOGW(TAG, "audio request rejected: audio unavailable");
        return false;
    }
    if (g.muted) return false;
    if (sfx < 0 || sfx >= SFX_COUNT) {
        ESP_LOGW(TAG, "audio request rejected: invalid effect %d", (int)sfx);
        return false;
    }
    if (xQueueSend(s_queue, &sfx, 0) != pdPASS) {
        ESP_LOGW(TAG, "audio request rejected: queue full");
        return false;
    }
    return true;
}

bool audio_playing(void) { return atomic_load(&s_playing); }

bool audio_test(void)
{
    bool ok = true;
    const uint32_t expected[] = {0x02c0ba3fu, 0x096a6969u};
    for (int leafy = 0; leafy < 2; leafy++) {
        ambience_t sound;
        ambience_init(&sound, leafy, 0x12345678u);
        uint32_t hash = 2166136261u;
        int peak = 0, first = 0, last = 0;
        while (sound.position < sound.length) {
            int sample = ambience_sample(&sound);
            if (sound.position == 1) first = sample;
            last = sample;
            int magnitude = sample < 0 ? -sample : sample;
            if (magnitude > peak) peak = magnitude;
            uint16_t bits = (uint16_t)sample;
            hash = (hash ^ (bits & 255u)) * 16777619u;
            hash = (hash ^ (bits >> 8)) * 16777619u;
        }
        bool valid = hash == expected[leafy] && first == 0 && last == 0 && peak < 2000;
        ok = valid && ok;
        ESP_LOGI(TAG, "PCM %s samples=%lu hash=%08lx peak=%d endpoints=%d/%d %s",
                 leafy ? "leaves" : "air", (unsigned long)sound.length,
                 (unsigned long)hash, peak, first, last, valid ? "PASS" : "FAIL");
    }
    return ok;
}

void audio_tick(uint32_t now_ms)
{
    (void)now_ms;
}

static void play_sfx(sfx_t sfx)
{
    int n = 0;

    switch (sfx) {
    case SFX_EAT:
        /* Two quick descending chirps: chewing. */
        n = render_sweep(880.0f, 520.0f, SFX_MS_EAT, 0.16f);
        break;

    case SFX_POUNCE:
        n = render_noise(SFX_MS_POUNCE, 0.12f);
        for (int i = 1; i < n; i++) s_buf[i] = (s_buf[i] + 3 * s_buf[i - 1]) / 4;
        break;

    case SFX_MOLT:
        /* Slow shimmer. */
        n = render_sweep(440.0f, 1320.0f, SFX_MS_MOLT, 0.12f);
        break;

    case SFX_TAP:
        /* Soft click. */
        n = render_noise(SFX_MS_TAP, 0.08f);
        break;

    case SFX_ALERT:
        /* Two-tone warning. */
        n = render_sweep(660.0f, 440.0f, SFX_MS_ALERT, 0.18f);
        break;

    case SFX_AIM:
        /* Soft rising tick: the window is open. */
        n = render_sweep(520.0f, 760.0f, SFX_MS_AIM, 0.08f);
        break;

    case SFX_PERFECT:
        /* Bright chime: a perfect catch. */
        n = render_sweep(880.0f, 1760.0f, SFX_MS_PERFECT, 0.18f);
        break;
    case SFX_RUSTLE:
        play_ambience(false);
        return;
    case SFX_CHIRP:
        n = render_sweep(1800, 1800, 160, 0.025f);
        break;
    case SFX_DROP:
        play_ambience(true);
        return;

    default:
        return;
    }

    if (n <= 0) return;

    if (write_pcm(n))
        ESP_LOGI(TAG, "effect %d: written samples=%d", (int)sfx, n);
}
