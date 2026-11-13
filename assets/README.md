# Generated artwork

Edit [tools/gen_sprites.py](../tools/gen_sprites.py), not generated PNGs.
The first sixteen core decor props now use hand-placed material geometry in
[core_decor_art.py](../tools/core_decor_art.py). The remaining 52 props, all 36
gifts and 12 treats use explicit per-item details from
[furnishing_art.py](../tools/furnishing_art.py); prey, icons and badges use
[small_art.py](../tools/small_art.py).
The 46 newly authored procedural theme pairs use
[terrain_art.py](../tools/terrain_art.py); the exact approved moss pair imports
[floor](sources/moss_floor.png) and [border](sources/moss_border.png).
Crystal imports its approved [floor](sources/crystal_floor.png) and selected
[border source](sources/crystal_border.png); never call PixelLab during builds.
The [style contract](../docs/ART_STYLE.md) and
[SNES handoff](../docs/ART_HANDOFF_SNES.md) describe the current direction.

The deterministic set contains **337 RGBA PNGs**: 39 spider exports, 40 prey
frames, 30 UI icons, 11 achievement badges, 36 gifts, 68 placeable decor,
five legacy decor, 48 backgrounds, 12 dedicated treat icons and 48 matching
enclosure borders. All 47 non-Crystal background/border pairs are upgraded.
The complete richer-material pass covers every non-spider category.
The final remaining-assets batch changes exactly 181 sprites, preserving
the 96 approved terrain images, 16 approved core props, five derived legacy
images and all 39 spider exports. Both live spider renderers remain unchanged.
Existing IDs and dimensions are stable; new catalogue entries append only.

## Core decor overhaul: first batch

The approved richer terrain direction now extends to the original sixteen
natural props. Rocks have irregular stepped planes, fissures and small moss
seams; wood has chipped bark ridges, recessed grain and a cut hollow opening.
Fern fronds use paired pinnae rather than bare branching sticks; moss has
overlapping short ridges, and succulent leaves overlap around a visible crown.
Flowers have folded, asymmetrical petal planes and clustered pollen. Mushroom
caps have a shaded underside/gills and a curved stalk. Shell ribs converge at
the hinge; crystals have cold/warm facets and fracture seams; leaves, twig
and bone have distinct silhouettes and material accents.

These are explicit shapes, not a global noise/detail filter or palette swap.
Each 32x32 logical drawing has a one-pixel transparent gutter, binary alpha
and 8-12 material colors, then doubles exactly to the existing 64x64 PNG.
Legacy props derive from the same five corresponding redrawn sources.
No catalogue ID, price, radius, blocking default, save data or runtime buffer
changes. The remainder is now integrated through the material pass below.
Builds remain offline and spend no PixelLab generations.

## Remaining props, furnishings and small sprites

[furnishing_art.py](../tools/furnishing_art.py) has explicit detail recipes
for every remaining prop, gift and treat. It preserves recognizable catalogue
geometry while replacing image-wide diagonal speckles with connected material
planes and individually positioned construction details. Each disconnected
surface receives its own top-left light; structural ramps have five finite
tones, accent and secondary ramps three each, plus ink (at most 12 colors).
Older natural props/gifts retain their finite palettes and gain named,
alpha-clipped cuts, folds, ribs, grain and facets rather than a noise filter.

Cycad pinnae, bonsai cushions/trunk, book pages, bookmarks, masonry cracks
and equipment rivets have dedicated geometry. Potted fern, mushroom pair,
flower planter, moss stone and crystal cluster reuse the approved core
anatomy. Whole mushroom/crystal composites retain their core palettes; mixed
composites explicitly assign core material colors to the destination's finite
palette before compositing. This is offline authoring, not runtime quantization.
Validate the resulting pixels and inspect every composition, not just colors.

Gifts stay on their 16x16 logical grid, doubled to the existing 32x32 PNG
and displayed by the existing 64x64 canvas. At this scale use a few readable
folds or construction marks; do not cram 32px prop detail into 16px furniture.
Small prey/icons/badges retain silhouettes, ink, glyph meaning and animation:
[small_art.py](../tools/small_art.py) adds named interior highlights using their
existing finite palettes. Shared light ramps shift warmer, shadows cooler.
Detached splatter droplets and all eight species' gait/leg contracts remain.
No gameplay footprint, menu layout, persistent allocation or save ABI changes.

