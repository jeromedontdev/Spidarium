"""Execute native audio rendering/transitions with only codec/RTOS stubs."""
from pathlib import Path
import re
import shutil
import subprocess
import unittest
import uuid


ROOT = Path(__file__).resolve().parents[1]


class NativeAudioTests(unittest.TestCase):
    def test_pcm_master_and_stream_transitions(self):
        compiler = shutil.which("gcc")
        if not compiler:
            self.skipTest("gcc is required for native audio tests")
        work = ROOT / "release" / ("audio-host-" + uuid.uuid4().hex)
        work.mkdir(parents=True)
        self.addCleanup(shutil.rmtree, work)
        audio = (ROOT / "main" / "audio.c").read_text()
        # Keep every production function, replacing only platform boundaries.
        audio = re.sub(r'^#include "(?!audio_(?:music|ambience)\.h).+"$', "", audio,
                       flags=re.MULTILINE)
        header = (ROOT / "main" / "audio.h").read_text()
        header = header.replace('#include "game.h"', "").replace("#pragma once", "")
        game = (ROOT / "main" / "game.h").read_text()
        effect_start = game.rfind("typedef enum {", 0, game.index("SFX_EAT"))
        effects = game[effect_start:game.index("} sfx_t;") + len("} sfx_t;")]
        source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "game_config.h"
typedef void *esp_codec_dev_handle_t;
typedef void *QueueHandle_t;
typedef struct { int sample_rate, channel, bits_per_sample; } esp_codec_dev_sample_info_t;
static struct { uint8_t muted, volume; } g = {0, 100};
static int16_t captured[100000];
static unsigned captured_count, codec_calls, writes;
static bool codec_fail;
static unsigned mute_on_write, volume_on_write;
static void control_after_write(void);
#define ESP_OK 0
#define ESP_CODEC_DEV_OK 0
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static void host_log(const char *tag, const char *format, ...) {
    (void)tag; (void)format;
}
#define ESP_LOGE(...) host_log(__VA_ARGS__)
#define ESP_LOGW(...) host_log(__VA_ARGS__)
#define ESP_LOGI(...) host_log(__VA_ARGS__)
static int esp_codec_dev_write(void *speaker, void *data, int bytes) {
    assert(speaker && bytes > 0 && bytes <= 1600);
    if (codec_fail) return -1;
    assert(captured_count + bytes / 2 < sizeof captured / sizeof *captured);
    memcpy(captured + captured_count, data, bytes);
    captured_count += bytes / 2;
    ++writes;
    control_after_write();
    return 0;
}
static int esp_codec_dev_set_out_vol(void *speaker, int volume) {
    assert(speaker && volume == 100); ++codec_calls; return 0;
}
static int esp_codec_dev_close(void *speaker) { assert(speaker); return 0; }
static int esp_codec_dev_open(void *speaker, const esp_codec_dev_sample_info_t *fs) {
    assert(speaker && fs->sample_rate == 16000 && fs->channel == 1); return 0;
}
static int bsp_audio_init(void *config) { (void)config; return 0; }
static void *bsp_audio_codec_speaker_init(void) { return (void *)1; }
static void *heap_caps_malloc(size_t size, unsigned caps) {
    assert(caps == 3); return malloc(size);
}
#define heap_caps_free free
static void *xQueueCreate(unsigned count, unsigned size) {
    assert(count == 4 && size > 0); return (void *)1;
}
static void vQueueDelete(void *queue) { (void)queue; }
static int xQueueSend(void *q, void *item, int wait) {
    (void)q; (void)item; (void)wait; return pdPASS;
}
static int xQueueReceive(void *q, void *item, int wait) {
    (void)q; (void)item; (void)wait; return 0;
}
static unsigned uxQueueMessagesWaiting(void *queue) { (void)queue; return 0; }
static void vTaskDelay(unsigned ticks) { (void)ticks; }
static int xTaskCreateWithCaps(void (*entry)(void *), const char *name, int size,
                              void *arg, int priority, void *handle, unsigned caps) {
    (void)entry; (void)name; (void)arg; (void)handle;
    assert(size == 4096 && priority == 7 && caps == 3); return pdPASS;
}
static bool net_busy(void) { return false; }
static bool power_sleep_pending(void) { return false; }
static unsigned esp_random(void) { return 0x12345678; }
static int64_t esp_timer_get_time(void) { static int64_t now; return ++now; }
'''
        source += effects + "\n" + header + "\n" + audio
        source += r'''
