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

typedef struct {
    const char *name;
    int index;
} icon_entry_t;

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

/* Frame indices of all currently supported prey species. */
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
    [PREY_BEETLE]  = { MMAP_STORAGE_PREY_BEETLE_WALK_0_QOI,
                       MMAP_STORAGE_PREY_BEETLE_FLEE_0_QOI,
                       MMAP_STORAGE_PREY_BEETLE_DEAD_0_QOI },
    [PREY_ROACH]   = { MMAP_STORAGE_PREY_ROACH_WALK_0_QOI,
                       MMAP_STORAGE_PREY_ROACH_FLEE_0_QOI,
                       MMAP_STORAGE_PREY_ROACH_DEAD_0_QOI },
    [PREY_MEALWORM]= { MMAP_STORAGE_PREY_MEALWORM_WALK_0_QOI,
                       MMAP_STORAGE_PREY_MEALWORM_FLEE_0_QOI,
                       MMAP_STORAGE_PREY_MEALWORM_DEAD_0_QOI },
    [PREY_FRUITFLY]= { MMAP_STORAGE_PREY_FRUITFLY_WALK_0_QOI,
                       MMAP_STORAGE_PREY_FRUITFLY_FLEE_0_QOI,
                       MMAP_STORAGE_PREY_FRUITFLY_DEAD_0_QOI },
    [PREY_WAXWORM] = { MMAP_STORAGE_PREY_WAXWORM_WALK_0_QOI,
                       MMAP_STORAGE_PREY_WAXWORM_FLEE_0_QOI,
                       MMAP_STORAGE_PREY_WAXWORM_DEAD_0_QOI },
};

static const int k_gift_index[GIFT_KINDS] = {
    MMAP_STORAGE_GIFT_PEBBLE_QOI, MMAP_STORAGE_GIFT_LEAF_QOI,
    MMAP_STORAGE_GIFT_TWIG_QOI, MMAP_STORAGE_GIFT_ACORN_QOI,
    MMAP_STORAGE_GIFT_MOSS_QOI, MMAP_STORAGE_GIFT_SAP_QOI,
    MMAP_STORAGE_GIFT_FLOWER_QOI, MMAP_STORAGE_GIFT_CRYSTAL_QOI,
    MMAP_STORAGE_GIFT_AMBER_QOI, MMAP_STORAGE_GIFT_CORAL_QOI,
    MMAP_STORAGE_GIFT_SILK_QOI, MMAP_STORAGE_GIFT_BONE_QOI,
    MMAP_STORAGE_GIFT_PEARL_QOI, MMAP_STORAGE_GIFT_RUBY_QOI,
    MMAP_STORAGE_GIFT_SAPPHIRE_QOI, MMAP_STORAGE_GIFT_EMERALD_QOI,
    MMAP_STORAGE_GIFT_DIAMOND_QOI, MMAP_STORAGE_GIFT_CROWN_QOI,
    MMAP_STORAGE_GIFT_FAIRY_COTTAGE_QOI, MMAP_STORAGE_GIFT_MOON_BED_QOI,
    MMAP_STORAGE_GIFT_READING_NOOK_QOI, MMAP_STORAGE_GIFT_TEAPOT_HIDE_QOI,
    MMAP_STORAGE_GIFT_WITCH_HAT_QOI, MMAP_STORAGE_GIFT_FERN_NEST_QOI,
    MMAP_STORAGE_GIFT_NEON_DEN_QOI, MMAP_STORAGE_GIFT_STAR_CAPSULE_QOI,
    MMAP_STORAGE_GIFT_DRAGON_ROOST_QOI, MMAP_STORAGE_GIFT_SUN_TEMPLE_QOI,
    MMAP_STORAGE_GIFT_ZEN_PAVILION_QOI, MMAP_STORAGE_GIFT_PETAL_SWING_QOI,
    MMAP_STORAGE_GIFT_VELVET_THRONE_QOI, MMAP_STORAGE_GIFT_SCHOLAR_PERCH_QOI,
    MMAP_STORAGE_GIFT_POTION_HIDE_QOI, MMAP_STORAGE_GIFT_FOSSIL_CRADLE_QOI,
    MMAP_STORAGE_GIFT_HOLO_GARDEN_QOI, MMAP_STORAGE_GIFT_TORII_RETREAT_QOI,
};

