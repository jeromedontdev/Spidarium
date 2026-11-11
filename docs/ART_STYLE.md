# Spidarium art style

## Direction

**16-bit SNES-era pixel art**, with crisp clusters, hue-shifted material ramps,
top-left lighting, shaded volume and broken highlights. Not smooth noise,
vector shapes, filtered photographs, or flat 8-bit silhouettes.

The November 2026 reference is a nine-panel sheet of woodland, ruins, desert,
mushroom glade, autumn, snow, shore, crystal cave and pond scenes. Its useful
qualities are readable materials, stepped edges, rich foliage and shaded
objects. The game deliberately uses a **quieter, more open composition**:
the spider and player-placed decor must remain the focal points.
The reference was supplied in chat, not committed as a source asset.

Do not restyle the approved spider. This direction applies to every other
asset and to native/browser/website chrome; it does not authorize changes to
spider geometry, palettes or rendering.

## Pixel and palette contracts

| Group | Logical canvas | PNG / runtime size | Visible colors |
|---|---|---|---|
| Living prey | 24x24 | 48x48 | 9-12 |
| Prey splatters | 24x24 | 48x48 | 6-12 |
| UI icons | 24x24 | 48x48 | 5-12; simple arrows remain readable |
| Achievement badges | 24x24 | 48x48 | 8-12 |
| Dedicated treat icons | 24x24 | 48x48 | 8-12 |
| Gifts | 16x16 | 32x32; gifts displayed at 64x64 | 8-12 |
| Placeable decor | 32x32 | 64x64; runtime uses footprint radius | 8-12 |
| Legacy decor | Half its PNG dimensions | Existing dimensions unchanged | At most 12 |
| Backgrounds | 240x240 | 240x240, enlarged to 480x480 | At most 64 |
| Enclosure borders | 120x120 | 240x240, enlarged to 480x480 | At most 16 |
| Spider exports | 96x96 | 96x96 | At most 12; approved renderer unchanged |

Sprites use exact nearest-neighbor doubling: each aligned 2x2 block is uniform.
Backgrounds and spider exports use native single-pixel detail and are exempt
from that rule. All alpha is binary; backgrounds are fully opaque.
No smoothing, blur, continuous gradients or translucent shadows.

## Terrain: detail at the edges, breathing room in the middle

All twenty-four existing saved theme IDs remain unchanged; twenty-four append.
Backgrounds are low-profile
terrain, not pre-furnished rooms:

- Cork and woodland: warm substrate with rimmed bark chips.
- Moss and mushroom: green clearings with moss cushions; mushroom has tiny
  edge fungi, not a mandatory large mushroom house.
- Leaf litter: warm soil and separate red/gold leaf fragments.
- Sand, seashell and desert: sparse sand grains, stepped ripples and edge
  stone; shell fragments and desert shoots distinguish those themes.
- Fern and orchid: earthy or plum ground, with shallow fern/leaf banks and
  small orchid accents at the margins.
- Gothic and cemetery: quiet beveled paving, respectively violet and blue-gray.
- Succulent: teal gravel with small fleshy edge shoots.
- Willow: yellow-green bank with a shallow corner of rippled water.
- Crystal: violet mineral ground with embedded edge facets.
- Minimal: pale, sparsely marked substrate with very little edge detail.

The central **160x160 source pixels**, coordinates `[40,200)` on both axes,
use at most **three closely spaced substrate tones**, with luminance span
at most **24/255** (integer luma weights 299/587/114). On screen this protects
a 320x320 play area. Strong outlines, stones, foliage banks and accent colors
stay outside it. Fine center texture is clustered and low contrast, never
isolated bright/dark noise.

This is an intentional refinement of the former "ground texture only" brief:
small non-blocking terrain-edge vegetation is allowed, but large logs,
shelters, trees, crystals and other furniture remain optional placeable decor.
Painted edge materials do not add collision obstacles.

Use a finite palette of discrete material ramps. Do not revive the old advice
to target 200-900 colors, interpolate shading, add value-noise octaves or
increase contrast everywhere. Color count is a regression guard, not proof of
art quality: inspect the actual composition with the spider and placed items.

