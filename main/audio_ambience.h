#pragma once

#include <stdint.h>

#define AMBIENCE_RATE 16000
#define AMBIENCE_CHUNK 1600
#define AMBIENCE_FADE 320

typedef struct {
    uint32_t seed;
    uint32_t position;
    uint32_t length;
    int32_t air;
    int32_t floor;
    int leafy;
} ambience_t;

static inline void ambience_init(ambience_t *sound, int leafy, uint32_t seed)
{
    *sound = (ambience_t){
        .seed = seed, .length = AMBIENCE_RATE * (leafy ? 10u : 8u), .leafy = leafy
    };
}

static inline double ambience_smooth(double value)
{
    return value * value * (3.0 - 2.0 * value);
}

static inline int16_t ambience_sample(ambience_t *sound)
{
    uint32_t i = sound->position++;
    sound->seed = sound->seed * 1664525u + 1013904223u;
    int32_t noise = (int32_t)(sound->seed >> 16) - 32768;
    sound->air += (noise - sound->air) / (sound->leafy ? 12 : 32);
    sound->floor += (noise - sound->floor) / 512;
    double attack = (double)i / (AMBIENCE_RATE * 2);
    double release = (double)(sound->length - 1 - i) / (AMBIENCE_RATE * 3);
    double envelope = attack < release ? attack : release;
    if (envelope > 1.0) envelope = 1.0;
    uint32_t cycle = (i + (sound->leafy ? 17000u : 0u)) % 112000u;
    double swell = cycle < 56000 ? (double)cycle / 56000 : (112000.0 - cycle) / 56000;
    double gain = (sound->leafy ? 0.095 : 0.14) * (0.65 + 0.35 * ambience_smooth(swell));
    return (int16_t)((sound->air - sound->floor) * gain * ambience_smooth(envelope));
}
