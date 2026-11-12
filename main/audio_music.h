/*
 * Spidarium — original background music and pet bell.
 *
 * Pure integer synthesis, mirrored sample for sample by site/lib/device-audio.js
 * (site/tests/audio.test.mjs parses these tables). A 32-second D-Bm-G-A loop of
 * slowly crossfaded, gently detuned sine triads with a sparse bell melody. The
 * function is periodic, so streaming it chunk by chunk is seamless. Track 0
 * remains byte-identical; two distinct-tempo/voiced original arrangements
 * below share the same small oscillator table, never another PCM allocation.
 */
#pragma once

#include <stdint.h>

#define MUSIC_RATE 16000
#define MUSIC_CHORD 128000u
#define MUSIC_OVERLAP 32000u
#define MUSIC_NOTE 32000u
#define MUSIC_LOOP (4u * MUSIC_CHORD)
#define MUSIC_PAD_GAIN 400
#define MUSIC_MELODY_GAIN 1100
#define MUSIC_FADE_IN 24000u
#define PET_SAMPLES 2560
#define PET_SECOND 640
#define MUSIC_TEST_HASH 0x6a09516au
#define MUSIC_MOON_TEST_HASH 0x28f08634u
#define MUSIC_RAIN_TEST_HASH 0xfa2bc0bcu
#define PET_TEST_HASH 0x3e6c27c9u
#define MUSIC_TRACK_COUNT 3

static const int16_t MUSIC_SINE[256] = {
    0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393, 7179, 7962, 8739,
    9512, 10278, 11039, 11793, 12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530,
    18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594, 23170, 23731, 24279, 24811,
    25329, 25832, 26319, 26790, 27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
    30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971, 32137, 32285, 32412, 32521,
    32609, 32678, 32728, 32757, 32767, 32757, 32728, 32678, 32609, 32521, 32412, 32285,
    32137, 31971, 31785, 31580, 31356, 31113, 30852, 30571, 30273, 29956, 29621, 29268,
    28898, 28510, 28105, 27683, 27245, 26790, 26319, 25832, 25329, 24811, 24279, 23731,
    23170, 22594, 22005, 21403, 20787, 20159, 19519, 18868, 18204, 17530, 16846, 16151,
    15446, 14732, 14010, 13279, 12539, 11793, 11039, 10278, 9512, 8739, 7962, 7179,
    6393, 5602, 4808, 4011, 3212, 2410, 1608, 804, 0, -804, -1608, -2410,
    -3212, -4011, -4808, -5602, -6393, -7179, -7962, -8739, -9512, -10278, -11039, -11793,
    -12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530, -18204, -18868, -19519, -20159,
    -20787, -21403, -22005, -22594, -23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790,
    -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956, -30273, -30571, -30852, -31113,
    -31356, -31580, -31785, -31971, -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
    -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285, -32137, -31971, -31785, -31580,
    -31356, -31113, -30852, -30571, -30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683,
    -27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731, -23170, -22594, -22005, -21403,
    -20787, -20159, -19519, -18868, -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
    -12539, -11793, -11039, -10278, -9512, -8739, -7962, -7179, -6393, -5602, -4808, -4011,
    -3212, -2410, -1608, -804,
};
static const uint32_t MUSIC_PAD[24] = {
    0x04b25700u, 0x04b352a8u, 0x05eafcceu, 0x05ebf877u, 0x0709bf9cu, 0x070abb45u,
    0x03f2f987u, 0x03f3f530u, 0x04b25700u, 0x04b352a8u, 0x05eafcceu, 0x05ebf877u,
    0x03225311u, 0x03234ebau, 0x03f2f987u, 0x03f3f530u, 0x04b25700u, 0x04b352a8u,
    0x0384a0e4u, 0x03859c8du, 0x046ed677u, 0x046fd220u, 0x0545ac47u, 0x0546a7f0u,
};
static const uint32_t MUSIC_MELODY[16] = {
    0x0bd6ffc1u, 0x00000000u, 0x0a8c5437u, 0x0965b424u,
    0x07e6eeb7u, 0x00000000u, 0x0965b424u, 0x00000000u,
    0x0965b424u, 0x07e6eeb7u, 0x00000000u, 0x0645a1cbu,
    0x070a3d71u, 0x00000000u, 0x08deb314u, 0x0a8c5437u,
};
static const uint32_t PET_NOTES[2] = {
    0x0645a1cbu, 0x0965b424u,
};
/* Moonlit Hollow: low Am-F-C-G, ten-second chords and a soft sub-octave.
 * Rainy Canopy: F-C/E-Dm-Bb/D, six-second chords and an airy octave partial. */
