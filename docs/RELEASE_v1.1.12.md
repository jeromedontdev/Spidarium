# Spidarium v1.1.12 — enclosures that look like enclosures

The sixteen keeper backgrounds were rebuilt from scratch. The old ones were
drawn on a shared template — a framed box with a back wall, ventilation slots
and a dark central trough — and every palette was crushed through a shared
brightness table, so all sixteen came out grey and interchangeable.

The new ones are top-down views of a keeper's substrate, drawn the way the
reference art is drawn.

## What changed in the art

**Authored at native resolution.** Backgrounds used to be composed at 120×120
and doubled to 240×240, so every authored pixel became a 4×4 block on the
480×480 panel. They are now composed at the full 240×240 and enlarged 2×, which
is the same upscale the panel already did — but with four times the detail
budget.

**No palette cap.** The asset validator rejected any image with more than
twelve visible colours, which is a ZX Spectrum palette. That rule still applies
to the sprite set, where it is the point, but backgrounds are illustrations and
now carry a full colour ramp. A typical enclosure went from 19 distinct colours
to roughly 180.

**Continuous shading.** Tones are computed from HLS specs and interpolated
rather than snapped to a handful of flat entries, so the floor has a lit side
and a shadowed side instead of banding.

**Earth floors, green foliage, grey stone, pale flowers.** The old palettes
picked a hue per theme and used it for everything, so a "moss" enclosure was
green on green with no separation. Every theme now follows the reference's
material logic: the floor is earth or stone, the foliage is green, the rocks are
neutral grey and the flowers are pale. Themes differ in the hue of the floor and
in which props they scatter.

**A worn trail.** The earth-toned themes have a meandering dirt path through the
middle, blended into the surrounding ground over its last few pixels. This is
what stops a floor reading as wallpaper: the reference always has two materials
meeting, with props crowding the edges of the trail.

**Real prop vocabulary.** Rocks are the dominant prop, with cast shadows, dark
rims, lit crowns, speckled facets and hairline cracks. Foliage is built from
pointed-oval leaves with a two-pixel dark outline, a midrib, side veins and a
bright sliver along the lit edge. Loose debris — twig fragments, seed husks and
grit — is scattered across every floor.

## The hunting floor is no longer forced dark

The validator used to require `max(rgb) <= 96` across the central two-fifths of
every background, so the spider and its prey would read against a dark trough.
That rule is what produced the dark oval in the middle of every enclosure, and
it is incompatible with the reference look, which has bright floors and props in
the centre.

The rule is gone. The spider is a direct-pixel canvas with its own outline and
the prey sprites are high-contrast, so they read against a mid-tone floor
without a painted shadow behind them.

## Also in this release

- **Prey species scale nutrition.** Each species carries `hunger_mult`,
  `mood_mult` and `energy_mult`. A waxworm is worth 2.4× a fly's hunger; a fruit
  fly is worth 0.7×. The Species menu shows `food x%.1f` so the trade-off is
  visible rather than hidden.
- **Needs scale with age.** Hunger, hygiene and energy decay are multiplied by
  `needs_age_scale()` — 1.0 at juvenile rising to a 1.45 cap — so an adult is
  genuinely more demanding without the player being told a number.
- **Menu subtext is standardised.** Every menu row carries a subtext line; the
  mixed treatment was visually uneven.

## Verification

- `build.ps1` exit 0, zero `main/` warnings.
- `asset_build.py`: 142 MMAP entries, QOI dimensions/opcodes/pixels verified;
  142 PNGs valid; generation deterministic across two passes; all 107
  spider/living-prey/UI/gift/decor PNGs byte-identical; 16 opaque 240×240
  enclosures.
- `flash.ps1 COM7` exit 0.
- `run_tests.py COM7`: 47 passed, 0 failed.
- `stability_check.py COM7 40`: `VERDICT: PASS`, 0 faults.
- `scanloop 5`: `largest_drift=0`. `render 500`: `largest_drift=0`.
- Internal DMA largest 65,536 bytes, above the 51,200 floor.
- All 16 enclosures captured from the panel and inspected.
- Browser suite: 93 passed, 0 failed.

## Storage

`storage.bin` grew from 158,792 to 883,864 bytes. The storage partition is 6 MB,
so there is ample headroom, but the flash image is larger and the first flash
after this release takes noticeably longer.
