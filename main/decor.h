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
 *   non-blocking  the spider walks straight over it
 *
 * Blocking is a steering input, not a positional shove, so the spider turns
 * away before reaching the item instead of grinding against it.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

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
    DECOR_COUNT,
} decor_kind_t;

/* How many items can be placed at once. */
#define DECOR_SLOTS 12

typedef struct {
    uint8_t kind;      /* decor_kind_t */
    uint8_t blocking;  /* 1 = spider avoids it */
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

/* Place an owned kind at a playfield position. Returns the slot, or -1. */
int decor_place(uint8_t kind, int16_t x, int16_t y);

/* Remove the item in a slot. */
bool decor_remove(int slot);

/* Toggle the blocking flag of a placed slot. */
bool decor_toggle_blocking(int slot);

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
