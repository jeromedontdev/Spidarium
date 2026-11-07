/*
 * Spidarium — synthesized sound effects implementation.
 */
#include "audio.h"

#include "game_config.h"

#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>
#include <string.h>

static const char *TAG = "spidarium.audio";

#define PI_F 3.14159265f

/* Longest SFX we synthesize, in samples. */
#define MAX_SAMPLES (AUDIO_SAMPLE_RATE * 400 / 1000)

static esp_codec_dev_handle_t s_spk;
static bool                   s_ready;

/* One reusable buffer: SFX are short and playback is serialized. */
static int16_t s_buf[MAX_SAMPLES];

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

    esp_codec_dev_set_out_vol(s_spk, AUDIO_VOLUME);

    s_ready = true;
    ESP_LOGI(TAG, "audio ready (%d Hz mono)", AUDIO_SAMPLE_RATE);
    return true;
}

void audio_set_muted(bool muted)
{
    g.muted = muted ? 1 : 0;
}

bool audio_muted(void)
{
    return g.muted != 0;
}

void audio_play(sfx_t sfx)
{
    if (!s_ready || !s_spk) return;
    if (g.muted) return;

    int n = 0;

    switch (sfx) {
    case SFX_EAT:
        /* Two quick descending chirps: chewing. */
        n = render_sweep(880.0f, 520.0f, SFX_MS_EAT, 0.35f);
        break;

    case SFX_POUNCE:
        /* Rising whoosh. */
        n = render_sweep(320.0f, 1100.0f, SFX_MS_POUNCE, 0.40f);
        break;

    case SFX_MOLT:
        /* Slow shimmer. */
        n = render_sweep(440.0f, 1320.0f, SFX_MS_MOLT, 0.30f);
        break;

    case SFX_TAP:
        /* Soft click. */
        n = render_noise(SFX_MS_TAP, 0.25f);
        break;

    case SFX_ALERT:
        /* Two-tone warning. */
        n = render_sweep(660.0f, 440.0f, SFX_MS_ALERT, 0.35f);
        break;

    case SFX_AIM:
        /* Soft rising tick: the window is open. */
        n = render_sweep(520.0f, 760.0f, SFX_MS_AIM, 0.22f);
        break;

    case SFX_PERFECT:
        /* Bright chime: a perfect catch. */
        n = render_sweep(880.0f, 1760.0f, SFX_MS_PERFECT, 0.30f);
        break;

    default:
        return;
    }

    if (n <= 0) return;

    esp_codec_dev_write(s_spk, s_buf, n * (int)sizeof(int16_t));
}