static const icon_entry_t k_decor[] = {
    { "rock_0",   MMAP_STORAGE_DECOR_ROCK_0_QOI },
    { "rock_1",   MMAP_STORAGE_DECOR_ROCK_1_QOI },
    { "plant_0",  MMAP_STORAGE_DECOR_PLANT_0_QOI },
    { "plant_1",  MMAP_STORAGE_DECOR_PLANT_1_QOI },
    { "wood_log", MMAP_STORAGE_DECOR_WOOD_LOG_QOI },
};

/* Placeable decor, in decor_kind_t order. */
static const icon_entry_t k_decor_items[] = {
    { "rock_small",   MMAP_STORAGE_DECORITEM_ROCK_SMALL_QOI   },
    { "rock_large",   MMAP_STORAGE_DECORITEM_ROCK_LARGE_QOI   },
    { "pebbles",      MMAP_STORAGE_DECORITEM_PEBBLES_QOI      },
    { "log",          MMAP_STORAGE_DECORITEM_LOG_QOI          },
    { "bark",         MMAP_STORAGE_DECORITEM_BARK_QOI         },
    { "fern",         MMAP_STORAGE_DECORITEM_FERN_QOI         },
    { "moss_clump",   MMAP_STORAGE_DECORITEM_MOSS_CLUMP_QOI   },
    { "succulent",    MMAP_STORAGE_DECORITEM_SUCCULENT_QOI    },
    { "flower_white", MMAP_STORAGE_DECORITEM_FLOWER_WHITE_QOI },
    { "flower_pink",  MMAP_STORAGE_DECORITEM_FLOWER_PINK_QOI  },
    { "mushroom",     MMAP_STORAGE_DECORITEM_MUSHROOM_QOI     },
    { "shell",        MMAP_STORAGE_DECORITEM_SHELL_QOI        },
    { "crystal",      MMAP_STORAGE_DECORITEM_CRYSTAL_QOI      },
    { "leaf_pile",    MMAP_STORAGE_DECORITEM_LEAF_PILE_QOI    },
    { "twig",         MMAP_STORAGE_DECORITEM_TWIG_QOI         },
    { "bone",         MMAP_STORAGE_DECORITEM_BONE_QOI         },
    { "fern_pot",      MMAP_STORAGE_DECORITEM_FERN_POT_QOI      },
    { "acorn_hide",    MMAP_STORAGE_DECORITEM_ACORN_HIDE_QOI    },
    { "driftwood_arch",MMAP_STORAGE_DECORITEM_DRIFTWOOD_ARCH_QOI},
    { "web_hammock",   MMAP_STORAGE_DECORITEM_WEB_HAMMOCK_QOI   },
    { "dew_pool",      MMAP_STORAGE_DECORITEM_DEW_POOL_QOI      },
    { "mushroom_pair", MMAP_STORAGE_DECORITEM_MUSHROOM_PAIR_QOI },
    { "flower_planter",MMAP_STORAGE_DECORITEM_FLOWER_PLANTER_QOI},
    { "crystal_cluster",MMAP_STORAGE_DECORITEM_CRYSTAL_CLUSTER_QOI},
    { "shell_cove",    MMAP_STORAGE_DECORITEM_SHELL_COVE_QOI    },
    { "twig_ladder",   MMAP_STORAGE_DECORITEM_TWIG_LADDER_QOI   },
    { "moss_stone",    MMAP_STORAGE_DECORITEM_MOSS_STONE_QOI    },
    { "seed_pod",      MMAP_STORAGE_DECORITEM_SEED_POD_QOI      },
    { "silk_tent",     MMAP_STORAGE_DECORITEM_SILK_TENT_QOI     },
    { "leaf_umbrella", MMAP_STORAGE_DECORITEM_LEAF_UMBRELLA_QOI },
    { "bone_arch",     MMAP_STORAGE_DECORITEM_BONE_ARCH_QOI     },
    { "fossil",        MMAP_STORAGE_DECORITEM_FOSSIL_QOI        },
    { "lantern_cap",   MMAP_STORAGE_DECORITEM_LANTERN_CAP_QOI   },
    { "stone_bridge",  MMAP_STORAGE_DECORITEM_STONE_BRIDGE_QOI  },
    { "fairy_ring", MMAP_STORAGE_DECORITEM_FAIRY_RING_QOI },
    { "foxglove_lantern", MMAP_STORAGE_DECORITEM_FOXGLOVE_LANTERN_QOI },
    { "pixie_door", MMAP_STORAGE_DECORITEM_PIXIE_DOOR_QOI },
    { "gargoyle", MMAP_STORAGE_DECORITEM_GARGOYLE_QOI },
    { "iron_gate", MMAP_STORAGE_DECORITEM_IRON_GATE_QOI },
    { "rose_urn", MMAP_STORAGE_DECORITEM_ROSE_URN_QOI },
    { "book_stack", MMAP_STORAGE_DECORITEM_BOOK_STACK_QOI },
    { "globe", MMAP_STORAGE_DECORITEM_GLOBE_QOI },
    { "quill_desk", MMAP_STORAGE_DECORITEM_QUILL_DESK_QOI },
    { "tea_cup", MMAP_STORAGE_DECORITEM_TEA_CUP_QOI },
    { "spool", MMAP_STORAGE_DECORITEM_SPOOL_QOI },
    { "thimble", MMAP_STORAGE_DECORITEM_THIMBLE_QOI },
    { "cauldron", MMAP_STORAGE_DECORITEM_CAULDRON_QOI },
    { "broom", MMAP_STORAGE_DECORITEM_BROOM_QOI },
    { "potion_rack", MMAP_STORAGE_DECORITEM_POTION_RACK_QOI },
    { "amber_fossil", MMAP_STORAGE_DECORITEM_AMBER_FOSSIL_QOI },
    { "cycad", MMAP_STORAGE_DECORITEM_CYCAD_QOI },
    { "dinosaur_egg", MMAP_STORAGE_DECORITEM_DINOSAUR_EGG_QOI },
    { "neon_sign", MMAP_STORAGE_DECORITEM_NEON_SIGN_QOI },
    { "holo_terminal", MMAP_STORAGE_DECORITEM_HOLO_TERMINAL_QOI },
    { "cable_coil", MMAP_STORAGE_DECORITEM_CABLE_COIL_QOI },
    { "moon_rover", MMAP_STORAGE_DECORITEM_MOON_ROVER_QOI },
    { "solar_array", MMAP_STORAGE_DECORITEM_SOLAR_ARRAY_QOI },
    { "oxygen_pod", MMAP_STORAGE_DECORITEM_OXYGEN_POD_QOI },
    { "wizard_tower", MMAP_STORAGE_DECORITEM_WIZARD_TOWER_QOI },
    { "dragon_statue", MMAP_STORAGE_DECORITEM_DRAGON_STATUE_QOI },
    { "treasure_chest", MMAP_STORAGE_DECORITEM_TREASURE_CHEST_QOI },
    { "sun_obelisk", MMAP_STORAGE_DECORITEM_SUN_OBELISK_QOI },
    { "glyph_tablet", MMAP_STORAGE_DECORITEM_GLYPH_TABLET_QOI },
    { "broken_column", MMAP_STORAGE_DECORITEM_BROKEN_COLUMN_QOI },
    { "stone_lantern", MMAP_STORAGE_DECORITEM_STONE_LANTERN_QOI },
    { "bamboo_fountain", MMAP_STORAGE_DECORITEM_BAMBOO_FOUNTAIN_QOI },
    { "bonsai", MMAP_STORAGE_DECORITEM_BONSAI_QOI },
    { "fairy_bridge", MMAP_STORAGE_DECORITEM_FAIRY_BRIDGE_QOI },
};

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
    { "daily",        MMAP_STORAGE_UI_DAILY_QOI        },
    { "treats",       MMAP_STORAGE_UI_TREATS_QOI       },
    { "backgrounds", MMAP_STORAGE_UI_BACKGROUNDS_QOI  },
    { "achievements",MMAP_STORAGE_UI_ACHIEVEMENTS_QOI },
    { "prey",         MMAP_STORAGE_UI_PREY_QOI         },
    { "settings",     MMAP_STORAGE_UI_SETTINGS_QOI     },
    { "wifi",         MMAP_STORAGE_UI_WIFI_QOI         },
    { "sd",           MMAP_STORAGE_UI_SD_QOI           },
    { "about",        MMAP_STORAGE_UI_ABOUT_QOI        },
    { "battery",      MMAP_STORAGE_UI_BATTERY_QOI      },
    { "display",      MMAP_STORAGE_UI_DISPLAY_QOI      },
    { "sound",        MMAP_STORAGE_UI_SOUND_QOI        },
    { "auto_feed",    MMAP_STORAGE_UI_AUTO_FEED_QOI    },
    { "screenshot",   MMAP_STORAGE_UI_SCREENSHOT_QOI   },
    { "reset",        MMAP_STORAGE_UI_RESET_QOI        },
    { "volume",       MMAP_STORAGE_UI_VOLUME_QOI       },
    { "music",        MMAP_STORAGE_UI_MUSIC_QOI        },
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

