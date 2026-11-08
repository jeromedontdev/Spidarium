/*
 * Spidarium — needs, care loop and life cycle.
 *
 * Four stats with distinct decay curves, mood derived from them, stage
 * progression, molting, and the recoverable SICK state. The spider never dies.
 */
#pragma once

#include "game.h"

#include <stdbool.h>

/* Advance needs one logic tick. */
void needs_tick(uint32_t now_ms, float dt_s);

/* Care actions. Each applies its effect and awards XP. */
void needs_feed(void);
void needs_clean(void);
void needs_play(void);
void needs_sleep(void);


/* Recompute mood from the other stats. */
void needs_recompute_mood(void);

/* Recompute the life stage from age; triggers a molt when it changes. */
void needs_update_stage(void);

/* True if the spider is currently sick. */
bool needs_is_sick(void);

/* Human-readable stage name. */
const char *needs_stage_name(void);

/* Days lived, derived from age_ticks. */
uint32_t needs_days_lived(void);
bool needs_hunger_test(void);
