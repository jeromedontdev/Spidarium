/*
 * Spidarium — placeable decor.
 *
 * Backgrounds are ground texture only. Everything that sits ON the floor is a
 * decor item the player buys with Silk and places themselves, which is what
 * makes an enclosure feel arranged rather than painted.
 *
 * Each kind has a footprint and a blocking flag:
 *
 *   blocking      the spider steers around it and will not walk over it
 *   walk over     the spider walks straight over it
 *   walk behind   the spider passes beneath the artwork, without collision
 *
 * Blocking is a steering input, not a positional shove, so the spider turns
 * away before reaching the item instead of grinding against it.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Decor kinds. Order is the shop order and must match k_decor_kinds[] in
 * decor.c and the sprite table in sprites.c. */
typedef enum {
    DECOR_ROCK_SMALL = 0,
    DECOR_ROCK_LARGE,
    DECOR_PEBBLES,
    DECOR_LOG,
    DECOR_BARK,
    DECOR_FERN,
    DECOR_MOSS_CLUMP,
    DECOR_SUCCULENT,
    DECOR_FLOWER_WHITE,
    DECOR_FLOWER_PINK,
    DECOR_MUSHROOM,
    DECOR_SHELL,
    DECOR_CRYSTAL,
    DECOR_LEAF_PILE,
    DECOR_TWIG,
    DECOR_BONE,
    DECOR_FERN_POT,
    DECOR_ACORN_HIDE,
    DECOR_DRIFTWOOD_ARCH,
    DECOR_WEB_HAMMOCK,
    DECOR_DEW_POOL,
    DECOR_MUSHROOM_PAIR,
    DECOR_FLOWER_PLANTER,
    DECOR_CRYSTAL_CLUSTER,
    DECOR_SHELL_COVE,
    DECOR_TWIG_LADDER,
    DECOR_MOSS_STONE,
    DECOR_SEED_POD,
    DECOR_SILK_TENT,
    DECOR_LEAF_UMBRELLA,
    DECOR_BONE_ARCH,
    DECOR_FOSSIL,
    DECOR_LANTERN_CAP,
    DECOR_STONE_BRIDGE,
    DECOR_FAIRY_RING,
    DECOR_FOXGLOVE_LANTERN,
    DECOR_PIXIE_DOOR,
    DECOR_GARGOYLE,
    DECOR_IRON_GATE,
    DECOR_ROSE_URN,
    DECOR_BOOK_STACK,
    DECOR_GLOBE,
    DECOR_QUILL_DESK,
    DECOR_TEA_CUP,
    DECOR_SPOOL,
    DECOR_THIMBLE,
    DECOR_CAULDRON,
    DECOR_BROOM,
    DECOR_POTION_RACK,
    DECOR_AMBER_FOSSIL,
    DECOR_CYCAD,
    DECOR_DINOSAUR_EGG,
    DECOR_NEON_SIGN,
    DECOR_HOLO_TERMINAL,
    DECOR_CABLE_COIL,
    DECOR_MOON_ROVER,
    DECOR_SOLAR_ARRAY,
    DECOR_OXYGEN_POD,
    DECOR_WIZARD_TOWER,
    DECOR_DRAGON_STATUE,
    DECOR_TREASURE_CHEST,
    DECOR_SUN_OBELISK,
    DECOR_GLYPH_TABLET,
    DECOR_BROKEN_COLUMN,
    DECOR_STONE_LANTERN,
    DECOR_BAMBOO_FOUNTAIN,
    DECOR_BONSAI,
    DECOR_FAIRY_BRIDGE,
    DECOR_COUNT,
} decor_kind_t;

/* Legacy inline slots retained for old save compatibility, not a placement cap. */
#define DECOR_SLOTS 12

typedef enum {
    DECOR_WALK_OVER = 0,
    DECOR_SOLID = 1,
    DECOR_WALK_BEHIND = 2,
    DECOR_PATH_COUNT
} decor_path_t;

typedef struct {
    uint8_t kind;      /* decor_kind_t */
    uint8_t blocking;  /* decor_path_t; byte retained for save compatibility */
    int16_t x, y;      /* playfield-local centre */
} decor_item_t;

typedef struct {
    const char *name;
    const char *effect;      /* one legible line for the shop row */
    uint16_t    cost;        /* Silk */
    uint8_t     unlock_level;
    uint8_t     radius;      /* footprint radius in pixels */
    uint8_t     blocking;    /* default blocking flag */
    const char *sprite;      /* asset name, e.g. "decor_rock_small" */
} decor_def_t;

/* The definition for a kind. Never NULL for a valid kind. */
const decor_def_t *decor_def(uint8_t kind);

/* True if the kind is unlocked at the current level. */
bool decor_unlocked(uint8_t kind);

/* True if the player owns at least one of the kind. */
bool decor_owned(uint8_t kind);

/* True if the kind can be bought right now (unlocked and affordable). */
bool decor_can_buy(uint8_t kind);

/* Buy one of the kind. Returns false and logs why on refusal. */
bool decor_buy(uint8_t kind);

/* Number of items currently placed. */
int decor_placed_count(void);

/* The placed item at a slot, or NULL when the slot is empty. */
const decor_item_t *decor_at(int slot);
int decor_slot_count(void);
const decor_item_t *decor_extra_data(void);
decor_item_t *decor_detach_extra(void);
/* Adopt a worker/test-owned PSRAM record array; g carries its restored count. */
void decor_restore_extra(decor_item_t *items);

/* Place an owned kind at a playfield position. Returns the slot, or -1. */
int decor_place(uint8_t kind, int16_t x, int16_t y);

/* Move a placed item to a new playfield position, clamped to the field. */
bool decor_move(int slot, int16_t x, int16_t y);

/* Remove the item in a slot. */
bool decor_remove(int slot);

/* Cycle Walk Over -> Solid -> Walk Behind. */
bool decor_toggle_blocking(int slot);
bool decor_set_path(int slot, uint8_t path);
int decor_layer_count(uint8_t path, int exclude_slot);
int decor_layer_index(int slot);
/* Reorder within the floor/canopy band. Returns the item's new slot, or -1. */
int decor_set_layer(int slot, int index);

/* The placed item whose footprint contains a point, or -1. */
int decor_hit_test(int16_t x, int16_t y);

/* True if a point is inside any blocking item's footprint. Used by the
 * behaviour module to steer around decor. */
bool decor_blocks_point(int16_t x, int16_t y);

/* Nearest blocking item within range, for steering. Returns false when the
 * path ahead is clear. */
bool decor_nearest_blocking(int16_t x, int16_t y, float max_distance,
                            int16_t *out_x, int16_t *out_y, float *out_distance);

/* Clear every placed item (used by reset). */
void decor_clear(void);

/* Mark every slot in a raw decor array empty. Used by save migration, which
 * works on a partially-populated state before it is installed. */
void decor_clear_slots(decor_item_t *slots, int count);

/* Self-test for the console. */
bool decor_test(void);