int sprite_achievement_index(uint32_t bit)
{
    switch (bit) {
    case ACH_FIRST_MEAL:    return MMAP_STORAGE_ACHIEVEMENT_FIRST_MEAL_QOI;
    case ACH_FIRST_PET:     return MMAP_STORAGE_ACHIEVEMENT_FIRST_PET_QOI;
    case ACH_FIRST_CLEAN:   return MMAP_STORAGE_ACHIEVEMENT_FIRST_CLEAN_QOI;
    case ACH_FIRST_PLAY:    return MMAP_STORAGE_ACHIEVEMENT_FIRST_PLAY_QOI;
    case ACH_FIRST_HUNT:    return MMAP_STORAGE_ACHIEVEMENT_FIRST_HUNT_QOI;
    case ACH_FIRST_MOLT:    return MMAP_STORAGE_ACHIEVEMENT_FIRST_MOLT_QOI;
    case ACH_TEN_MEALS:     return MMAP_STORAGE_ACHIEVEMENT_TEN_MEALS_QOI;
    case ACH_ADULT:         return MMAP_STORAGE_ACHIEVEMENT_ADULT_QOI;
    case ACH_LEVEL_5:       return MMAP_STORAGE_ACHIEVEMENT_LEVEL_5_QOI;
    case ACH_SURVIVED_SICK: return MMAP_STORAGE_ACHIEVEMENT_SURVIVED_SICK_QOI;
    case ACH_FIRST_PERFECT: return MMAP_STORAGE_ACHIEVEMENT_FIRST_PERFECT_QOI;
    default:               return -1;
    }
}

