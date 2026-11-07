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
void audio_play(sfx_t sfx);

/* Mute state, persisted in game_state_t. */
void audio_set_muted(bool muted);
bool audio_muted(void);