static void control_after_write(void) {
    if (mute_on_write && writes == mute_on_write) atomic_store(&s_muted, true);
    if (volume_on_write && writes == volume_on_write) atomic_store(&s_volume, 0);
}
static void reset(int volume) {
    atomic_store(&s_volume, volume);
    atomic_store(&s_muted, false);
    s_master_gain = s_master_start = s_master_target = master_target();
    s_master_fade = AMBIENCE_FADE;
    captured_count = writes = mute_on_write = volume_on_write = 0;
    codec_fail = false;
}
static void constant_pcm(int count) {
    for (int i = 0; i < count; ++i) s_buf[i] = 16000;
    assert(write_pcm(count));
}
int main(void) {
    assert(audio_init() && codec_calls == 1);
    assert(s_master_gain == 32768);
    assert(audio_test()); /* Existing deterministic hashes for all ten scores. */
    for (int effect = SFX_EAT; effect <= SFX_PERFECT; ++effect) {
        reset(100);
        play_sfx((sfx_t)effect);
        assert(captured_count > 0);
        if(captured[0] || captured[captured_count - 1]) {
            fprintf(stderr,"effect %d endpoints=%d/%d\n",effect,captured[0],
                    captured[captured_count - 1]);
            return 2;
        }
    }
    reset(100);
    constant_pcm(800);
    assert(captured[799] == 16000);
    atomic_store(&s_volume, 0);
    constant_pcm(800);
    assert(captured[800] < 16000 && captured[800] >= 15949);
    for (unsigned i = 800; i < 1120; ++i) {
        assert(captured[i] <= captured[i - 1]);
        assert(captured[i - 1] - captured[i] <= 51);
    }
    for (unsigned i = 1119; i < captured_count; ++i) assert(!captured[i]);
    atomic_store(&s_volume, 100);
    constant_pcm(100); /* Ramp must continue across short writes. */
    int before = captured[captured_count - 1];
    atomic_store(&s_volume, 70); /* Retarget from actual gain, not old target. */
    constant_pcm(800);
    assert(abs(captured[1700] - before) <= 51);
    assert(s_master_gain >= 5826 && s_master_gain <= 5828); /* -15 dB. */
    assert(captured[captured_count - 1] >= 2844 &&
           captured[captured_count - 1] <= 2846);
    reset(100);
    mute_on_write = 1;
    constant_pcm(MAX_SAMPLES);
    assert(writes == MAX_SAMPLES / MUSIC_CHUNK);
    assert(captured[799] == 16000 && captured[800] >= 15949);
    for (unsigned i = 1119; i < captured_count; ++i) assert(!captured[i]);
    reset(100);
    volume_on_write = 1;
    play_sfx(SFX_CHARGE); /* Real effect observes volume before its next chunk. */
    assert(captured_count == CHARGE_SAMPLES);
    for (unsigned i = 1119; i < captured_count; ++i) assert(!captured[i]);
    reset(100);
    s_music_active_track = 0;
    s_music_pos = 123456;
    s_music_fade = MUSIC_FADE_IN;
    s_music_gain = 100;
    s_music_on = true;
    atomic_store(&s_music_volume, 100);
    music_chunk();
    assert(captured[0] == music_track_sample(0, 123456));
    music_stop("effect");
    assert(!captured[captured_count - 1]);
    play_sfx(SFX_CHIRP);
    assert(!captured[800 + AMBIENCE_FADE]);
    assert(!captured[captured_count - 1]);
    unsigned resume = captured_count;
    music_chunk();
    assert(!captured[resume]); /* Production effect -> music fade restart. */
    for (unsigned i = 1; i < captured_count; ++i)
        assert(abs(captured[i] - captured[i - 1]) < 1000);
    reset(100);
    s_music_pos = 123456;
    s_music_fade = MUSIC_FADE_IN;
    music_chunk();
    atomic_store(&s_volume, 0);
    music_chunk(); /* Master zero during an already playing native score. */
    for (unsigned i = 1119; i < captured_count; ++i) assert(!captured[i]);
    atomic_store(&s_volume, 100);
    music_chunk();
    for (unsigned i = 1; i < captured_count; ++i)
        assert(abs(captured[i] - captured[i - 1]) < 1000);
    atomic_store(&s_muted, true);
    music_stop("muted");
    assert(!captured[captured_count - 1]);
    reset(100);
    for (int i = 0; i < 800; ++i) s_buf[i] = INT16_MIN;
    assert(write_pcm(800));
    for (unsigned i = 0; i < captured_count; ++i) assert(captured[i] == INT16_MIN);
    assert(codec_calls == 1); /* No active-stream codec register changes. */
    codec_fail = true;
    assert(!write_pcm(800));
    free(s_buf);
    puts("native audio transitions and original waveform hashes PASS");
    return 0;
}
'''
        path = work / "audio_host.c"
        path.write_text(source)
        binary = work / "audio_host.exe"
        compiled = subprocess.run(
            [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
             "-I", str(ROOT / "main"), str(path), "-lm", "-o", str(binary)],
            capture_output=True, text=True,
        )
        self.assertEqual(compiled.returncode, 0, compiled.stderr)
        result = subprocess.run([str(binary)], cwd=work, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
