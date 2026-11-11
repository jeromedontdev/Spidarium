# Spidarium v1.1.13 — Ground textures and placeable decor

## The enclosure is now something you arrange

v1.1.12 rebuilt the sixteen enclosures as painted scenes. They were brighter and
more detailed, but they were still cluttered: the spider had poor contrast
against its own floor, and every enclosure was the same composition recoloured.

This release splits the enclosure in two.

**Backgrounds are ground texture only.** Each of the sixteen themes is now the
substrate a keeper would actually use — cork, moss, leaf litter, sand, coir,
bark chip, gravel — seen from directly above, with a meandering trail and fine
grit. No canopies, no pebble scatter, no flower clusters, no fronds. The floor
is clean, so the spider reads first.

**Everything on the floor is decor you buy and place.** Sixteen kinds:

| Kind | Silk | Level | Blocks |
|---|---:|---:|---|
| Twig | 12 | 1 | no |
| Pebbles | 14 | 1 | no |
| Leaf Pile | 16 | 1 | no |
| Small Rock | 18 | 1 | no |
| Moss Clump | 20 | 2 | no |
| Bark Slab | 22 | 3 | no |
| Shell | 24 | 5 | no |
| White Flower | 26 | 3 | no |
| Mushroom | 28 | 4 | no |
| Pink Flower | 30 | 6 | no |
| Large Rock | 34 | 2 | **yes** |
| Fern | 38 | 5 | no |
| Log | 46 | 4 | **yes** |
| Succulent | 52 | 7 | **yes** |
| Bone | 58 | 8 | **yes** |
| Crystal | 68 | 9 | **yes** |

Pick a kind in the new **Decor** menu, tap the tank to place it, and twelve slots
hold the result. The corner X cancels placement, exactly like the gift flow.

## Blocking is a steering input, not a wall

Each item is either **blocking** or **non-blocking**.

A blocking item is not a positional shove. The spider turns away from it *before*
it arrives, the same way it already turns away from the gift and from the walls.
It measures the berth from the item's footprint edge, so a 22 px log pushes the
spider away further out than a 9 px pebble cluster, and it only steers when the
item is actually ahead — an item behind the spider must not push it, or the
spider could never turn around.

Non-blocking decor is ignored by steering entirely. That is the point of the
flag: the spider is meant to walk over a leaf pile.

Tap a placed item in **Placed Items** to flip its flag. The choice is saved.

## 16-bit SNES-era art direction

The art direction is now written down and enforced in
[docs/ART_STYLE.md](../docs/ART_STYLE.md):

- crescent shading rather than concentric rings — offset the highlight ellipse
  up-left and re-stamp the body over its lower-right, which is what gives a prop
  volume instead of reading as a flat disc with a border;
- three tones per material (shadow, body, highlight), because two tones read
  flat;
- an ink rim on every silhouette, so a prop separates from the substrate;
- no gradients, no translucent shadows, no blur — a gradient is a filtered
  resize in disguise.

The fern was redrawn under this contract. It is now a rosette of arcing fronds:
a curved rachis with paired pinnae stamped along it, shrinking toward the tip.
Drawing a fern as one pointed blade is what made the earlier attempts read as
grass.

## Save format v8

`game_state_t` grows from 160 to 236 bytes: `decor_owned` at offset 160 and
`decor[12]` at 164. A v7 save is a valid prefix and is migrated by clearing the
slots — kind 0 is a real item, so a zeroed slot would otherwise place twelve
small rocks on an upgraded pet. Existing pets, Silk, gifts and progress are
preserved.

## Browser parity

The companion site mirrors all of it: the same sixteen kinds with the same names,
costs, unlock levels, footprint radii and blocking flags, in the same order. A
test parses the C catalogue table and compares it field by field, so the two
targets cannot drift. Placement, blocking and the Placed Items list behave
identically.

## Verification

- `build.ps1` exit 0, zero `main/` warnings.
- `asset_build.py`: 158 MMAP entries, QOI dimensions/opcodes/pixels verified;
  158 PNGs valid; deterministic across two passes; all 123 preserved PNGs
  byte-identical; 16 opaque 240×240 ground textures.
- `flash.ps1 COM7` exit 0.
- `run_tests.py COM7`: **47 passed, 0 failed**.
- `uitest`: `OK uitest pages=1 keyboard=1 ****** hud=1 backgrounds=1`.
- `decortest`: `OK decortest placed=12 slots=12`.
- `stability_check.py COM7 40`: `VERDICT: PASS`, 0 faults.
- `scanloop 5`: `largest_drift=0`. `render 500`: `largest_drift=0`.
- Internal DMA largest 63,488 bytes, above the 51,200 floor.
- Decor survives a reboot; `save schema` reports version 8 with the decor fields
  at their documented offsets.
- Browser suite **97/97**.
