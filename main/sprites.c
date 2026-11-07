/*
 * Spidarium — sprite table implementation.
 */
#include "sprites.h"

#include "mmap_generate_storage.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265f

/* Frame counts per clip, matching the asset specification. */
static const int k_frames[ANIM_COUNT] = {
    [ANIM_WALK]   = 4,
    [ANIM_IDLE]   = 2,
    [ANIM_EAT]    = 2,
    [ANIM_SLEEP]  = 1,
    [ANIM_POUNCE] = 2,
    [ANIM_MOLT]   = 2,
};

static const uint32_t k_frame_ms[ANIM_COUNT] = {
    [ANIM_WALK]   = 110,
    [ANIM_IDLE]   = 900,
    [ANIM_EAT]    = 180,
    [ANIM_SLEEP]  = 0,
    [ANIM_POUNCE] = 90,
    [ANIM_MOLT]   = 400,
};

static const bool k_loops[ANIM_COUNT] = {
    [ANIM_WALK]   = true,
    [ANIM_IDLE]   = true,
    [ANIM_EAT]    = true,
    [ANIM_SLEEP]  = false,
    [ANIM_POUNCE] = false,
    [ANIM_MOLT]   = false,
};

/* Base index of each (view, anim) block in the generated enum. The generator
 * sorts filenames alphabetically, so each view's clips are contiguous. */
static const int k_spider_base[3][ANIM_COUNT] = {
    /* VIEW_N */
    [VIEW_N] = {
        [ANIM_WALK]   = MMAP_STORAGE_SPIDER_N_WALK_0_QOI,
        [ANIM_IDLE]   = MMAP_STORAGE_SPIDER_N_IDLE_0_QOI,
        [ANIM_EAT]    = MMAP_STORAGE_SPIDER_N_EAT_0_QOI,
        [ANIM_SLEEP]  = MMAP_STORAGE_SPIDER_N_SLEEP_0_QOI,
        [ANIM_POUNCE] = MMAP_STORAGE_SPIDER_N_POUNCE_0_QOI,
        [ANIM_MOLT]   = MMAP_STORAGE_SPIDER_N_MOLT_0_QOI,
    },
    /* VIEW_E */
    [VIEW_E] = {
        [ANIM_WALK]   = MMAP_STORAGE_SPIDER_E_WALK_0_QOI,
        [ANIM_IDLE]   = MMAP_STORAGE_SPIDER_E_IDLE_0_QOI,
        [ANIM_EAT]    = MMAP_STORAGE_SPIDER_E_EAT_0_QOI,
        [ANIM_SLEEP]  = MMAP_STORAGE_SPIDER_E_SLEEP_0_QOI,
        [ANIM_POUNCE] = MMAP_STORAGE_SPIDER_E_POUNCE_0_QOI,
        [ANIM_MOLT]   = MMAP_STORAGE_SPIDER_E_MOLT_0_QOI,
    },
    /* VIEW_S */
    [VIEW_S] = {
        [ANIM_WALK]   = MMAP_STORAGE_SPIDER_S_WALK_0_QOI,
        [ANIM_IDLE]   = MMAP_STORAGE_SPIDER_S_IDLE_0_QOI,
        [ANIM_EAT]    = MMAP_STORAGE_SPIDER_S_EAT_0_QOI,
        [ANIM_SLEEP]  = MMAP_STORAGE_SPIDER_S_SLEEP_0_QOI,
        [ANIM_POUNCE] = MMAP_STORAGE_SPIDER_S_POUNCE_0_QOI,
        [ANIM_MOLT]   = MMAP_STORAGE_SPIDER_S_MOLT_0_QOI,
    },
};