## Decor

All sixty-eight placeable sprites have 8-12 visible tones. Distinct material
surfaces matter more than simply adding colors:

- Rocks have lit upper faces, cool shadow planes, small chips and broken rims.
- The hollow log has bark grain, a cut rim and a dark opening; bark has
  paired light/dark furrows.
- Ferns have curved rachises with separate paired pinnae; not a grass star.
- Moss has overlapping cushions with short shoots; succulents have layered,
  pointed leaves, not a ring of green circles.
- Flowers and mushroom caps have hard-banded volume and material details.
- Shell ribs converge at the hinge; crystals have separate lit/shadow facets.
- Leaves, twigs and bone retain readable outlines and small surface accents.

Shading is clipped inside the silhouette. `_blob()` must not spill offset
highlight/shadow ellipses outside an ink rim. `_leaf_shape()` draws lit and
shadowed halves and a midrib. Top-left lighting is shared throughout.
Legacy `decor_*.png` derive from the same redesigned placeable art.

**Decor includes all thirty-six gift furnishings**, not only the sixty-eight
`decoritem_*.png` props. Gifts use the same top-left light, ink rims, shaded
wood/stone/foliage and 8-12 visible tones within their smaller 16x16 logical
canvas. Their stable filenames denote gameplay IDs, not literal jewels:
Crystal/Sapphire Lookouts and Royal Perch are furniture; Leaf Shelter,
Acorn Hide, Bone Tunnel and Diamond Retreat are usable-looking shelters.
Shared cut-wood platforms have grain and lit edges; gemstone accents have
facets, foliage has separate leaf planes, and silk has woven folds.

Never change catalogue IDs, ordering, footprint radii, blocking defaults or
save data for an art-only change. The C enum/catalogue, sprite mappings,
validator and browser catalogue must continue to agree.

## Appended catalogue and enclosure frames

The 34 appended props and 18 appended furnishings have separately authored
silhouettes: fairy doors and bridges, ironwork and urns, books and globes,
household sewing tools, witchcraft props, fossils and cycads, neon machinery,
space equipment, fantasy monuments, lost-city stonework and Japanese gardens.
The 24 appended backgrounds echo these themes through different architectural,
vegetation, celestial, water and circuit edge compositions, with the same
quiet-center contract. All original 201 PNGs and both spider renderers remain
unchanged.

Each of the 48 backgrounds has a separate `border_{theme}_0.png`. Borders are
transparent enclosure overlays, never backgrounds with an opaque center.
Their 120x120 logical pixels double without filtering to 240x240. The entire
source opening `[16,224)` on both axes must be alpha zero; only the outer
16-pixel band may contain ink/materials. Theme-specific vines, flowers,
masonry, shells, crystals, books, circuits, clouds and raked stone edge motifs
use binary alpha and at most 16 colors. Rotation is exact, never interpolated.
Fifteen separately authored corner silhouette families reinforce theme identity:
leaf boughs, fairy mushrooms, gothic gargoyle/arches, academia books, sewing
buttons/spools, witch potions/moons, fossil ribs, cyberpunk cog/wires, space
stars/solar panels, fantasy battlements/dragons, ancient stepped temples/runes,
zen torii/bamboo, shells, crystal shards and desert dunes/cacti. Corner
ornaments extend along both edges but never intrude into the transparent opening.
Material ramps likewise differ by family: cold steel/cyan and navy/magenta
are not shaded wood; gothic stone is cool with burgundy accents; academia
is oak/brass; household goods are porcelain/cloth; witchcraft is black-plum
and herb green; fossils are umber/amber; Japanese props are bamboo/stone.

Twelve dedicated treat icons use 24x24 logical pixels and 8-12 tones.
Legacy filename keys do not rename items: `hunger`, `mood`, `energy`, `dew`,
`pollen`, `silk` mean Juicy Fly, Silk Ribbon, Warm Pebble, Honey Moth,
Dew Drop and Cozy Nest respectively. The six appended keys are `moon`,
`amber`, `neon`, `star`, `lotus`, `rune`. Treats are explicitly fictional game
rewards; art must not imply real spider nutrition or animal-care advice.

