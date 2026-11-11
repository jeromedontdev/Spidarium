# Generated artwork

Edit [tools/gen_sprites.py](../tools/gen_sprites.py), not individual PNGs.
The [style contract](../docs/ART_STYLE.md) and
[SNES handoff](../docs/ART_HANDOFF_SNES.md) describe the current direction.

The deterministic set contains **337 RGBA PNGs**: 39 spider exports, 40 prey
frames, 30 UI icons, 11 achievement badges, 36 gifts, 68 placeable decor,
five legacy decor, 48 backgrounds, 12 dedicated treat icons and 48 matching
enclosure borders. All 201 previously published PNGs remain byte-identical.
Existing IDs and dimensions are stable; new catalogue entries append only.

## Shared SNES artwork

Backgrounds are 240x240 native-pixel terrain, enlarged exactly 2x to the
480x480 floor at runtime. Finite hue-shifted ramps, crisp texture clusters,
rimmed edge materials and restrained foliage replace smooth noise and grit.
Strong detail stays near the edges; the central 160x160 pixels use at most
three substrate tones with luminance span <=24/255. Each background is opaque
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
and bamboo/stone zen props. Borders have separately drawn family corner
silhouettes (books, gargoyle arches, wire/cogs, stars/panels, torii/bamboo,
fossil ribs, potions, leaves, mushrooms, runes and more), not only tinted trim.

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
determinism across two generator passes and preservation of all 201 source
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
