# Spidarium asset style

Spidarium's art direction is **16-bit SNES-era pixel art**: the look of a
late-generation 16-bit console game, not an 8-bit micro, not a vector
illustration, and not a filtered photo.

The distinction matters because it is easy to drift into the wrong one. An
8-bit look is a handful of flat colours, one-pixel outlines and no shading. A
16-bit look keeps the crisp pixel grid but spends far more of its budget on
volume and material: crescent shading, three or four tones per material, ink
rims that separate a prop from the floor, and enough colour depth that a rock
reads as stone rather than as a grey blob.

## The 16-bit contract

| Rule | Why |
|---|---|
| Integer geometry only, drawn on a half-resolution canvas, then enlarged exactly 2× with nearest-neighbour | Keeps every pixel a crisp 2×2 block. No antialiasing, no filtered resizes. |
| Binary alpha (0 or 255) on every sprite | LVGL blends RGB565A8; a soft alpha edge would smear the pixel grid. |
| Ink rim on every prop silhouette | A prop must separate from the substrate. Without the rim, dark decor on a dark floor disappears. |
| Three tones per material: shadow, body, highlight | This is the whole difference between 8-bit and 16-bit. Two tones read flat. |
| Crescent shading, not concentric rings | Offset the highlight ellipse up-left and re-stamp the body over its lower-right. Concentric rings read as a flat disc with a border. |
| No gradients, no translucent shadows, no blur | The grid is the style. A gradient is a filtered resize in disguise. |
| Backgrounds are exempt from the 12-colour cap and the 2×2 uniformity rule | They are authored at native 240×240 and are illustrations, not sprites. |

## Pixel-grid contract

| Group | Logical canvas | PNG and runtime size |
|---|---|---|
| Prey | 24×24 | 48×48 |
| UI icons | 24×24 | 48×48 |
| Achievement badges | 24×24 | 48×48 |
| Gifts | 16×16 | 32×32 |
| Placeable decor | 32×32 | 64×64 |
| Enclosure ground textures | 240×240 | 240×240 source → 480×480 floor canvas |

Sprites keep the 2×2 uniformity rule and the 12-visible-colour cap. Backgrounds
keep neither: they are authored at the panel's native 240×240 and enlarged 2×
into the 480×480 canvas at runtime, so each source pixel covers a 2×2 block
without being quantised to a tiny palette. Backgrounds must still be fully
opaque.

## Enclosure ground textures

Backgrounds are **ground texture only**. They are the substrate a keeper would
actually use — cork, moss, leaf litter, sand, coir, bark chip, gravel — seen
from directly above, with a meandering trail and fine grit. They contain no
props.

This is a deliberate split. Earlier rounds painted furniture into the
background, and the result was cluttered: the spider had poor contrast against
its own floor, and every enclosure looked like the same scene recoloured. Props
are now **placeable decor** the player buys with Silk and positions themselves,
which is what makes an enclosure feel arranged rather than painted.

Each ground texture is built from:

- layered value noise at three scales (coarse, medium, fine) so the substrate
  has structure at more than one distance;
- directional light from the top-left with a vignette, so the floor has a
  consistent light source;
- an optional meandering trail blended over about 7 px, in a warm earth tone
  distinct from both the floor and any foliage;
- gentle tonal drift that never goes darker than the base, plus fine grit.

Palettes are generated from HLS specs rather than hand-picked, so every theme
keeps the same contrast structure: the floor sits in a narrow lightness band,
and the trail always separates from it. Hand-picked greens kept collapsing into
the substrate.

Do not reintroduce canopies, pebble scatter, flower clusters, fronds or any
other prop primitive into the background path. If a scene needs a prop, it is a
decor item.

## Placeable decor

Sixteen kinds, each a 32×32 logical sprite doubled to 64×64. Each has a
footprint radius and a blocking flag:

- **blocking** — the spider steers around it and will not walk over it;
- **non-blocking** — the spider walks straight over it.

Blocking is a steering input, not a positional shove, so the spider turns away
before reaching the item instead of grinding against it. The renderer draws each
item at `radius * 4` pixels so the art and the collision circle agree.

The catalogue order is load-bearing: `decor_kind_t` in `main/decor.h`,
`k_decor_kinds[]` in `main/decor.c`, `k_decor_items[]` in `main/sprites.c`,
`DECOR_ITEMS` in `tools/asset_build.py`, and `DECOR_KINDS` in
`site/lib/device-engine.js` must all agree.

A fern is drawn as a rosette of arcing fronds — a curved rachis with paired
pinnae stamped along it, shrinking toward the tip. Drawing a fern as one pointed
blade is what makes it read as grass.

## Generate and validate

Pillow is required. From the repository root:

```powershell
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\asset_build.py
```

`tools\gen_sprites.py` deterministically writes all assets into `assets\`.
`tools\asset_build.py` runs it twice and checks the exact file set, count,
dimensions, PNG format, RGBA mode, identical SHA-256 hashes across both runs,
byte-for-byte preservation of the accepted images, and the non-spider
pixel-grid/alpha/palette contract. Background opacity is checked separately.

It also validates an existing MMAP pack, decodes its QOI streams, and compares
every pixel with the PNGs. Use `--png-only` while the existing pack is stale;
runtime mapping checks remain enabled. After rebuilding, run the default command
or `--packed PATH` to validate the packed image.

Preservation compares the files present before a validator run. Achievements,
dead-prey splatters and backgrounds are preservation-exempt, never exempt from
determinism or pixel/palette tests. For an intentional approved redesign, run
the edited generator first, then validate with `--png-only` and rebuild the pack.

## Adding or replacing art

1. Add deterministic integer Pillow geometry on a half-resolution canvas and a
   filename in `tools\gen_sprites.py`. Use `_enlarge()` only for exact
   nearest-neighbour doubling. Avoid random inputs, machine fonts, external
   downloads, float geometry, and smoothing.
2. Update the expected names and native dimensions in `tools\asset_build.py`.
3. If runtime code needs the asset, add its generated enum mapping in
   `main\sprites.c` and reference it from the owning renderer. The enum is
   produced by the ESP-IDF asset-pack step; do not hand-edit
   `main\mmap_generate_storage.h`.
4. Run `asset_build.py`, then build with `build.ps1` to pack the PNGs as QOI.

The spider artwork is a separate 96×96 canvas set and is intentionally
unchanged. Do not edit `draw_spider()`, its inputs, or the existing
`spider_*.png` files as part of non-spider asset work.

The checked asset set currently contains 158 PNGs (119 non-spider); the runtime
descriptor table is sized from the generated asset count.
