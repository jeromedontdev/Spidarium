#pragma once
#include "companion.h"
#include <stdbool.h>
#include <stdint.h>

#define LIFE_TRICKS 3
#define LIFE_RETIRED_CAP 3
#define LIFE_NEVER UINT32_MAX

typedef struct {
    uint8_t core[120], daily[24];
    companion_state_t companion;
    uint8_t progress[3], reserved;
} life_pet_t;

typedef struct {
    uint32_t visitor_day, season_highwater;
    uint8_t progress[3], cooldown, daynight, weather, retired_count, light_gesture_disabled;
    life_pet_t retired[LIFE_RETIRED_CAP];
} life_state_t;

typedef struct {
    bool available, visitor;
    uint8_t period, season, activity; /* 0..3 periods; activity 0=none,1..3 */
    uint32_t day, season_key;
} life_clock_t;

_Static_assert(sizeof(life_pet_t) == 740, "life pet ABI changed");
_Static_assert(sizeof(life_state_t) == 2236, "life ABI changed");
_Static_assert(offsetof(life_state_t, progress) == 8, "life progress ABI changed");
_Static_assert(offsetof(life_state_t, light_gesture_disabled) == 15, "light gesture ABI changed");
_Static_assert(offsetof(life_state_t, retired) == 16, "life archives ABI changed");
_Static_assert(offsetof(life_pet_t, companion) == 144, "archive companion ABI changed");
_Static_assert(offsetof(life_pet_t, progress) == 736, "archive progress ABI changed");

void life_defaults(life_state_t *state);
bool life_valid(const life_state_t *state);
life_clock_t life_clock(uint32_t epoch);
/* The current day period (0 morning, 1 afternoon, 2 evening, 3 night), or 255
 * when the clock is unavailable. The spider's mind uses this so its behaviour
 * follows the real day rather than only the stat thresholds. */
uint8_t life_period_now(void);
void life_tick(void);
void life_tick_active(uint32_t now_ms, bool active);
const char *life_trick_name(unsigned trick);
const char *life_phase_name(unsigned phase);
const char *life_season_name(unsigned season);
const char *life_activity_name(unsigned activity);
bool life_visitor_available(void);
bool life_train(unsigned trick);
bool life_perform(unsigned trick);
bool life_claim_visitor(void);
bool life_claim_season(void);
bool life_retire_confirmed(void);
bool life_resume(unsigned slot);
/* -1 waiting/no operation; 0 failed commit; 1 durable successful snapshot. */
int life_generation_save_status(void);
bool life_test(void);