static const uint32_t MUSIC_MOON_PAD[24] = {
    0x0384cad5u, 0x0385729bu, 0x042f4f0eu, 0x042ff6d3u, 0x0545d639u, 0x05467dfeu,
    0x02cadff8u, 0x02cb87beu, 0x0384cad5u, 0x0385729bu, 0x042f4f0eu, 0x042ff6d3u,
    0x02177857u, 0x0218201du, 0x02a2bbedu, 0x02a363b2u, 0x03227d03u, 0x032324c8u,
    0x03227d03u, 0x032324c8u, 0x03f32379u, 0x03f3cb3eu, 0x04b280f1u, 0x04b328b7u,
};
static const uint32_t MUSIC_MOON_MELODY[16] = {
    0x070a3d71u, 0x00000000u, 0x0645a1cbu, 0x05462a1bu,
    0x05967232u, 0x00000000u, 0x070a3d71u, 0x00000000u,
    0x05462a1bu, 0x04b2d4d4u, 0x00000000u, 0x042fa2f0u,
    0x04b2d4d4u, 0x00000000u, 0x0645a1cbu, 0x00000000u,
};
static const uint32_t MUSIC_RAIN_PAD[24] = {
    0x02ca8c15u, 0x02cbdba1u, 0x038476f3u, 0x0385c67eu, 0x042efb2bu, 0x04304ab6u,
    0x02a2680au, 0x02a3b795u, 0x03222920u, 0x032378abu, 0x042efb2bu, 0x04304ab6u,
    0x0258c2a4u, 0x025a1230u, 0x02ca8c15u, 0x02cbdba1u, 0x038476f3u, 0x0385c67eu,
    0x0258c2a4u, 0x025a1230u, 0x02ca8c15u, 0x02cbdba1u, 0x03ba0a52u, 0x03bb59deu,
};
static const uint32_t MUSIC_RAIN_MELODY[16] = {
    0x085f3b64u, 0x00000000u, 0x070a3d71u, 0x00000000u,
    0x0645a1cbu, 0x00000000u, 0x05462a1bu, 0x0645a1cbu,
    0x05967232u, 0x00000000u, 0x070a3d71u, 0x00000000u,
    0x07756430u, 0x05967232u, 0x00000000u, 0x04b2d4d4u,
};

typedef struct {
    const uint32_t *pad;
    const uint32_t *melody;
    uint32_t chord, overlap, note, attack;
    int32_t pad_gain, melody_gain;
} music_track_t;

static const music_track_t MUSIC_EXTRA_TRACKS[2] = {
    {MUSIC_MOON_PAD, MUSIC_MOON_MELODY, 160000u, 40000u, 40000u, 3200u, 360, 850},
    {MUSIC_RAIN_PAD, MUSIC_RAIN_MELODY, 96000u, 24000u, 24000u, 2400u, 380, 950},
};

static inline uint32_t music_track_loop(uint8_t track)
{
    return track == 0 ? MUSIC_LOOP : 4u * MUSIC_EXTRA_TRACKS[track - 1u].chord;
}

static inline int16_t music_gain(int32_t sample, uint8_t volume)
{
    return (int16_t)(sample * volume / 100);
}

static inline int32_t music_sine(uint32_t phase)
{
    uint32_t index = phase >> 24;
    int32_t frac = (int32_t)((phase >> 8) & 0xffffu);
    int32_t a = MUSIC_SINE[index], b = MUSIC_SINE[(index + 1u) & 255u];
    return a + (((b - a) * frac) >> 16);
}

static inline int32_t music_smooth(int32_t x)
{
    int32_t square = (x * x) >> 15;
    return (square * (49152 - x)) >> 14;
}