int sprite_gift_index(uint8_t kind)
{
    return kind < GIFT_KINDS ? k_gift_index[kind] : -1;
}

int sprite_treat_index(uint8_t kind)
{
    static const int treats[] = {
        MMAP_STORAGE_TREAT_HUNGER_QOI, MMAP_STORAGE_TREAT_MOOD_QOI,
        MMAP_STORAGE_TREAT_ENERGY_QOI, MMAP_STORAGE_TREAT_DEW_QOI,
        MMAP_STORAGE_TREAT_POLLEN_QOI, MMAP_STORAGE_TREAT_SILK_QOI,
        MMAP_STORAGE_TREAT_MOON_QOI, MMAP_STORAGE_TREAT_AMBER_QOI,
        MMAP_STORAGE_TREAT_NEON_QOI, MMAP_STORAGE_TREAT_STAR_QOI,
        MMAP_STORAGE_TREAT_LOTUS_QOI, MMAP_STORAGE_TREAT_RUNE_QOI,
    };
    return kind < sizeof treats / sizeof treats[0] ? treats[kind] : -1;
}

int sprite_background_index(uint8_t scene)
{
    static const int backgrounds[] = {
        MMAP_STORAGE_BACKGROUND_CORK_0_QOI,
        MMAP_STORAGE_BACKGROUND_MOSS_0_QOI,
        MMAP_STORAGE_BACKGROUND_LEAF_LITTER_0_QOI,
        MMAP_STORAGE_BACKGROUND_SAND_0_QOI,
        MMAP_STORAGE_BACKGROUND_FERN_0_QOI,
        MMAP_STORAGE_BACKGROUND_ORCHID_0_QOI,
        MMAP_STORAGE_BACKGROUND_WOODLAND_0_QOI,
        MMAP_STORAGE_BACKGROUND_MINIMAL_0_QOI,
        MMAP_STORAGE_BACKGROUND_MUSHROOM_0_QOI,
        MMAP_STORAGE_BACKGROUND_GOTHIC_0_QOI,
        MMAP_STORAGE_BACKGROUND_SEASHELL_0_QOI,
        MMAP_STORAGE_BACKGROUND_SUCCULENT_0_QOI,
        MMAP_STORAGE_BACKGROUND_WILLOW_0_QOI,
        MMAP_STORAGE_BACKGROUND_DESERT_0_QOI,
        MMAP_STORAGE_BACKGROUND_CRYSTAL_0_QOI,
        MMAP_STORAGE_BACKGROUND_CEMETERY_0_QOI,
        MMAP_STORAGE_BACKGROUND_HOUSE_0_QOI,
        MMAP_STORAGE_BACKGROUND_DARK_CAVE_0_QOI,
        MMAP_STORAGE_BACKGROUND_NIGHT_0_QOI,
        MMAP_STORAGE_BACKGROUND_AUTUMN_0_QOI,
        MMAP_STORAGE_BACKGROUND_SNOW_0_QOI,
        MMAP_STORAGE_BACKGROUND_POND_0_QOI,
        MMAP_STORAGE_BACKGROUND_RUINS_0_QOI,
        MMAP_STORAGE_BACKGROUND_MARSH_0_QOI,
        MMAP_STORAGE_BACKGROUND_FAIRY_GLADE_0_QOI,
        MMAP_STORAGE_BACKGROUND_MOONFLOWER_GROVE_0_QOI,
        MMAP_STORAGE_BACKGROUND_CATHEDRAL_0_QOI,
        MMAP_STORAGE_BACKGROUND_ROSE_COURTYARD_0_QOI,
        MMAP_STORAGE_BACKGROUND_LIBRARY_0_QOI,
        MMAP_STORAGE_BACKGROUND_OBSERVATORY_0_QOI,
        MMAP_STORAGE_BACKGROUND_KITCHEN_0_QOI,
        MMAP_STORAGE_BACKGROUND_SEWING_ROOM_0_QOI,
        MMAP_STORAGE_BACKGROUND_WITCH_GARDEN_0_QOI,
        MMAP_STORAGE_BACKGROUND_MOON_ALTAR_0_QOI,
        MMAP_STORAGE_BACKGROUND_PRIMEVAL_JUNGLE_0_QOI,
        MMAP_STORAGE_BACKGROUND_AMBER_BASIN_0_QOI,
        MMAP_STORAGE_BACKGROUND_NEON_ALLEY_0_QOI,
        MMAP_STORAGE_BACKGROUND_CIRCUIT_GARDEN_0_QOI,
        MMAP_STORAGE_BACKGROUND_LUNAR_BASE_0_QOI,
        MMAP_STORAGE_BACKGROUND_STARSHIP_0_QOI,
        MMAP_STORAGE_BACKGROUND_ENCHANTED_COURT_0_QOI,
        MMAP_STORAGE_BACKGROUND_DRAGON_HEATH_0_QOI,
        MMAP_STORAGE_BACKGROUND_LOST_CITY_0_QOI,
        MMAP_STORAGE_BACKGROUND_SUNKEN_TEMPLE_0_QOI,
        MMAP_STORAGE_BACKGROUND_ZEN_GARDEN_0_QOI,
        MMAP_STORAGE_BACKGROUND_BAMBOO_GROVE_0_QOI,
        MMAP_STORAGE_BACKGROUND_SAKURA_COURTYARD_0_QOI,
        MMAP_STORAGE_BACKGROUND_CLOUD_SANCTUARY_0_QOI,
    };
    return scene < sizeof backgrounds / sizeof backgrounds[0] ? backgrounds[scene] : -1;
}

