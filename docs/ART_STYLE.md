# Spidarium art style

## Direction

**Modern hand-crafted pixel art**, with crisp clusters, hue-shifted material
ramps, top-left lighting, shaded volume and broken highlights. Not smooth
noise, vector shapes, filtered photographs, or flat 8-bit silhouettes.

"Modern pixel art" is the working reference rather than a named console
generation. The target is the standard used by contemporary indie games: real
per-material shading, deliberate lighting, and enough tonal depth that surfaces
read as objects rather than as flat fills. Naming specific commercial titles in
a generation prompt is also counterproductive, because image services reject
prompts that reference copyrighted works.

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
| Large climb structures | 192x192 | 192x192; drawn 1:1 at LVGL scale 256 | 8-12 |
| Legacy decor | Half its PNG dimensions | Existing dimensions unchanged | At most 12 |
| Backgrounds | 240x240 | 240x240, enlarged to 480x480 | At most 64 |
| Enclosure borders | 120x120 | 240x240, enlarged to 480x480 | At most 16 |
| Spider exports | 96x96 | 96x96 | At most 12; approved renderer unchanged |

Sprites use exact nearest-neighbor doubling: each aligned 2x2 block is uniform.
Backgrounds and spider exports use native single-pixel detail and are exempt
from that rule. All alpha is binary; backgrounds are fully opaque.
No smoothing, blur, continuous gradients or translucent shadows.

## Terrain: detail at the edges, breathing room in the middle

