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

/* How many dust motes drift at once. Eight is enough to read as "there is air
 * in here" without costing a measurable amount of the frame budget. */
#define AMBIENT_MOTES 8

/* How many light shafts cross the tank. Three is the smallest number that reads
 * as light falling through a canopy rather than as one stripe. */
#define AMBIENT_SHAFTS 3

/* Light shafts are BAKED INTO THE BACKGROUND CANVAS, not drawn as LVGL objects.
 *
 * The first two attempts used LVGL rectangles. Rotated ones were software
 * rendered at roughly 111,000 blended pixels each per frame, which made the
 * whole tank choppy. Unrotated ones were cheap but read as blocky vertical
 * stripes going one way -- the "too blocky and symmetrical and only go down the
 * screen in a single direction" report.
 *
 * Baking costs nothing per frame: the beams are part of the background image,
 * so LVGL blits them as ordinary opaque pixels. That leaves the whole frame
 * budget for the spider, and it lets the beams be genuinely diagonal, of
 * different widths, and feathered at the edges -- none of which a rectangle can
 * do. The trade is that they only change when the background is rebuilt, which
 * is exactly when the day period changes. */

/* Set the day period the beams are lit for: 0 morning, 1 afternoon, 2 evening,
 * 3 night, or 255 for no beams at all. Does not repaint; the caller rebuilds the
 * background when this actually changes. */
void ambient_shafts_set_period(uint8_t period);

/* The period the beams are currently lit for, so a caller can tell whether a
 * rebuild is needed. */
uint8_t ambient_shafts_period(void);

/* Pin the beams to a fixed period, ignoring the clock, so they can be inspected
 * on the panel. `period` 0..3 pins; anything else releases. */
void ambient_shafts_force(uint8_t period);

/* True while a forced period is in effect. */
bool ambient_shafts_forced(void);

/* Composite the beams over an RGB565 buffer of `w` x `h` pixels. Called from
 * terrarium_rebuild() after the background is filled and before the border is
 * blitted, so the frame stays on top of the light. */
void ambient_shafts_bake(uint16_t *dst, int32_t w, int32_t h);

/* True when a beam covers this pixel. Used by the self-test to prove the beams
 * are diagonal and not all parallel, without needing a framebuffer. */
bool ambient_shafts_lit_at(int32_t x, int32_t y, int32_t h);

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