After an edit, run the focused art regressions, full packed-pixel validation,
and compare all sprites at game scale. Source equality, explicit recipe
coverage, alpha/ink clipping, determinism and distinct content are tested.
Color counts are necessary constraints, not proof of subjective art quality.

## PixelLab workflow and offline preparation

The user approved the offline moss trial and requested this approach for all
remaining themes. This catalogue redraw spends no PixelLab generations.

Read [Getting good PixelLab results](../docs/PIXELLAB_WORKFLOW.md) for the
tested reference/prompt recipes, cost bands, separate floor/border geometry
and reproducible installation workflow. Start with a single chosen theme,
review before further spending, and never change the approved spider.

The detail experiment preserves existing background/frame designs and the
approved spider. [pixellab_prepare.py](../tools/pixellab_prepare.py) prepares
one prompt brief, an optional protected-region mask, or validates an external
candidate. It makes **no network calls**, handles no credentials, spends no
generations and never installs candidates. Stage outputs outside the repository.

```powershell
python tools\pixellab_prepare.py brief --kind background --theme moss --action edit --reference C:\Art\original-reference.png --output C:\Art\moss-brief.json
python tools\pixellab_prepare.py brief --kind border --theme moss --action inpaint --reference C:\Art\original-reference.png --output C:\Art\moss-border-brief.json
python tools\pixellab_prepare.py mask --kind background --output C:\Art\moss-mask.png
python tools\pixellab_prepare.py mask --kind border --output C:\Art\moss-border-mask.png
python tools\pixellab_prepare.py validate C:\Art\candidate.png --kind background --theme moss
```

The original nine-panel reference (woodland, ruins, desert, mushroom, autumn,
snow, beach, crystal cave and pond) is not stored here. Supply the actual image;
`--draft-without-reference` explicitly produces an incomplete draft instead.
Briefs identify both the existing source and separate style reference by path
and hash, without embedding or uploading either image.