int sprite_decor_index(const char *name)
{
    if (!name) return -1;
    for (size_t i = 0; i < sizeof k_decor / sizeof k_decor[0]; i++) {
        if (strcmp(k_decor[i].name, name) == 0) return k_decor[i].index;
    }
    return -1;
}

int sprite_border_index(uint8_t scene)
{
    static const int borders[] = {
        MMAP_STORAGE_BORDER_CORK_0_QOI, MMAP_STORAGE_BORDER_MOSS_0_QOI,
        MMAP_STORAGE_BORDER_LEAF_LITTER_0_QOI, MMAP_STORAGE_BORDER_SAND_0_QOI,
        MMAP_STORAGE_BORDER_FERN_0_QOI, MMAP_STORAGE_BORDER_ORCHID_0_QOI,
        MMAP_STORAGE_BORDER_WOODLAND_0_QOI, MMAP_STORAGE_BORDER_MINIMAL_0_QOI,
        MMAP_STORAGE_BORDER_MUSHROOM_0_QOI, MMAP_STORAGE_BORDER_GOTHIC_0_QOI,
        MMAP_STORAGE_BORDER_SEASHELL_0_QOI, MMAP_STORAGE_BORDER_SUCCULENT_0_QOI,
        MMAP_STORAGE_BORDER_WILLOW_0_QOI, MMAP_STORAGE_BORDER_DESERT_0_QOI,
        MMAP_STORAGE_BORDER_CRYSTAL_0_QOI, MMAP_STORAGE_BORDER_CEMETERY_0_QOI,
        MMAP_STORAGE_BORDER_HOUSE_0_QOI, MMAP_STORAGE_BORDER_DARK_CAVE_0_QOI,
        MMAP_STORAGE_BORDER_NIGHT_0_QOI, MMAP_STORAGE_BORDER_AUTUMN_0_QOI,
        MMAP_STORAGE_BORDER_SNOW_0_QOI, MMAP_STORAGE_BORDER_POND_0_QOI,
        MMAP_STORAGE_BORDER_RUINS_0_QOI, MMAP_STORAGE_BORDER_MARSH_0_QOI,
        MMAP_STORAGE_BORDER_FAIRY_GLADE_0_QOI, MMAP_STORAGE_BORDER_MOONFLOWER_GROVE_0_QOI,
        MMAP_STORAGE_BORDER_CATHEDRAL_0_QOI, MMAP_STORAGE_BORDER_ROSE_COURTYARD_0_QOI,
        MMAP_STORAGE_BORDER_LIBRARY_0_QOI, MMAP_STORAGE_BORDER_OBSERVATORY_0_QOI,
        MMAP_STORAGE_BORDER_KITCHEN_0_QOI, MMAP_STORAGE_BORDER_SEWING_ROOM_0_QOI,
        MMAP_STORAGE_BORDER_WITCH_GARDEN_0_QOI, MMAP_STORAGE_BORDER_MOON_ALTAR_0_QOI,
        MMAP_STORAGE_BORDER_PRIMEVAL_JUNGLE_0_QOI, MMAP_STORAGE_BORDER_AMBER_BASIN_0_QOI,
        MMAP_STORAGE_BORDER_NEON_ALLEY_0_QOI, MMAP_STORAGE_BORDER_CIRCUIT_GARDEN_0_QOI,
        MMAP_STORAGE_BORDER_LUNAR_BASE_0_QOI, MMAP_STORAGE_BORDER_STARSHIP_0_QOI,
        MMAP_STORAGE_BORDER_ENCHANTED_COURT_0_QOI, MMAP_STORAGE_BORDER_DRAGON_HEATH_0_QOI,
        MMAP_STORAGE_BORDER_LOST_CITY_0_QOI, MMAP_STORAGE_BORDER_SUNKEN_TEMPLE_0_QOI,
        MMAP_STORAGE_BORDER_ZEN_GARDEN_0_QOI, MMAP_STORAGE_BORDER_BAMBOO_GROVE_0_QOI,
        MMAP_STORAGE_BORDER_SAKURA_COURTYARD_0_QOI, MMAP_STORAGE_BORDER_CLOUD_SANCTUARY_0_QOI,
    };
    return scene < sizeof borders / sizeof borders[0] ? borders[scene] : -1;
}

int sprite_decor_item_index(uint8_t kind)
{
    return kind < sizeof k_decor_items / sizeof k_decor_items[0]
               ? k_decor_items[kind].index : -1;
}