/* Soft attack, cubic decay to exactly zero at length, quieter octave partial. */
static inline int32_t music_bell(uint32_t inc, uint32_t local, uint32_t length,
                                 uint32_t attack, int32_t gain)
{
    if (!inc || local >= length) return 0;
    int32_t env;
    if (local < attack) env = (int32_t)(local * 32768u / attack);
    else {
        int32_t r = (int32_t)((length - local) * 32768u / (length - attack));
        env = (((r * r) >> 15) * r) >> 15;
    }
    uint32_t phase = inc * local;
    int32_t upper = (env * env) >> 15;
    return ((((music_sine(phase) * gain) >> 15) * env) >> 15) +
           ((((music_sine(phase << 1) * (gain >> 2)) >> 15) * upper) >> 15);
}

static inline int32_t music_pad(uint32_t chord, uint32_t local)
{
    int32_t sum = 0;
    for (uint32_t voice = 0; voice < 6; voice++)
        sum += music_sine(MUSIC_PAD[chord * 6u + voice] * local);
    int32_t env = 32768;
    if (local < MUSIC_OVERLAP) env = music_smooth((int32_t)(local * 32768u / MUSIC_OVERLAP));
    else if (local >= MUSIC_CHORD)
        env = 32768 - music_smooth((int32_t)((local - MUSIC_CHORD) * 32768u / MUSIC_OVERLAP));
    return (((sum * MUSIC_PAD_GAIN) >> 15) * env) >> 15;
}

static inline int16_t music_sample(uint32_t position)
{
    uint32_t p = position % MUSIC_LOOP, chord = p / MUSIC_CHORD, local = p - chord * MUSIC_CHORD;
    int32_t value = music_pad(chord, local);
    if (local < MUSIC_OVERLAP) value += music_pad((chord + 3u) & 3u, local + MUSIC_CHORD);
    uint32_t note = p / MUSIC_NOTE;
    value += music_bell(MUSIC_MELODY[note], p - note * MUSIC_NOTE, MUSIC_NOTE, 1280u,
                        MUSIC_MELODY_GAIN);
    return (int16_t)value;
}

static inline int32_t music_extra_pad(const music_track_t *track, uint8_t id,
                                      uint32_t chord, uint32_t local)
{
    int32_t sum = 0;
    for (uint32_t voice = 0; voice < 6; voice++) {
        uint32_t inc = track->pad[chord * 6u + voice];
        sum += music_sine(inc * local);
        /* Divide the increment, not a wrapped phase: the low voice must
         * remain continuous when the fundamental completes a cycle. */
        sum += music_sine(id == 1 ? (inc >> 1) * local : (inc * local) << 1) >> 3;
    }
    int32_t env = 32768;
    if (local < track->overlap)
        env = music_smooth((int32_t)(local * 32768u / track->overlap));
    else if (local >= track->chord)
        env = 32768 - music_smooth((int32_t)((local - track->chord) * 32768u / track->overlap));
    return (((sum * track->pad_gain) >> 15) * env) >> 15;
}

static inline int16_t music_track_sample(uint8_t id, uint32_t position)
{
    if (id == 0) return music_sample(position);
    const music_track_t *track = &MUSIC_EXTRA_TRACKS[id - 1u];
    uint32_t p = position % (4u * track->chord), chord = p / track->chord;
    uint32_t local = p - chord * track->chord;
    int32_t value = music_extra_pad(track, id, chord, local);
    if (local < track->overlap)
        value += music_extra_pad(track, id, (chord + 3u) & 3u, local + track->chord);
    value /= 4;
    if (id == 1) {
        uint32_t beat = local % 20000u;
        value += music_bell(track->pad[chord * 6u] >> 1, beat, 20000u, 240u, 1850);
        value += music_bell(track->melody[(p / 20000u) % 16u], beat, 16000u, 160u, 1500);
    } else {
        uint32_t beat = local % 6000u, voice = (local / 6000u) % 3u;
        value += music_bell(track->pad[chord * 6u + voice * 2u] * 2u, beat, 6000u, 96u, 2100);
        value += music_bell(track->melody[(p / 24000u) % 16u], local % 24000u, 18000u, 320u, 700);
    }
    return (int16_t)value;
}

static inline int16_t pet_sample(uint32_t i)
{
    int32_t value = music_bell(PET_NOTES[0], i, PET_SAMPLES, 160u, 1500);
    if (i >= PET_SECOND)
        value += music_bell(PET_NOTES[1], i - PET_SECOND, PET_SAMPLES - PET_SECOND, 160u, 1300);
    return (int16_t)value;
}