Crystal's user-approved PixelLab floor now retains richer violet mineral
texture across the whole canvas, without the oversized crystal/boulder rim.
It is imported unchanged from `assets/sources/crystal_floor.png`. Its exact
decoded pixels are hash-verified. The player subsequently approved the offline
moss trial and requested this texture-driven approach across the remaining
themes. Separate
border and decor layers remain independent. Crystal's separate PixelLab border
uses an explicit nearest-neighbor nine-slice fit to retain the standard thin
16px runtime rim and transparent opening; this pair and decor remain unchanged
while the other 47 floor/frame pairs are upgraded. See
[source provenance](../assets/README.md#approved-pixellab-crystal-floor).
Read [the PixelLab workflow](PIXELLAB_WORKFLOW.md) before generating more art.

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

The central **160x160 source pixels**,
coordinates `[40,200)` on both axes,
use at most **16 closely spaced substrate tones**, with luminance span
at most **64/255** (integer luma weights 299/587/114). Minimal retains 3/24.
The exact approved moss allows 17 tones/80 luminance span but is hash-pinned.
On screen this protects a 320x320 play area. Strong outlines, large stones,
foliage banks and accent colors
stay outside it. Fine center texture is clustered and low contrast, never
isolated bright/dark noise.

### Approved imports are exempt from the tone limits

A background may instead be imported unchanged from a reviewed source image
and pinned by pixel hash. Approved imports are verified exactly rather than
bounded by the tone limits above, because procedural drawing cannot reproduce
their texture and the hash already guarantees the published art cannot drift.

The approved **crystal** floor is the reference for what a richer import looks
like: 64 colors, 56 center tones, and a center luminance span of 142/255
against a mean of 65. Its richness comes from hue variation inside a narrow
luminance band, not from high contrast. Measured over the central 160x160 play
area it has a luminance standard deviation near 8 and a p10-p90 spread near 16,
which is why the spider stays readable on it despite the color depth.

Generated textures arrive far outside that band. `tools/import_background.py`
pulls them into it: it reduces to 240x240, fits luminance contrast to the
reference, retargets the mean, applies the shared top-left light, desaturates
toward the reference, and quantizes to 64 colors without dithering. Adopt a
source only after reviewing the converted result at native scale.

**Contrast is fitted, brightness is per theme.** A brick floor comes back far
contrastier than a snowfield, so a single contrast multiplier either flattens
the snow or leaves the brick too busy to read the spider against; the tool
searches for the factor that lands the central play area near the reference
standard deviation of 8.1. Brightness is not shared, though: forcing every floor
to crystal's mean of 65 would turn snow, sand and kitchen into mud. `THEME_MEAN`
holds the mean each procedural floor already shipped with, so an imported floor
keeps the brightness players associate with its theme.

All 48 shipped floors are now approved imports except **moss**, which keeps its
own pinned path in [terrain_art.py](../tools/terrain_art.py).

### Organic ground must not repeat

Repetition is correct for manufactured surfaces — brick, floorboards, tiling,
paving, panelling — and those themes may tile visibly. **Organic ground must
not.** Soil, moss, leaf litter, sand, gravel and ash must never show a repeating
unit, a grid, a honeycomb, or an evenly spaced field of similar shapes. Every
region of an organic background is unique, with mound sizes mixed across a wide
range. A regular cobblestone-like field is a defect on an organic theme even
when its palette and shading are otherwise correct.

Two failure modes recur when generating organic ground, and both are defects
even when the palette and shading are otherwise correct:

- **The bubble field.** Asking for "soft rounded cushions" produces a uniform
  field of similar rounded lumps, which reads as cobblestone. Ask for
  fine-grained, feathery or fractured structure instead, and name the defect in
  the avoid list.
- **The starburst.** Asking for "fine fronds" can produce radiating star-shaped
  clusters on a regular spacing, which is a repetition defect. Ask for an even
  mat of tiny shoots instead.

This is an intentional refinement of the former "ground texture only" brief:
small non-blocking terrain-edge vegetation is allowed, but large logs,
shelters, trees, crystals and other furniture remain optional placeable decor.
Painted edge materials do not add collision obstacles.

Use a finite palette of discrete material ramps. Do not revive the old advice
to target 200-900 colors, interpolate shading, add value-noise octaves or
increase contrast everywhere. Authoring uses named material recipes in
[terrain_art.py](../tools/terrain_art.py), not one recolored texture for every
theme. Substrate shading is materially specific: grooves in bark/wood,
short shingled moss ridges, sand ripples, chipped paving, woven cloth, panel
seams, crushed minerals and shallow water highlights. No per-frame artwork
generation or new display buffers. Color count is a regression guard, not proof of
art quality: inspect the actual composition with the spider and placed items.

## Decor

The approved overhaul starts with the original sixteen natural props, authored
in [core_decor_art.py](../tools/core_decor_art.py). Keep their 32x32 logical
canvases, exact doubling and one-pixel transparent gutter. Richer detail means
intentional cracks, bark grain, pinnae, overlapping growth, petal folds,
gills and shell ribs, not noise or extra outlines on every pixel. Review both
enlarged silhouettes and actual game-scale compositions on moss and Crystal.
The subsequent completed pass covers the other 52 props and all 36 gifts in
[furnishing_art.py](../tools/furnishing_art.py). Retain recognizable silhouettes
and separately shade connected material planes. Named details belong to the
object: book pages/bookmarks, bark grain, cloth folds, stone cracks, bamboo
nodes, equipment seams/rivets, cycad pinnae and bonsai cushions. Never use a
generic scattered highlight filter as a substitute for material anatomy.
Structural materials may use five finite tones; secondary materials retain
three each, with ink keeping the full sprite within 12 visible colors.

All sixty-eight placeable props have 8-12 visible tones, and the six large
climb structures share that budget at their larger canvas. Distinct material
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
Legacy `decor_*.png` derive from the same redesigned placeable art, including
the newer core-prop drawings.

### Large climb structures

The six climb structures (branch, vine, root, ivy, boulder, driftwood) are a
separate family authored at 192x192 and drawn 1:1 in game. They exist because a
64x64 prop cannot sell a climb: at that size the spider appears to hop. Keep
them large, keep them spanning most of the enclosure, and keep the same
top-left light, ink rims and 8-12 tone budget as the props.

**Their artwork is generated, not drawn procedurally.** The prompts live in
[climb_prompts.py](../tools/climb_prompts.py) and are run through nano banana
(`google/gemini-nano-banana-2.1`) via `tools/imagegen.py --provider openrouter`.
[import_climb.py](../tools/import_climb.py) keys the background, centres the
content, resizes to 192 with NEAREST and quantizes to 12 tones. `gen_sprites.py`
preserves the installed PNGs rather than redrawing them; `climb_art.py` is only
a fallback for a missing file. Do not hand-edit the PNGs.

Three prompt rules matter and were each learned from a failed generation:

- **Low three-quarter view.** The object needs a visible near face and a
  receding top surface. A flat top-down view is what made the first attempt read
  as a hop.
- **One continuous object.** It must read as a single solid structure with a
  walkable spine, not a pile of small pieces.
- **Framing.** The whole object must fit inside the canvas with a transparent
  margin. Without this the generator bleeds the object off all four edges and
  the ends are lost.

The importer also handles two generator quirks. It sometimes renders
"transparent background" as a literal grey checkerboard, which is detected and
keyed. And keying sets alpha without clearing the pixel's RGB, so the
background colour is still in the data: quantizing the cropped image spends
palette entries on colours nobody can see. A white-background source burned 8
of its 12 entries on near-white and shipped with only 4 visible tones. The
importer therefore builds the palette from the opaque pixels alone, then maps
every pixel to its nearest entry. Every climb structure must end up with 12
visible tones; `asset_build.py` rejects fewer than 8.

The lift is a **slope that runs up the screen**: the spider enters at the bottom
edge of the footprint and rises as it walks toward the top edge. Height is a
function of vertical position only. Ramping radially from the centre made the
spider rise from whichever side it approached, which read as being lifted onto
a platform rather than climbing a branch.

The lift alone is not enough to sell the climb. Three further cues are required,
and each was added in response to a specific report:

- **The body tilts with the surface.** `decor_climb_slope()` reports the
  gradient as a signed value and the renderer rotates the whole body by
  `atan(slope) * CLIMB_TILT_FOLLOW`, clamped to `CLIMB_TILT_MAX` (about 30
  degrees). A level spider on a ramp reads as a sprite sliding up the screen.
- **The viewing angle changes with height.** The forward axis compresses toward
  `CLIMB_FORESHORTEN` as the lift approaches `CLIMB_FULL_LIFT`, so the higher
  the spider climbs the more it is seen from the side rather than from above.
  Without this it keeps its flat top-down silhouette all the way up.
- **No shadow is cast onto the structure.** The contact shadow is drawn only
  when the spider is on the floor. A shadow at the floor point while climbing
  lands on the very thing being climbed, which is physically wrong and was the
  main reason the first 2.5D attempt failed. The structure's own artwork
  supplies the shading that grounds the spider.

### Climb structures are stylised props, not photographs

The first generated set was rejected as "almost too realistic", with the
driftwood singled out. The failure mode was photographic material rendering:
fine parallel grain, weathering speckle and per-fibre detail. The prompts now
carry `SIMPLIFY_RULE` and `SURFACE_RULE`, which cap each material at three or
four flat tones, forbid texture noise and fine hatching, and require the
walkable top to be a calm band of flat colour. Any detail smaller than about
four pixels is dropped. The target is the same flat-shaded language as the
64x64 props, not a rendering of real timber.

### Organic props must read as their subject, not as speckle

At a 32x32 logical canvas a one-pixel stroke is a sixteenth of the sprite, so a
scatter of them reads as noise rather than as material. Three rules follow:

- **A frond is a solid tapering blade, not a row of pinnae.** Individual pinnae
  are one or two pixels each, so a row of them merges into a speckled mass.
  `_frond()` draws a filled band along the spine, split into a lit upper half
  and a shadowed lower half.
- **A fern is an arching plant, not a starburst.** Fronds must leave the stem at
  different heights and curve outward. Five fronds radiating from one point is
  the "grass star" this contract forbids.
- **A succulent is a layered rosette, not a ring of pointed blades.** Leaves are
  short and plump with blunt tips (`_plump_leaf()`), drawn back to front so each
  layer overlaps the one behind it, over a solid body that gives the ink ring a
  continuous silhouette to follow. Long thin blades radiating from one point
  read as a spiky star.

Ink outlines are stroked onto the finished silhouette (`_outline()`) rather than
drawn per element. Per-element outlines are covered by the next element, which
leaves a dotted ring instead of a continuous edge.

**Decor includes all thirty-six gift furnishings**, not only the sixty-eight
`decoritem_*.png` props and the six large climb structures. Gifts use the same
top-left light, ink rims, shaded
wood/stone/foliage and 8-12 visible tones within their smaller 16x16 logical
canvas. Their stable filenames denote gameplay IDs, not literal jewels:Crystal/Sapphire Lookouts and Royal Perch are furniture; Leaf Shelter,
Acorn Hide, Bone Tunnel and Diamond Retreat are usable-looking shelters.
Shared cut-wood platforms have grain and lit edges; gemstone accents have
facets, foliage has separate leaf planes, and silk has woven folds.

### Gifts must read as their object at 16x16

The same "no speckle" rule applies, and it bites harder here because the canvas
is half the size. A gift is drawn as a solid body with a second material, never
as a set of thin strokes:

- **A bone is a shaft with two knuckled ends**, plus a leather thong so it is not
  one flat ivory mass.
- **A twig is a solid limb with a fork**, thickest at the base, with bark grain
  along the stem.
- **Coral is a branching colony**: tapered tubes with a lit upper face, a
  shadowed lower face and rounded polyp tips.
- **Sap is a resin run on a solid branch**: a connected bead chain, not a dotted
  line.
- **A ruby or sapphire is a cut stone**: table facet, crown facets and pavilion
  inside one ink silhouette, on a gold setting.
- **A crown is a band with three points** and a jewelled rim.
- **A silk hammock is a filled cloth curve** slung between two solid posts, with
  connected fold bands.
- **A crescent bed is a filled crescent polygon** with a mattress laid in its
  curve. `_oval()` draws an ink outline, so cutting a hole through one leaves
  only the outline behind.

Every gift needs at least eight visible tones, which in practice means a second
material: a setting, a thong, a platform, a soil bed.

Never change catalogue IDs, ordering, footprint radii, blocking defaults or
save data for an art-only change. The C enum/catalogue, sprite mappings,
validator and browser catalogue must continue to agree.

## Appended catalogue and enclosure frames

The 34 appended props and 18 appended furnishings have separately authored
silhouettes: fairy doors and bridges, ironwork and urns, books and globes,
household sewing tools, witchcraft props, fossils and cycads, neon machinery,
space equipment, fantasy monuments, lost-city stonework and Japanese gardens.
The 24 appended backgrounds echo these themes through materially specific
terrain and separate architectural, vegetation, celestial, water and circuit
trim. All47 non-Crystal floor/frame pairs are redrawn; the Crystal pair, all
non-terrain PNGs and both spider renderers remained unchanged during that
terrain batch. The later core-decor batch changes only the sixteen original
props and five derived legacy images. The final pass updates the other
181 sprites while preserving all approved terrain/core art and spider.

Each of the 48 backgrounds has a separate `border_{theme}_0.png`. Borders are
transparent enclosure overlays, never backgrounds with an opaque center.
Their 120x120 logical pixels double without filtering to 240x240. The entire
source opening `[16,224)` on both axes must be alpha zero; only the outer
16-pixel band may contain ink/materials. Theme-specific vines, flowers,
masonry, shells, crystals, books, circuits, clouds and raked stone edge motifs
use binary alpha and at most 16 colors. Rotation is exact, never interpolated.
Eight trim material families and separately drawn motifs reinforce theme identity:
leaf/fern sprigs, small mushrooms/petals, chipped arches/runes, books, shell
ribs/fossils, bamboo nodes, stitches, small crystalline accents and circuitry.
Each edge has its own deterministic motif offsets; it is not one rotated
repeating tile. No large corner monument or ornament may intrude into the opening.
Material ramps likewise differ by family: cold steel/cyan and navy/magenta
are not shaded wood; gothic stone is cool with burgundy accents; academia
is oak/brass; household goods are porcelain/cloth; witchcraft is black-plum
and herb green; fossils are umber/amber; Japanese props are bamboo/stone.

### Approved border imports

A border may also be imported from a reviewed source image and pinned by pixel
hash, exactly like a floor. The approved **crystal** frame is the reference:
chunky pale stone blocks with a lighter top face, a darker lower face, and
small crystal accents growing out of the band.

Unlike a floor, a frame is decorative, so it is allowed far more contrast than
the quiet play area a floor must provide. Crystal already runs at a luminance
standard deviation near 47 over its opaque pixels. `tools/import_border.py`
therefore treats contrast as a **ceiling** rather than a target: it reduces
contrast only until the rim comes in under `SD_CEILING` (80), so a generated
frame keeps the shading that makes it read as carved. Fitting to crystal's
exact sd instead flattens that shading into a flat strip.

The tool also has to solve a geometry problem the generator does not. It
usually draws a black margin around the frame and a band far wider than the
contract's 8 logical pixels. The tool crops to the drawn content, finds the
hollow middle by looking for the longest run of rows and columns that are
mostly empty, then nine-slices the band down to exactly 8 pixels and forces
everything inside the opening to alpha zero. That is the same geometry the
crystal frame uses, so a generated frame lands on the same grid.

All 48 shipped frames are now approved imports. Every theme, including
**crystal**, is registered in `APPROVED_BORDERS` and pinned by the SHA256 of
its published 240x240 pixels. Crystal used to be nine-sliced from a PixelLab
frame by `gen_sprites._draw_crystal_border()`; it is now a plain approved
import like the rest, so that special case is gone and `BORDER_SPECIAL` is
empty. **moss** keeps its own pinned path in
[terrain_art.py](../tools/terrain_art.py).

## Small-sprite translation

The same language must remain legible on a 16px gift or 24px icon/prey canvas.
Keep silhouettes, species anatomy, glyph meaning and animation timing; use
short connected interior highlights and cool shadow breaks, not decorative
noise or enlarged outlines. [small_art.py](../tools/small_art.py) records
explicit glyph/species detail paths and clips to opaque, non-ink pixels.
Gift furnishings retain 16px logical art and the existing runtime scale.
Do not silently expand dimensions, add translucent pixels or increase palettes
to imitate the larger props. Compare native-size thumbnails as well as zooms.

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

All thirty action/stat/settings glyphs and eleven achievement badges share the
same cluster lighting. Keep their recognizable motifs and semantic colors:
mint hygiene, amber food, blue energy, gold XP, and individual badge accents.
`_shade_materials()` changes only exact selected material colors within
existing clusters; it never changes alpha, ink or unrelated details.

## Menu and website chrome

### Dedicated icons: no reuse for different meanings

Every distinct feature, menu destination, item and achievement needs its own
recognizable icon in the approved SNES-era style. Do not reuse a generic icon
for unrelated entries, recolor the same glyph, or copy pixels under a new
filename as a substitute for dedicated artwork. Your Spider, Today and Diary,
for example, require different illustrations rather than shared status,
calendar or achievement placeholders.

The same action may retain its icon wherever it appears (such as Back or
Next); this consistency does not permit different actions to share one.
Apply the rule to native menus, the browser game and website. When adding a
feature, include its artwork and wire every relevant surface before calling
the icon work complete. Review silhouettes and meaning at actual menu size,
not just filenames or palette differences. Preserve the approved spider.

[companion_icon_art.py](../tools/companion_icon_art.py) authors the 28 dedicated
companion/Today/Sound icons as distinct silhouettes on the same logical grid.
The validator rejects duplicate decoded pixels and alpha silhouettes within
that set and against existing UI icons. Native menu tests also verify distinct
loaded indices for the new controls; inspect actual-size captures as well.

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

The circular website hero uses a quiet light parchment fill (`#e3dcc7`),
not a detailed habitat texture: both the gray spider and dark beetle must
remain distinct. Keep their source pixels unfiltered; the beetle displays at
48x48 above the spider's legs. This presentation-only backdrop does not
replace any in-game terrain.

## Generate, pack and verify

Edit [the generator](../tools/gen_sprites.py), not individual PNGs. Geometry is
pixel-snapped; procedural curves may calculate positions before snapping.
Random placement uses fixed per-theme seeds. Approved source imports are
hash-pinned; builds never download artwork or call a generation service.

From the repository root:

```powershell
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' -m unittest discover -s tools -p test_art_style.py -v
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\gen_sprites.py
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\asset_build.py --png-only
powershell -File build.ps1
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\asset_build.py
```

For a complete review sheet, add `--preview PATH.png` to the validator.
It lays out all354 non-spider assets with nearest-neighbor sampling.

The validator checks393 RGBA PNGs, dimensions/names, binary alpha, sprite grid,
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