## Prey, glyphs and badges

All forty prey frames now use discrete top-left highlights and lower-right
shadows on body, shell and wing clusters. Ink, legs, antennae, species sizes
and walk/flee silhouettes remain readable; no smoothing or random color noise.
All eight dead frames retain their species-colored puddle, detached droplets
and separate silhouettes, with a broken highlight and darker pooled edge.

All thirteen action/stat glyphs and eleven achievement badges share the
same cluster lighting. Keep their recognizable motifs and semantic colors:
mint hygiene, amber food, blue energy, gold XP, and individual badge accents.
`_shade_materials()` changes only exact selected material colors within
existing clusters; it never changes alpha, ink or unrelated details.

## Menu and website chrome

Native modal rows, setup, status, care cards, editor keys, sliders and minigame
tiles use flat palette fills, opaque two-pixel borders, two-pixel corner steps
and hard ink/pressed outlines. No gradient fills or blurred drop shadows.
Shared `ui_style_card()` owns the native card contract. Preserve touch sizes,
keyboard coverage, selection markers, destructive red and disabled navigation.
The care overlay may retain its intentional translucent fill; the folded ear,
physical viewport mask and approved spider are not redesigned.

Browser game chrome and the website use the same navy/mint/gold palette and
local Montserrat. Hard inset highlights are allowed; artwork is displayed
without recoloring or smoothing. Keep visible keyboard focus, readable labels,
mobile layouts and distinct selected/disabled/destructive states. This is a
pixel-art visual treatment, not an instruction to replace text with tiny
bitmap lettering or change game behavior.

## Generate, pack and verify

Edit [the generator](../tools/gen_sprites.py), not individual PNGs. Geometry is
pixel-snapped; procedural curves may calculate positions before snapping.
Random placement uses fixed per-theme seeds. No downloaded art or machine fonts.

From the repository root:

```powershell
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' -m unittest discover -s tools -p test_art_style.py -v
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\gen_sprites.py
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\asset_build.py --png-only
powershell -File build.ps1
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\asset_build.py
```

For a complete review sheet, add `--preview PATH.png` to the validator.
It lays out all 119 non-spider assets with nearest-neighbor sampling.

The validator checks 158 RGBA PNGs, dimensions/names, binary alpha, sprite grid,
palette limits, quiet centers, deterministic hashes across two passes, runtime
mappings, and every decoded QOI pixel in the existing pack. Every source PNG,
including backgrounds, badges and spider exports, must remain byte-identical
across ordinary validation runs. For an explicitly approved redesign, run the
edited generator first to establish the new source baseline; this does not
bypass any style or packed-pixel check. CI runs both art tests and source
validation before publishing the site.
The generator keeps an existing PNG byte-for-byte when its mode, dimensions
and decoded pixels already match. Different platform zlib encoders must not
silently replace approved spider/art files with equivalent compressed bytes.
Real pixel changes still rewrite the file and fail an ordinary preservation
check until explicitly generated as an approved redesign.

Build only with the wrappers. Never hand-edit the generated MMAP header.
After a browser build, run `tools\prepare_website.py --assets-only` to copy
the exact same PNGs into the distribution. Source browser rendering already
loads the root assets. Art changes need no new runtime buffers or per-frame objects.
See [the handoff](ART_HANDOFF_SNES.md) for hardware and visual acceptance.

## Spider preservation

`main/cspider.c` draws the approved 96x96 RGB565A8 spider.
`site/lib/spider-renderer.js` is its pixel port; the 39 `spider_*.png` are a
generated export, not the live device renderer. An art-only terrain/decor
change must leave all three byte-identical. Compare against pre-edit hashes,
not only hashes taken immediately before a validation run.

Both ports calculate in binary32 (`_f32()` / `Math.fround`); generator division
uses C truncation (`_idiv`). Preserve draw order: both eyes, both pupils, both
fangs. The browser test compares every pixel of all 39 exports to its renderer.
Do not replace these invariants with approximate visual similarity.
