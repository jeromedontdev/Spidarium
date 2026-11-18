/*
 * Spidarium — ambient life.
 *
 * The tank should read as a living ecosystem, not a diorama with one moving
 * part. This module adds the small, cheap, always-on motion that sells that:
 * dust motes drifting in the light, and a spider that breathes, blinks and
 * glances around even when it is doing nothing.
 *
 * WHY THIS AND NOT MORE ART: every effect here is drawn from the theme's own
 * palette (`theme->dust`, `theme->web`) and from primitives the renderer
 * already has. There is no new sprite, no new asset, and no image generation.
 * The whole module is a few hundred bytes of state and a handful of LVGL
 * objects that are created once and moved, never created or deleted per frame.
 *
 * THREADING: this runs on the LVGL adapter task, from the same logic tick that
 * already drives the spider. It never blocks, never allocates per frame, and
 * never touches the network, NVS or SD.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* How many dust motes drift at once. Sixteen reads as air with real depth; the
 * original eight was sparse enough to look like a few stray pixels. */
#define AMBIENT_MOTES 16

/* How many drifting spores/pollen specks ride the air alongside the dust. These
 * are larger and slower than the motes and drift sideways as well as up, so the
 * air reads as having two layers rather than one uniform field. */
#define AMBIENT_SPORES 6

/* How many web strands hang in the tank. Six is enough to read as a neglected
 * corner of an enclosure rather than as two stray lines. */
#define AMBIENT_STRANDS 6

/* How many decor/gift particles exist at once.
 *
 * This is a FIXED POOL, not one emitter per item. The tank can hold twelve decor
 * slots plus a gift, and giving each its own particles would make the cost scale
 * with how much the player has bought -- the worst possible property for a
 * budget. Instead a small pool is reassigned to whichever items are on screen,
 * so the cost is constant whether the tank is empty or full.
 *
 * 48 is deliberately generous. The first pass used 16 and the effect was
 * invisible: with a dozen items placed, each one got a particle roughly once
 * every four seconds, which reads as nothing at all. The pool is cheap because
 * the objects are created once and only moved, so the count is the wrong place
 * to economise -- the spawn cadence is. */
#define AMBIENT_PARTICLES 48

/* How often the ambient layer is pushed to LVGL, in milliseconds.
 *
 * The simulation runs on every render tick; only the present is throttled. The
 * cost of an ambient object is not its CPU time but the dirty rectangle it adds,
 * and these objects are scattered across the whole tank -- so at 30 Hz the layer
 * was costing about 4.5 Hz of frame rate. Dust drifts at 4-12 px/s, so at 10 Hz
 * it still advances less than a pixel per update and looks identical. */
#define AMBIENT_PRESENT_MS 100

/* There are no light shafts.
 *
 * They were tried three ways and removed. Rotated LVGL rectangles were software
 * rendered at ~111,000 blended pixels each per frame and made the tank choppy.
 * Unrotated bands were cheap but read as blocky vertical stripes going one way.
 * Baking them into the background fixed both, but a baked beam cannot move, and
 * static light does not read as light -- it reads as a stain on the art. Making
 * it move means invalidating a large alpha region every frame, which is the
 * choppiness again.
 *
 * The lesson is that a full-screen lighting effect is the wrong shape for this
 * budget. Aliveness comes from many small things that move independently, which
 * is what the rest of this module does. */

/* Create the mote objects. Call once, after the terrarium exists and before the
 * creature layer, so the motes sit behind the spider. */
void ambient_init(void);

/* Advance the motes and the spider's idle life. Called from the logic tick. */
void ambient_tick(uint32_t now_ms);

/* Re-seed the motes for the current theme. Called when the theme changes, so
 * the motes take the new palette rather than keeping the old one. */
void ambient_retint(void);

/* Hide or show the motes. The tank is hidden while a menu is open, and moving
 * invisible objects is wasted work. */
void ambient_set_visible(bool visible);

/* The spider's idle life, for the renderer.
 *
 * `breath` is a 0..1 scale the body is drawn at, so the abdomen rises and falls
 * a couple of percent. `blink` is 0..1 where 1 is fully closed. Both are cheap
 * to sample and are read by cspider.c when it draws the body and the eyes. */
float ambient_breath(void);
float ambient_blink(void);

/* True while the spider is glancing to one side. The renderer offsets the eyes
 * rather than rotating the body, because rotating the body sweeps the whole leg
 * fan and reads as the legs moving on a stopped spider. */
float ambient_glance(void);

/* Opacity for the dimmed-enclosure overlay, 0..255.
 *
 * The overlay used to be a fixed 110. A fixed value reads as a filter laid over
 * a photograph; a slow drift reads as a room whose light is coming from
 * somewhere. The drift is small -- a few percent -- so it never looks like the
 * screen is flickering. */
uint8_t ambient_light_opa(void);

/* Report the ambient state, for the console and the tests. */
void ambient_report(void);

/* Self-test: the motes stay inside the tank and the idle life stays in range. */
bool ambient_test(void);
/* A slow, smooth 0..1 value that never repeats on a short cycle, for effects
 * that want to breathe at their own pace. `seed` picks a different phase so two
 * callers do not move in lockstep. */
float ambient_wander(uint32_t seed);

/* Which particle style an item emits. Chosen from the item's own kind, so no new
 * data has to be authored per decor entry. */
typedef enum {
    AMBIENT_EMIT_NONE = 0,
    AMBIENT_EMIT_SPORE,   /* drifting specks: plants, moss, mushrooms */
    AMBIENT_EMIT_GLINT,   /* brief bright flashes: crystal, glass, metal */
    AMBIENT_EMIT_DUST,    /* slow settling motes: stone, wood, bone */
} ambient_emit_t;

/* The emitter style for a decor kind. Pure lookup, no state. */
ambient_emit_t ambient_emit_for_decor(uint8_t kind);

/* The emitter style for a gift kind. */
ambient_emit_t ambient_emit_for_gift(uint8_t kind);
