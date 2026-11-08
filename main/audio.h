/*
 * Spidarium — synthesized sound effects.
 *
 * No audio assets: every SFX is a short PCM envelope generated in C and
 * pushed to the ES8311 speaker through esp_codec_dev.
 */
#pragma once

#include "game.h"

#include <stdbool.h>

/* Bring up the speaker. Returns false if audio is unavailable; all other
 * calls then become no-ops. */
bool audio_init(void);

/* Play a sound effect. Non-blocking. */
bool audio_play(sfx_t sfx);

/* Active playback, used by the network worker's exclusive-operation drain. */
bool audio_playing(void);

/* Mute state, persisted in game_state_t. */
void audio_set_muted(bool muted);
bool audio_muted(void);

/* Speaker volume 0..100, persisted in game_state_t. Applied on the audio
 * worker's next effect so no codec write happens on the LVGL task. */
void audio_set_volume(uint8_t volume);
uint8_t audio_volume(void);
void audio_tick(uint32_t now_ms);
