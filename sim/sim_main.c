/*
 * Spidarium — headless PC simulator.
 *
 * Runs the REAL game logic (behaviour, prey, needs, mind, gift, species, theme)
 * on the host at a fixed timestep, with no display. The point is troubleshooting:
 * you can run a simulated week in seconds, watch the state machine, and catch
 * the kind of deadlock that takes 90 minutes to reproduce on hardware.
 *
 * This is deliberately NOT a graphical simulator. A window would need SDL2 plus
 * a full LVGL host build, and the thing that actually goes wrong in this project
 * is logic (stuck states, starving spiders, prey that never gets caught), not
 * pixels. The harness therefore stubs the handful of LVGL calls the logic
 * modules make and drives the tick loop directly.
 *
 * Build:  see sim/build_sim.ps1
 * Run:    sim/spidarium-sim.exe [--minutes N] [--seed S] [--verbose]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include "game.h"
#include "game_config.h"
#include "behaviour.h"
#include "prey.h"
#include "needs.h"
#include "mind.h"
#include "gift.h"
#include "species.h"
#include "theme.h"

/* ------------------------------------------------------------------ */
/* Stubs                                                               */
/* ------------------------------------------------------------------ */
/* The logic modules call a small set of LVGL and hardware functions. On the
 * host these are no-ops: the simulator cares about state, not pixels. */

/* The two globals the whole game shares. game.c owns them on the device; the
 * simulator owns them here so the logic modules link. */
game_state_t g;
playfield_t  g_play;

/* Temperament table. game.c normally provides this; the simulator needs its own
 * copy so behaviour.c and needs.c can read the multipliers. */
const temperament_t k_temperaments[TEMPERAMENT_COUNT] = {
    { "bold",   1.00f, 1.00f, 1.00f, 1.00f, 20 },
    { "timid",  1.15f, 0.85f, 1.00f, 1.00f, 30 },
    { "greedy", 0.90f, 1.15f, 1.00f, 1.00f, 25 },
    { "lazy",   1.00f, 0.90f, 1.00f, 1.00f, 15 },
};

const temperament_t *game_temperament(void)
{
    uint8_t i = g.temperament;
    if (i >= TEMPERAMENT_COUNT) i = 0;
    return &k_temperaments[i];
}

uint32_t lv_tick_get(void);

static uint32_t s_now_ms;

uint32_t lv_tick_get(void)
{
    return s_now_ms;
}

/* Audio: silent. */
void audio_play(int sfx) { (void)sfx; }
bool audio_muted(void) { return true; }
void audio_set_muted(bool m) { (void)m; }
void audio_init(void) {}

/* Terrarium: the logic only needs the poop count and the add/clear calls. */
static int s_poop;

void terrarium_add_poop(int32_t x, int32_t y) { (void)x; (void)y; s_poop++; }
void terrarium_clear_poop(void) { s_poop = 0; }
int  terrarium_poop_count(void) { return s_poop; }
void terrarium_rebuild(void) {}
void terrarium_refresh_gift(void) {}
void terrarium_tick(uint32_t now) { (void)now; }
void terrarium_init(void *parent) { (void)parent; }

/* Spider: the logic drives it through these. The simulator tracks the ones
 * that matter for behaviour (position, heading, one-shot animation). */
static bool s_oneshot;

void spider_launch_pounce(int32_t dist, uint32_t ms) { (void)dist; (void)ms; s_oneshot = true; }
void spider_play_once(int anim, bool once) { (void)anim; (void)once; s_oneshot = true; }
bool spider_oneshot_active(void) { return s_oneshot; }
void spider_set_emote(int e) { (void)e; }
void spider_refresh_colours(void) {}
void spider_render(uint32_t now) { (void)now; s_oneshot = false; }
void spider_init(void *parent) { (void)parent; }

/* Game: XP and achievements are bookkeeping the simulator reports on. */
static uint32_t s_xp;
static uint32_t s_ach;

void game_add_xp(uint32_t xp) { s_xp += xp; }
void game_award(uint32_t bit) { s_ach |= bit; }
void game_save_now(void) {}

/* ------------------------------------------------------------------ */
/* Harness                                                             */
/* ------------------------------------------------------------------ */
#define LOGIC_HZ 20
#define DT_S     (1.0f / (float)LOGIC_HZ)

static const char *state_name(uint8_t s)
{
    switch (s) {
    case SPIDER_IDLE:   return "IDLE";
    case SPIDER_WANDER: return "WANDER";
    case SPIDER_STALK:  return "STALK";
    case SPIDER_AIM:    return "AIM";
    case SPIDER_POUNCE: return "POUNCE";
    case SPIDER_EAT:    return "EAT";
    case SPIDER_SLEEP:  return "SLEEP";
    case SPIDER_PLAY:   return "PLAY";
    case SPIDER_SICK:   return "SICK";
    default:            return "?";
    }
}

/* Track how long the spider has been in its current state, so a stuck state is
 * obvious in the report rather than something you have to infer. */
static uint8_t  s_last_state;
static uint32_t s_state_since;
static uint32_t s_longest_state_ms;
static uint8_t  s_longest_state;

static void track_state(uint32_t now)
{
    if (g.state != s_last_state) {
        uint32_t held = now - s_state_since;
        if (held > s_longest_state_ms) {
            s_longest_state_ms = held;
            s_longest_state    = s_last_state;
        }
        s_last_state   = g.state;
        s_state_since  = now;
    }
}

