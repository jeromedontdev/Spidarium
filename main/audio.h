/*
 * Spidarium — synthesized sound effects.
 *
 * No audio assets: effects, long softly faded air/leaf ambience and a gentle
 * looping background-music stream are synthesized on the worker and pushed to
 * the ES8311 speaker.
 */
#pragma once

#include "game.h"

#include <stdbool.h>

/* Bring up the speaker. Returns false if audio is unavailable; all other
 * calls then become no-ops. */
bool audio_init(void);

/* Queue an effect without blocking. Foreground requests fade out ambience. */
bool audio_play(sfx_t sfx);

/* Active playback, used by the network worker's exclusive-operation drain. */
bool audio_playing(void);
/* Deterministic PCM self-test; call only from the console worker, not LVGL. */
bool audio_test(void);

/* Background music request, independent of effects but silenced by master
 * mute, radio jobs and sleep. Atomic publish only: safe from LVGL; the worker
 * starts (slow fade in) or stops (20 ms fade) at its next chunk/poll. Effects
 * fade music out and it resumes afterwards. The caller persists the setting. */
void audio_set_music(bool enabled);
bool audio_music(void);

/* Mute state, persisted in game_state_t. */
void audio_set_muted(bool muted);
bool audio_muted(void);

/* Speaker volume 0..100, persisted in game_state_t. Applied on the audio
 * worker's next effect so no codec write happens on the LVGL task. */
void audio_set_volume(uint8_t volume);
uint8_t audio_volume(void);
void audio_tick(uint32_t now_ms);
