# Asset inventory

The generator and validator define a deterministic set of 134 RGBA
PNGs in `assets\`. ESP-IDF packs them into the `storage` partition as QOI. At
runtime `assets.c` returns persistent image descriptors for the memory-mapped
assets. `assets.c` decodes the QOI images once into RGB565A8 buffers in PSRAM;
renderers keep their LVGL objects and change sources only when an animation
frame or gift changes. Non-background images use about 1.6 MB of
PSRAM; eight opaque background sources add 1,382,400 bytes (about 1.38 MB).

| Group | Files | Logical → native dimensions | Runtime use |
|---|---:|---|---|
| Spider | 39 | Preserved 96×96 | Spider sprite table/setup; existing artwork preserved |
| Prey | 40 | 24×24 → 48×48 | Fly, cricket, moth, beetle, roach, mealworm, fruitfly, and waxworm walk/flee/dead clips |
| UI icons | 13 | 24×24 → 48×48 | Native-size HUD actions, stat icons, and back/lock |
| Achievement badges | 11 | 24×24 → 48×48 | Unique badge for each persisted achievement bit; ten currently earnable |
| Gifts | 18 | 16×16 → 32×32 | Placed furniture enlarged to 64px; shop explains purpose and price |
| Terrarium decor | 5 | Half native → native (below) | Optional two rocks, fern, succulent, and cut log |
| Enclosure backgrounds | 8 | 120×120 → 240×240 | Cached keeper art, selected source enlarged 2× into 480×480 RGB565 canvas |

The 95 non-spider assets are authored pixel art, enlarged exactly 2× using
nearest-neighbor sampling. Every aligned 2×2 RGBA block is identical, alpha is
binary (0/255), with fully opaque backgrounds, and each image has at most
12 visible colors. The 39 spider
PNGs and their generator are exempt from this new contract and remain unchanged.
Living fly/cricket/moth/beetle PNGs also remain byte-identical; species
are appended after those four to preserve their IDs. Each species has two
walk frames, two flee frames, and one dead frame. Roach uses an oval body and
long antennae; mealworm is narrow, segmented, and tan; fruitfly is a small
red-eyed winged insect; waxworm is plump and pale with a brown head.

Prop native sizes are 48×32 and 40×28 (rocks), 48×64 and 52×56 (plants),
and 96×36 (log). These PNGs remain available in the asset library without
mandatory placement in any scene. Eight authored background PNGs depict
top-down physical keeper enclosures: cork, moss, leaf litter, sand, fern,
orchid, woodland, and minimal. Each preserves a clean central floor, with
convincing cork bark/tubes, climbing twigs, coherent botanical leaves/fronds,
an anchored silk sleeping retreat, and restrained acrylic rim/vent details.
Minimal uses paper substrate and a small climbing/retreat arrangement, not
an empty grid. Textures are concentrated inside objects in two or three flat
tones; there are no gradients, random floor noise, or outside landscapes.

Background source dimensions are exactly 240×240. The renderer caches eight
RGB565A8 sources and scales the selected image 2× into its existing 480×480
RGB565 PSRAM canvas. Background art is not synthesized by runtime procedural
pixel functions. The 115 earlier accepted images remain byte-identical,
including all 39 spider and 32 living prey frames. Eight dead-prey images
are deliberately changed into species-colored organic splatters.
Gift sources remain 32x32 with stable IDs/filenames but are redesigned as
functional habitat furniture and displayed at 64x64 using a reused PSRAM
RGB565A8 canvas. The shop uses labels/effects; the placed item consumes the art.

## Filenames

- Spider: `spider_{e,n,s}_{walk,idle,eat,sleep,pounce,molt}_{frame}.png`
- Prey: `prey_{fly,cricket,moth,beetle,roach,mealworm,fruitfly,waxworm}_{walk,flee}_{0,1}.png` and
  `prey_{species}_dead_0.png`
- HUD: `ui_{feed,clean,play,sleep,decor,status,back,hunger,mood,energy,hygiene,xp,lock}.png`
- Achievements: `achievement_{first_meal,first_pet,first_clean,first_play,first_hunt,first_molt,ten_meals,adult,level_5,survived_sick,first_perfect}.png`
- Gifts: `gift_{pebble,leaf,twig,acorn,moss,sap,flower,crystal,amber,coral,silk,bone,pearl,ruby,sapphire,emerald,diamond,crown}.png`
- Decor: `decor_rock_0.png`, `decor_rock_1.png`, `decor_plant_0.png`,
  `decor_plant_1.png`, `decor_wood_log.png`
- Backgrounds: `background_{cork,moss,leaf_litter,sand,fern,orchid,woodland,minimal}_0.png`

`tools\asset_build.py` validates every filename, count, image size, PNG format,
RGBA mode, repeat-generation hash, preservation of 115 earlier accepted
PNGs, and the exact non-spider 2×2 grid/binary-alpha/12-color limit. Backgrounds
also require 240×240 dimensions and opaque alpha. It retains MMAP table and
decoded QOI pixel validation for packed assets. Use `--png-only` to validate
source art before rebuilding a stale pack; runtime mapping checks remain
enabled. Use `--packed PATH` afterward. See
`docs\ART_STYLE.md` and `assets\README.md` for the authoring workflow.