int main(int argc, char **argv)
{
    int  minutes = 30;
    unsigned seed = 12345;
    bool verbose = false;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--minutes") && i + 1 < argc) {
            minutes = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            seed = (unsigned)strtoul(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--verbose")) {
            verbose = true;
        } else if (!strcmp(argv[i], "--help")) {
            printf("usage: spidarium-sim [--minutes N] [--seed S] [--verbose]\n");
            return 0;
        }
    }

    srand(seed);

    /* Fresh state, as a new tank would be. */
    memset(&g, 0, sizeof g);
    g.hunger  = 65;
    g.mood    = 60;
    g.energy  = 80;
    g.hygiene = 80;
    g.seed    = (uint32_t)seed;
    g.state   = SPIDER_IDLE;
    g.stage   = STAGE_SPIDERLING;
    g.level   = 1;
    g.temperament = (uint8_t)(rand() % TEMPERAMENT_COUNT);
    g.colour      = 0;
    g.pupil_colour = 0;
    g.leg_colour   = 2;
    g.fang_colour  = 1;
    strcpy(g.name, "sim");

    g.spider_x = 240;
    g.spider_y = 240;
    g.spider_heading = 0.0f;

    g_play.left   = PLAY_MARGIN;
    g_play.right  = 480 - PLAY_MARGIN;
    g_play.top    = PLAY_MARGIN;
    g_play.bottom = 480 - PLAY_MARGIN;

    s_last_state  = g.state;
    s_state_since = 0;

    printf("spidarium simulator\n");
    printf("  seed      %u\n", seed);
    printf("  duration  %d min\n", minutes);
    printf("  temper    %s\n", game_temperament()->name);
    printf("\n");

    const uint32_t total_ticks = (uint32_t)minutes * 60u * LOGIC_HZ;
    uint32_t next_report = 0;
    uint32_t feeds = 0;
    uint32_t cleans = 0;

    for (uint32_t t = 0; t < total_ticks; t++) {
        s_now_ms = t * (1000u / LOGIC_HZ);

        /* The player feeds when hunger drops, which is what a real owner does.
         * Without this the spider starves and the run tells you nothing about
         * hunting. */
        if (g.hunger < 40 && prey_count() == 0) {
            needs_feed();
            feeds++;
        }

        /* And cleans when the tank gets dirty. Without this the spider always
         * ends up SICK with hygiene 0, which says nothing about the hunting
         * loop -- it just says the simulated owner was negligent. */
        if (g.hygiene < 30) {
            needs_clean();
            cleans++;
        }

        /* Drive the same order as game_tick_logic(). */
        mind_tick(s_now_ms);
        gift_compute();
        needs_tick(s_now_ms, DT_S);
        behaviour_tick(s_now_ms, DT_S);
        prey_tick(s_now_ms, DT_S);

        track_state(s_now_ms);

        if (verbose && (t % (LOGIC_HZ * 10u)) == 0) {
            printf("t=%6us %-7s pos=(%3d,%3d) h=%3u m=%3u e=%3u hy=%3u prey=%d lvl=%lu\n",
                   (unsigned)(s_now_ms / 1000u), state_name(g.state),
                   g.spider_x, g.spider_y,
                   g.hunger, g.mood, g.energy, g.hygiene,
                   prey_count(), (unsigned long)g.level);
        }

        /* Periodic summary. */
        if (s_now_ms >= next_report) {
            next_report = s_now_ms + 60000u;
            if (!verbose) {
                printf("%3u min  %-7s h=%3u m=%3u e=%3u hy=%3u prey=%d lvl=%lu\n",
                       (unsigned)(s_now_ms / 60000u), state_name(g.state),
                       g.hunger, g.mood, g.energy, g.hygiene,
                       prey_count(), (unsigned long)g.level);
            }
        }
    }

    /* Final report. */
    printf("\n--- result ---\n");
    printf("  final state   %s\n", state_name(g.state));
    printf("  hunger        %u\n", g.hunger);
    printf("  mood          %u\n", g.mood);
    printf("  energy        %u\n", g.energy);
    printf("  hygiene       %u\n", g.hygiene);
    printf("  level         %lu\n", (unsigned long)g.level);
    printf("  meals         %lu\n", (unsigned long)g.meals);
    printf("  feeds given   %u\n", feeds);
    printf("  cleans given  %u\n", cleans);
    printf("  xp            %lu\n", (unsigned long)s_xp);
    printf("  achievements  0x%08lx\n", (unsigned long)s_ach);
    printf("  longest state %s for %lu s\n",
           state_name(s_longest_state),
           (unsigned long)(s_longest_state_ms / 1000u));

    /* A stuck state is the failure this tool exists to catch.
     *
     * Only TRANSIENT states count. WANDER, IDLE and SLEEP are legitimately
     * long-lived -- a spider with no prey wanders for hours, and that is
     * correct. AIM, POUNCE, EAT and PLAY should each resolve in seconds, so
     * one of those holding for minutes is a real deadlock. */
    bool transient = (s_longest_state == SPIDER_AIM ||
                      s_longest_state == SPIDER_POUNCE ||
                      s_longest_state == SPIDER_EAT ||
                      s_longest_state == SPIDER_PLAY);

    if (transient && s_longest_state_ms > 30000u) {
        printf("\n  WARNING: %s held for %lu s -- possible deadlock\n",
               state_name(s_longest_state),
               (unsigned long)(s_longest_state_ms / 1000u));
        return 1;
    }

    printf("\n  OK\n");
    return 0;
}
