#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COMPANION_DIARY_CAP 64
#define COMPANION_PIN_CAP 8
#define COMPANION_UNKNOWN 255

typedef struct {
    uint32_t day;
    uint8_t kind, value, pinned, calendar;
} companion_memory_t;

typedef struct {
    uint8_t kind, target, progress, state;
} companion_request_t;

/* Appended after the v14 fields. No pointers or runtime deadlines. */
typedef struct {
    uint32_t request_day;
    uint32_t seen_achievements;
    companion_request_t requests[2];
    uint8_t treat_counts[12], gift_counts[36], play_counts[3];
    uint8_t favorite_treat, favorite_gift, favorite_play;
    uint8_t diary_count, clock_mode, initialized, bond_level;
    uint8_t last_stage, was_sick;
    uint8_t reserved[2];
    companion_memory_t diary[COMPANION_DIARY_CAP];
} companion_state_t;

_Static_assert(sizeof(companion_state_t) == 592, "companion save ABI changed");

typedef enum {
    COMPANION_PET = 1, COMPANION_PLAY, COMPANION_HUNT,
    COMPANION_VISIT, COMPANION_TREAT, COMPANION_CLEAN
} companion_event_t;

void companion_defaults(companion_state_t *state);
bool companion_valid(const companion_state_t *state);
void companion_tick(void);
void companion_event(companion_event_t event, uint8_t value);
void companion_request_text(unsigned slot, char *title, size_t title_size,
                            char *detail, size_t detail_size);
void companion_memory_text(unsigned slot, char *title, size_t title_size,
                           char *detail, size_t detail_size);
bool companion_dismiss(unsigned slot);
bool companion_pin(unsigned slot);
bool companion_test(void);
