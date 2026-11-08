# Spidarium asset style

The non-spider set is deliberately authored retro pixel art, not a reduced
version of smooth/vector artwork. It uses integer geometry, crisp stepped
outlines, flat color clusters, and restrained single-color highlights. Muted
natural colors identify creatures/props; brighter accents identify controls
and keepsakes. There are no antialiased edges, gradients, translucent shadows,
or filtered resizes.

## Pixel-grid contract

| Group | Logical canvas | PNG and runtime size |
|---|---|---|
| Prey | 24×24 | 48×48 |
| UI icons | 24×24 | 48×48 |
| Achievement badges | 24×24 | 48×48 |
| Gifts | 16×16 | 32×32 |
| Rock 0 / rock 1 | 24×16 / 20×14 | 48×32 / 40×28 |
| Plant 0 / plant 1 | 24×32 / 26×28 | 48×64 / 52×56 |
| Wood log | 48×18 | 96×36 |
| Enclosure backgrounds | 120×120 | 240×240 source → 480×480 floor canvas |

Draw directly on the logical canvas, then enlarge exactly 2× using
nearest-neighbor sampling. Every aligned 2×2 block, including transparent
pixels, must contain identical RGBA values. Alpha is binary (0 or 255);
backgrounds are fully opaque (255 throughout).
Each non-spider asset may use at most 12 visible RGBA colors. Keep native
runtime dimensions; shrinking icons or filtering images breaks the grid.

Prey are top-down and orientation-neutral. This keeps them readable while they
move in any direction without LVGL matrix or image-mirroring support. Walk and
flee clips have two alternating frames; the dead pose is a separate frame.
Fly wings/red eyes, cricket antennae/jumping legs, patterned moth wings, and
beetle shell seams distinguish the original four species even in their dead poses.
Their artwork remains byte-identical. The four added species use an elongated
oval roach with long antennae, a narrow tan mealworm with segment bands, a
smaller red-eyed fruitfly with paired wings, and a plump pale waxworm with
stubby prolegs and a brown head. Each has five native 48×48 frames:
`walk_0`, `walk_1`, `flee_0`, `flee_1`, and `dead_0`.
Gift shapes use individual silhouettes and facets: crystal clusters, octagonal
rubies, pointed sapphires, rectangular emeralds, and cut diamonds are not one
generic gem recolored. Actions use a bowl (feed), broom (clean), ball (play),
crescent (sleep), potted sprout (decor), and clipboard (status).

## Authored enclosure backgrounds

The eight background PNGs show actual small jumping-spider keeper boxes from
above: cork, moss, leaf litter, sand, fern, orchid, woodland, and minimal.
They are authored compositions, not runtime procedural backgrounds or outside
landscapes. A restrained acrylic rim with ventilation slots encloses a clean
central substrate/paper floor. Bark slabs, hollow cork tubes, branching
climbing twigs, and a layered silk sleeping sac anchored near the upper wall establish
plausible keeper furniture. Botanical leaves attach to visible stems; fern
leaflets attach to their frond midrib. Material texture stays within bark,
moss cushions, or leaf blades, rather than becoming random floor clutter.

Use two or three subtle flat tones per material with crisp clustered
highlights and bounded fissures/veins. No gradients, scattered random pixels,
mountain silhouettes, horizons, or sparse geometric stripes. Minimal is still
a believable keeper box: folded paper substrate, a forked climbing twig, a
small attached leafy sprig, and an anchored sleeping retreat.
Jumping spiders do not use radial orb capture webs: the retreat is a compact,
irregular pouch with overlapping silk bands, a small dark entrance and a few
attachment strands. Do not draw concentric rings or a spoke grid.

Floors use subdued shades so the creature reads first. The generator applies
40% channel intensity to floor shades, 55-65% to most furniture and 68% to silk.
Keep the central 40%-60% region of each background at RGB channel values <=96;
the validator rejects brighter hunting floors. Material accents can be brighter
than the floor without turning the entire enclosure pale again.

Each image is authored at 120×120 and exported at exactly 240×240 with
nearest-neighbor doubling. Runtime decodes eight RGB565A8 sources into PSRAM
(1,382,400 bytes total) and enlarges the selected source 2× into the existing
480×480 RGB565 floor canvas. Do not export 480×480 PNGs or change the source
dimensions. The five separate terrarium prop PNGs are optional resources;
painted background furniture does not require automatic prop placement.

The spider artwork is a separate 96×96 canvas set and is intentionally
unchanged. Do not edit `draw_spider()`, its inputs, or the existing
`spider_*.png` files as part of non-spider asset work.

## Generate and validate

Pillow is required. From the repository root, run:

```powershell
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\asset_build.py
```

`tools\gen_sprites.py` deterministically writes all assets into `assets\`.
`tools\asset_build.py` runs it twice and checks the exact file set, count,
dimensions, PNG format, RGBA mode, identical SHA-256 hashes across both runs,
byte-for-byte preservation of 115 existing accepted images
(including all 39 spider and 32 living prey images), and the non-spider
pixel-grid/alpha/palette contract. Background opacity is checked separately.
Gift art is now purpose-built habitat furniture: hides, perches, resting places
and enrichment. Keep the original stable filenames and 16->32 source-art scale;
the tank CPU enlarges the selected item to 64px without per-frame transforms.
The current round deliberately replaces the eight dead-prey images with
species-colored puddles/drops/shards and adds eleven uniquely shaped badges.
The living prey and spider art remain protected. Badge hashes and silhouettes
must be unique; splatter coverage, disconnected fragments and representative
material pixels are checked against intact-corpse regressions.
Preservation compares the files present before a validator run; gifts and
backgrounds are now protected too. For an intentional approved redesign, run
the edited generator first, then validate with `--png-only` and rebuild the pack.
Only achievements and dead-prey files are preservation-exempt, never exempt
from determinism or pixel/palette tests.
It also validates an existing
MMAP pack, decodes its QOI streams, and compares every pixel with the PNGs.
Use `--png-only` while the existing pack is stale; runtime mapping checks
remain enabled. After rebuilding, run the default command or `--packed PATH`
to validate the packed image.

## Adding or replacing art

1. Add deterministic integer Pillow geometry on a half-resolution canvas and
   a filename in `tools\gen_sprites.py`. Use `_enlarge()` only for exact
   nearest-neighbor doubling. Avoid random inputs, machine fonts, external
   downloads, float geometry, and smoothing.
2. Update the expected names and native dimensions in
   `tools\asset_build.py`.
3. If runtime code needs the asset, add its generated enum mapping in
   `main\sprites.c` and reference it from the owning renderer. The enum is
   produced by the ESP-IDF asset-pack step; do not hand-edit
   `main\mmap_generate_storage.h`.
4. Run `asset_build.py`, then build with `build.ps1` to pack the PNGs as QOI.

Keep the spider generator path and files unchanged when working on other art.
The checked asset set currently contains 134 PNGs (95 non-spider); the runtime
descriptor table is sized from the generated asset count.
