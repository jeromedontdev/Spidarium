# Asset source files

These are generator outputs. Edit `tools\gen_sprites.py`, not individual
non-spider PNGs, so art changes remain reproducible. The spider PNGs are
preserved artwork; the validator fails if generation changes their bytes.

Preservation compares the PNGs present before a validation run, not a committed
hash baseline. Only achievements and dead-prey files are exempt. For an
intentional approved gift/background change, edit the generator, run
`tools\gen_sprites.py` first to establish the new source baseline, then validate
with `tools\asset_build.py --png-only`, rebuild and validate the packed art.
`--png-only` is not a preservation bypass.

From the repository root, regenerate and validate the complete set with:

```powershell
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\asset_build.py
```

The script checks exactly 134 PNGs, their names and dimensions, RGBA mode,
determinism across two runs, and byte-preservation of 115 accepted images,
including all spider and living prey art. Eight dead images intentionally
become splatters and eleven achievement badges are added. All 95 non-spider assets are authored as retro
pixel art at half resolution and enlarged exactly
2× with nearest-neighbor sampling: prey/icons 24→48 px, gifts 16→32 px,
backgrounds 120→240 px, and props at half their native sizes. Each 2×2 RGBA
block must be uniform, alpha must be 0 or 255 (always 255 for backgrounds),
and each asset has at most 12 visible colors.
No smoothing, gradients, translucent shadows, or downsampled vector art.

An existing pack must match the regenerated PNG pixels. Before rebuilding a
stale pack, validate source art alone with:

```powershell
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\asset_build.py --png-only
```

After packing, use `--packed PATH` to explicitly validate the MMAP/QOI image;
runtime mapping checks remain enabled in all modes. The default command also
checks the pack when present. The build system converts these PNGs to QOI
for the memory-mapped `storage` partition.

The 40 prey PNGs cover fly, cricket, moth, beetle, roach, mealworm, fruitfly,
and waxworm: two walk frames, two flee frames, and one dead frame per species.
The new silhouettes are an oval roach with long antennae, a segmented tan
mealworm, a small red-eyed fruitfly, and a plump pale waxworm.
Each dead frame is now a species-colored organic puddle with detached drops
or fragments. Tests reject intact-corpse regressions.

Eleven `achievement_*.png` badges have distinct silhouettes/material accents,
not repeated generic UI symbols. Ten are currently earnable; First Pet is
retained for legacy achievement-bit mapping only. The icon helper maps bits,
not list positions, so ordering changes cannot silently award the wrong badge.

The eight `background_{name}_0.png` images are carefully composed retro
top-down keeper enclosures: cork, moss, leaf litter (`leaf_litter`), sand,
fern, orchid, woodland, and minimal. Their fixed 240×240 dimensions are a
runtime contract: each is cached as RGB565A8 in PSRAM and the selected image
is enlarged 2× into the existing 480×480 RGB565 canvas. Eight cached sources
consume 1,382,400 bytes (about 1.38 MB); selecting a theme does not redraw
procedural scenery.

The art leaves a clean substrate or paper floor in the center, with bark
slabs, hollow cork tubes, climbing twigs, rooted foliage, and an anchored silk
retreat at the edges. Acrylic rims and ventilation slots establish the keeper
box. Texture stays inside defined objects using two or three flat material
tones, not gradients or randomly sprinkled noise. Minimal has folded paper,
a forked climbing twig, a small leafy sprig, and a silk sleeping retreat;
none are outside landscapes or blank geometric grids.

The five separate decor PNGs remain optional props; scene furniture is
already painted into the backgrounds and needs no automatic prop placement.
Keep prey and icons at 48×48, gifts at 32×32, and backgrounds at 240×240.
The gift source sprites now depict shelters, perches, a hammock, garden and
enrichment objects instead of disconnected jewels. Asset names/IDs remain
stable for saved items. Runtime enlarges the selected gift exactly 2x to 64x64
in a single reusable RGB565A8 PSRAM buffer; it does not use LVGL image transforms
or allocate a blending buffer per frame. Keep each 32px source on the existing
2px art grid. See [gift purposes and prices](../docs/GIFTS_CATALOG.md).

When adding art, update the generator and validator's expected file list and
dimensions. Add runtime mappings in `main\sprites.c` when an asset is consumed.
The ESP-IDF build regenerates `main\mmap_generate_storage.h`; that generated
header should not be edited by hand. See `docs\ART_STYLE.md` for the current
style and authoring rules.
