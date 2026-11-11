# Asset inventory

The generator and validator define a deterministic set of **158** RGBA PNGs in
`assets\`. ESP-IDF packs them into the `storage` partition as QOI. At runtime
`assets.c` returns persistent image descriptors for the memory-mapped assets and
decodes each QOI image once into an RGB565A8 buffer in PSRAM; renderers keep
their LVGL objects and change sources only when an animation frame or a placed
item changes.

| Group | Files | Logical → native dimensions | Runtime use |
|---|---:|---|---|
| Spider | 39 | 96×96 | Generated export of `main/cspider.c`; the device draws the spider procedurally, not from these PNGs |
| Prey | 40 | 24×24 → 48×48 | Fly, cricket, moth, beetle, roach, mealworm, fruitfly, and waxworm walk/flee/dead clips |
| UI icons | 13 | 24×24 → 48×48 | Native-size HUD actions, stat icons, and back/lock |
| Achievement badges | 11 | 24×24 → 48×48 | Unique badge for each persisted achievement bit; ten currently earnable |
| Gifts | 18 | 16×16 → 32×32 | Placed furniture enlarged to 64px; shop explains purpose and price |
| Placeable decor | 16 | 32×32 → 64×64 | Bought with Silk, placed by the player, drawn at `radius × 4` px |
| Legacy decor | 5 | Half PNG size → unchanged PNG dimensions | Derived from matching redesigned placeable props |
| Enclosure ground textures | 16 | 240×240 → 240×240 | Ground texture only, enlarged 2× into the 480×480 RGB565 canvas |

The 103 non-spider sprites are authored pixel art, enlarged exactly 2× using
nearest-neighbour sampling. Every aligned 2×2 RGBA block is identical, alpha is
binary (0/255), and each sprite has at most 12 visible colours. The 39 spider
PNGs are exempt from the 2×2 grid rule because they are a direct export of
`main/cspider.c`, which writes single pixels into a 96×96 canvas; forcing them
onto a 2×2 grid would make the published art disagree with the spider the panel
draws. They are still checked for binary alpha, determinism and the 12-colour
limit. This art release leaves both renderers and all 39 exports byte-identical.
Species are appended after fly/cricket/moth/beetle to preserve their IDs;
all eight species now share the refreshed material shading. Each has two walk
frames, two flee frames, and one dead frame. Roach uses an oval body and long
antennae; mealworm is narrow, segmented, and tan; fruitfly is a small red-eyed
winged insect; waxworm is plump and pale with a brown head.

## Art direction

Every non-spider asset follows the **16-bit SNES-era** contract in
[ART_STYLE.md](ART_STYLE.md): crisp material clusters, finite hue-shifted ramps,
top-left lighting, ink rims, shaded volume and surface detail. Placeable decor
and all eighteen gift furnishings use 8-12 visible tones. No gradients,
translucent shadows or blur. Living prey use 9-12 tones, dead puddles 6-12,
icons 5-12 and badges 8-12. Their silhouettes, recognizable motifs and gameplay
IDs are retained; only the approved spider remains unchanged.

## Enclosure ground textures

Backgrounds are low-profile terrain with restrained edge materials: bark
chips, leaf fragments, stone, shallow foliage banks, sand ripples and mineral
facets. Large furniture remains optional placeable decor. The central 160x160
source pixels use at most three substrate tones with luminance span <=24/255,
leaving a 320x320 screen area quiet for the spider and player arrangements.
Smooth noise, vignettes and high-contrast grit have been removed.

Background source dimensions are exactly 240×240 and fully opaque. The renderer
caches sixteen RGB565A8 sources and scales the selected image 2× into its
existing 480×480 RGB565 PSRAM canvas. Background art is not synthesized by
runtime procedural pixel functions. Backgrounds are exempt from the 12-colour
sprite cap and the 2×2 uniformity rule because they use native single-pixel
detail. They have their own 64-color maximum and quiet-center checks.

## Placeable decor

Sixteen kinds at 32×32 logical, 64×64 native. Each has a footprint radius and a
blocking flag; the renderer draws it at `radius × 4` pixels so the art and the
collision circle agree. The catalogue order is load-bearing and must agree
across `decor_kind_t` in `main/decor.h`, `k_decor_kinds[]` in `main/decor.c`,
`k_decor_items[]` in `main/sprites.c`, `DECOR_ITEMS` in `tools/asset_build.py`,
and `DECOR_KINDS` in `site/lib/device-engine.js`.

Gift sources remain 32×32 with stable IDs/filenames and are displayed at 64×64
using a reused PSRAM RGB565A8 canvas. The shop uses labels/effects; the placed
item consumes the art.

## Filenames

- Spider: `spider_{e,n,s}_{walk,idle,eat,sleep,pounce,molt}_{frame}.png`
- Prey: `prey_{fly,cricket,moth,beetle,roach,mealworm,fruitfly,waxworm}_{walk,flee}_{0,1}.png` and
  `prey_{species}_dead_0.png`
- HUD: `ui_{feed,clean,play,sleep,decor,status,back,hunger,mood,energy,hygiene,xp,lock}.png`
- Achievements: `achievement_{first_meal,first_pet,first_clean,first_play,first_hunt,first_molt,ten_meals,adult,level_5,survived_sick,first_perfect}.png`
- Gifts: `gift_{pebble,leaf,twig,acorn,moss,sap,flower,crystal,amber,coral,silk,bone,pearl,ruby,sapphire,emerald,diamond,crown}.png`
- Placeable decor: `decoritem_{rock_small,rock_large,pebbles,log,bark,fern,moss_clump,succulent,flower_white,flower_pink,mushroom,shell,crystal,leaf_pile,twig,bone}.png`
- Ground textures: `background_{cork,moss,leaf_litter,sand,fern,orchid,woodland,minimal,mushroom,gothic,seashell,succulent,willow,desert,crystal,cemetery}_0.png`

`tools\asset_build.py` validates every filename, count, image size, PNG format,
RGBA mode, repeat-generation hash, preservation of all 158 source PNGs,
and the sprite 2×2 grid/binary-alpha/12-colour limit. Placeable decor and gift
furnishings require at least eight tones. Backgrounds require 240×240 dimensions,
opaque alpha, at most 64 colors and quiet centers. It retains MMAP table and decoded
QOI pixel validation for packed assets. Use `--png-only` to validate source art
before rebuilding a stale pack; runtime mapping checks remain enabled.
`--preview PATH.png` writes the complete non-spider review sheet. Approved
redesigns explicitly regenerate the source baseline before validation.
