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