Start with **one chosen-background request**, inspect it with the existing spider
and furnishings, then consider a matching border. Do not batch all 48 pairs.
The user reported 40 trial generations: one request is **not** assumed to cost
one generation. Confirm API trial access, exact debit, single-output/grid
settings, current request schema and reference roles using the
[official endpoint list](https://www.pixellab.ai/pixellab-api) before submission.
The suggested edit/inpaint endpoints are not executable API payloads. Masks
use white for editable and black for protected locally; verify the endpoint's
mask polarity/encoding before use. No automatic retries: an ambiguous timeout
must be reconciled against the provider's job/billing status before resubmission.
Never put API keys in source, briefs, logs or command-line arguments.

Technical validation is not visual approval. Reject wrong canvases, unapproved noisy
centers, antialiased pixels and filled frame openings; do not silently resize,
quantize or erase them to make checks pass. More detail belongs in material
planes and edge clusters, not a busier playable center. A later approved
integration must preserve reproducibility: the current generator would
overwrite hand-replaced PNGs. Crystal is now integrated through the approved
source import below; all other candidates still require explicit integration.

## Approved PixelLab Crystal floor

The selected floor is PixelLab Pro job `4542b745-e8ec-427a-b86f-4bc0c09ecbce`,
seed `861115`, a 240x240 opaque RGBA PNG with 64 colors. The user approved its
rich violet mineral texture and requested no large baked-in border scenery.
The source file SHA256 is
`7a19cb8a21c148ca506809d4494948778725d148039ea4e8720359e115f3d88f`.
The generator checks its decoded-pixel hash before copying it to
`background_crystal_0.png`, so builds are offline and reproducible.
Runtime still doubles native pixels to 480x480 with no new buffers.

Crystal supersedes the former three-tone-center requirement. Validation pins
the exact approved pixels instead of accepting arbitrary detailed/noisy centers.
Other themes now follow the bounded textured-center policy below. The separately
selectable Crystal border now imports its own PixelLab source below; it never
modifies the floor. All other artwork and the approved spider are
unchanged. Background ID 14, catalogue prices, ownership and save layout stay
stable; selecting an unowned Crystal still uses the existing 48-Silk purchase.

## PixelLab Crystal border

Pro job `2f18c1f6-972f-4386-878a-8a7ad07fd42d`, seed `861116`, returned four
120x120 RGBA candidates for the final 10 trial generations (40 used, 0 remaining).
Candidate index 0 supplies understated violet/slate mineral edging and small
lavender/cyan crystals. Its raw image is retained as
[crystal_border.png](sources/crystal_border.png), with decoded RGBA SHA256
`7ea7c587861a0844e9296a01b01a22859f631f5ac1d7602af157081bdeac9c09`.

All four candidates ignored the requested 8px logical rim. Candidate 0's rim
extends 18px inward, leaving an 84x84 transparent opening. It did **not** pass
the runtime contract unchanged. The generator explicitly nine-slices source
edges `(0,18,102,120)` onto `(0,8,112,120)` with nearest-neighbor sampling,
then doubles logical pixels to 240x240. This compresses edge/corner thickness
without clipping inward tips or painting new colors. The final 208x208 opening
is transparent, binary alpha and all 16 source colors remain, with uniform
aligned 2x2 blocks. Final decoded RGBA SHA256 is
`e6160c76ed9bc30aea45595ba507e4a5155e55f9687c49382702e23d314c6e1a`.

This explicit source-derived fit is documented, tested and hash-pinned; do not
use arbitrary resizing/erasure to hide later generation failures. Only
`border_crystal_0.png` changed for that installation. The later offline redraw
preserves this pair. Border ID 14, prices, ownership/save layout and spider remain unchanged.

## Approved offline moss and full terrain redraw

The selected moss trial is retained exactly, not approximated by recoloring
another theme. Its opaque 240x240 floor has 17 colors with a center luminance
span of 75/255; its separate 240x240 frame has 12 colors, binary alpha,
uniform 2x2 blocks and a transparent `[16,224)` opening. Both are checked
against their approved decoded-pixel hashes in the generator/validator.

The remaining 46 non-Crystal pairs use explicit named recipes in
[terrain_art.py](../tools/terrain_art.py): chipped bark, overlapping moss,
leaf litter, mineral/gravel, sand/dunes, snow, mud/banks, submerged masonry,
beveled paving/flagstones/tiles, wooden boards, woven cloth, metal panels,
regolith, ash, raked sand and cloud texture. Each theme has a fixed seed,
substrate/secondary ramps and matching trim material/motif. No filtered noise
or continuous pixel gradients. Motifs include leaves/ferns, mushrooms, petals,
shells/fossils, chipped runes/arches, books, bamboo, stitches and circuitry.
Backgrounds stay free of large mandatory furniture; borders occupy only the
standard outer 16 runtime pixels, never a larger baked-in scene.

The central 160x160 pixels allow at most 16 finite substrate tones and
luminance span <=64/255. Minimal deliberately retains its 3-tone/24 limit.
Approved moss allows 17/80 but is hash-pinned; approved Crystal remains pinned
to its richer original. All backgrounds stay opaque and at most 64 colors;
all frames stay at most 16 colors with binary alpha and an open 208x208 center.
This supersedes the old universal three-tone-center brief, following explicit
approval of the moss treatment. Validate contrast in composed scenes too.

## Shared SNES artwork

Backgrounds are 240x240 native-pixel terrain, enlarged exactly 2x to the
480x480 floor at runtime. Finite hue-shifted ramps, crisp texture clusters,
rimmed edge materials and restrained foliage replace smooth noise and grit.
Large scenery stays optional; richer low-relief material texture can cover
the center within the per-theme bounds above. Each background is opaque
and uses at most 64 colors. Large furniture remains optional player decor.

Sixty-eight `decoritem_*.png` props are authored at 32x32 and doubled to 64x64,
with 8-12 visible material tones, ink rims, lit/shadow planes and surface
details. The five legacy `decor_*.png` derive from the same art at their
unchanged dimensions.

All thirty-six `gift_*.png` furnishings also use 8-12 material tones, with the
same shaded wood, stone, foliage, facets and ink rims. This includes every
shelter, perch, hammock, garden and toy, not only the separate decor catalogue.
All forty prey frames, thirty UI glyphs and eleven achievement badges
share discrete material lighting, without changing their recognizable motifs,
species IDs. Living insects have six fixed thoracic leg attachments with
alternating tripod feet; cricket hind legs have enlarged jointed femurs.
Larvae have short true legs near the head, not long legs along the abdomen;
waxworm prolegs remain short. Bodies/heads stay anchored between frames.
Both runtimes orient the right-facing art to travel and stop ground gait while
resting; wings can keep beating. Living prey require 9-12 visible tones,
splatters 6-12, icons 5-12 and badges 8-12. Simple arrows are deliberately
less elaborate than furnishings. Sprites use uniform aligned
2x2 blocks and binary alpha. Prey/icons are 48x48;
gifts are 32x32 and use the existing reusable 64x64 PSRAM display buffer.
No filtered resizing, blur, gradients or soft shadows.

The appended catalogue spans woodland fairy, gothic, academia, household,
witchy, prehistoric, cyberpunk, science fiction, fantasy, lost civilization
and Japanese zen themes. Props and furnishings have distinct silhouettes,
not recolored copies. Every `border_{background-key}_0.png` matches its
background: native-size 240x240 RGBA, nearest-neighbor doubled logical pixels,
at most 16 material tones and binary alpha. Frame detail occupies only the
outer 16 pixels; the entire `[16,224)` square is transparent.

Fifteen material families separate cool steel/cyan space equipment, navy and
magenta cyberpunk, cold stone/burgundy gothic, oak/brass academia, porcelain
and cloth household goods, black-plum/herb witchcraft, umber/amber fossils
and bamboo/stone zen props. Current borders use eight trim material families
with separately drawn leaves/ferns, petals, mushrooms, shell/fossil ribs, books,
arches/runes, bamboo nodes, stitches and circuit motifs, not only tinted trim.

Dedicated 48x48 `treat_*.png` retain the existing ID/key order:
`hunger` = Juicy Fly, `mood` = Silk Ribbon, `energy` = Warm Pebble,
`dew` = Honey Moth, `pollen` = Dew Drop, `silk` = Cozy Nest.
The appended `moon`, `amber`, `neon`, `star`, `lotus`, `rune` depict Moon Mousse,
Amber Drop, Neon Jelly, Star Wafer, Lotus Nectar and Rune Biscuit.
These are fictional game rewards, not real animal-care recommendations.

## Do not touch the approved spider

`main/cspider.c` draws the live 96x96 RGB565A8 spider.
`site/lib/spider-renderer.js` is its pixel port. The 39 `spider_*.png` are
faithful exports of the same geometry, palette, draw order and binary32 math.
Non-spider art/chrome work must leave all these byte-identical. Spider PNGs are
exempt only from the sprite grid rule; their alpha/palette/determinism checks
still apply. Browser tests compare all 39 exports pixel for pixel.

## Generate and validate

From the repository root:

```powershell
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' -m unittest discover -s tools -p test_art_style.py -v
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\gen_sprites.py
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\asset_build.py --png-only
powershell -File build.ps1
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\asset_build.py
```

Validation checks the exact file set, dimensions, PNG/RGBA format, style,
determinism across two generator passes and preservation of all 337 source
PNGs. For an approved redesign, generate first
to establish its new source baseline. `--png-only` skips a stale pack, not
preservation or runtime mapping checks.
Add `--preview PATH.png` to write a review sheet of all 298 non-spider assets.
The publication workflow runs the art tests and source validation.

The build wrapper packs QOI into the memory-mapped `storage` partition.
Default validation decodes each packed entry and compares every pixel to its
PNG. `--packed PATH` validates an explicit pack. Never hand-edit the
generated MMAP header. Update generator, expected names/dimensions and runtime
mappings together when adding an asset.

After building the browser, copy the same source art into its distribution:

```powershell
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\prepare_website.py --assets-only
```