static const int k_prey_base[PREY_SPECIES_COUNT][3] = {
    /* walk, flee, dead */
    [PREY_FLY]     = { MMAP_STORAGE_PREY_FLY_WALK_0_QOI,
                       MMAP_STORAGE_PREY_FLY_FLEE_0_QOI,
                       MMAP_STORAGE_PREY_FLY_DEAD_0_QOI },
    [PREY_CRICKET] = { MMAP_STORAGE_PREY_CRICKET_WALK_0_QOI,
                       MMAP_STORAGE_PREY_CRICKET_FLEE_0_QOI,
                       MMAP_STORAGE_PREY_CRICKET_DEAD_0_QOI },
    [PREY_MOTH]    = { MMAP_STORAGE_PREY_MOTH_WALK_0_QOI,
                       MMAP_STORAGE_PREY_MOTH_FLEE_0_QOI,
                       MMAP_STORAGE_PREY_MOTH_DEAD_0_QOI },
};

typedef struct {
    const char *name;
    int index;
} icon_entry_t;

static const icon_entry_t k_icons[] = {
    { "feed",    MMAP_STORAGE_UI_FEED_QOI    },
    { "clean",   MMAP_STORAGE_UI_CLEAN_QOI   },
    { "play",    MMAP_STORAGE_UI_PLAY_QOI    },
    { "sleep",   MMAP_STORAGE_UI_SLEEP_QOI   },
    { "decor",   MMAP_STORAGE_UI_DECOR_QOI   },
    { "status",  MMAP_STORAGE_UI_STATUS_QOI  },
    { "back",    MMAP_STORAGE_UI_BACK_QOI    },
    { "hunger",  MMAP_STORAGE_UI_HUNGER_QOI  },
    { "mood",    MMAP_STORAGE_UI_MOOD_QOI    },
    { "energy",  MMAP_STORAGE_UI_ENERGY_QOI  },
    { "hygiene", MMAP_STORAGE_UI_HYGIENE_QOI },
    { "xp",      MMAP_STORAGE_UI_XP_QOI      },
    { "lock",    MMAP_STORAGE_UI_LOCK_QOI    },
};

sprite_view_t sprite_view_for_heading(float heading_rad)
{
    /* Normalise to [0, 2pi). */
    float a = fmodf(heading_rad, 2.0f * PI_F);
    if (a < 0.0f) a += 2.0f * PI_F;

    /* Screen space: +x right, +y down. Heading 0 = east. */
    if (a < PI_F * 0.25f || a >= PI_F * 1.75f) return VIEW_E;
    if (a < PI_F * 0.75f) return VIEW_S;
    if (a < PI_F * 1.25f) return VIEW_W;
    return VIEW_N;
}

int sprite_frame_count(spider_anim_t anim)
{
    if (anim < 0 || anim >= ANIM_COUNT) return 1;
    return k_frames[anim];
}

uint32_t sprite_frame_ms(spider_anim_t anim)
{
    if (anim < 0 || anim >= ANIM_COUNT) return 0;
    return k_frame_ms[anim];
}

bool sprite_anim_loops(spider_anim_t anim)
{
    if (anim < 0 || anim >= ANIM_COUNT) return false;
    return k_loops[anim];
}

int sprite_spider_index(sprite_view_t view, spider_anim_t anim, int frame)
{
    if (anim < 0 || anim >= ANIM_COUNT) return -1;

    /* West is east mirrored; the caller flips the object. */
    int v = (view == VIEW_W) ? VIEW_E : (int)view;
    if (v < 0 || v > 2) return -1;

    int n = k_frames[anim];
    if (n <= 0) return -1;
    if (frame < 0) frame = 0;
    if (frame >= n) frame = n - 1;

    return k_spider_base[v][anim] + frame;
}

int sprite_prey_index(prey_species_t species, bool fleeing, bool dead, int frame)
{
    if (species < 0 || species >= PREY_SPECIES_COUNT) return -1;

    int col = dead ? 2 : (fleeing ? 1 : 0);
    int base = k_prey_base[species][col];

    if (dead) return base;

    if (frame < 0) frame = 0;
    if (frame > 1) frame = 1;
    return base + frame;
}

int sprite_icon_index(const char *name)
{
    if (!name) return -1;
    for (size_t i = 0; i < sizeof k_icons / sizeof k_icons[0]; i++) {
        if (strcmp(k_icons[i].name, name) == 0) return k_icons[i].index;
    }
    return -1;
}
