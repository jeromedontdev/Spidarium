# Asset inventory

The generator and validator define a deterministic set of **158** RGBA PNGs in
`assets\`. ESP-IDF packs them into the `storage` partition as QOI. At runtime
`assets.c` returns persistent image descriptors for the memory-mapped assets and
decodes each QOI image once into an RGB565A8 buffer in PSRAM; renderers keep
their LVGL objects and change sources only when an animation frame or a placed
item changes.

| Group | Files | Logical → native dimensions | Runtime use |
|---|---:|---|---|
| Spider | 39 | Preserved 96×96 | Spider sprite table/setup; existing artwork preserved |
| Prey | 40 | 24×24 → 48×48 | Fly, cricket, moth, beetle, roach, mealworm, fruitfly, and waxworm walk/flee/dead clips |
| UI icons | 13 | 24×24 → 48×48 | Native-size HUD actions, stat icons, and back/lock |
| Achievement badges | 11 | 24×24 → 48×48 | Unique badge for each persisted achievement bit; ten currently earnable |
| Gifts | 18 | 16×16 → 32×32 | Placed furniture enlarged to 64px; shop explains purpose and price |
| Placeable decor | 16 | 32×32 → 64×64 | Bought with Silk, placed by the player, drawn at `radius × 4` px |
| Enclosure ground textures | 16 | 240×240 → 240×240 | Ground texture only, enlarged 2× into the 480×480 RGB565 canvas |

The 119 non-spider sprites are authored pixel art, enlarged exactly 2× using
nearest-neighbour sampling. Every aligned 2×2 RGBA block is identical, alpha is
binary (0/255), and each sprite has at most 12 visible colours. The 39 spider
PNGs and their generator are exempt from this contract and remain unchanged.
Living fly/cricket/moth/beetle PNGs also remain byte-identical; species are
appended after those four to preserve their IDs. Each species has two walk
frames, two flee frames, and one dead frame. Roach uses an oval body and long
antennae; mealworm is narrow, segmented, and tan; fruitfly is a small red-eyed
winged insect; waxworm is plump and pale with a brown head.

## Art direction

The whole set follows a **16-bit SNES-era** contract, documented in
[ART_STYLE.md](ART_STYLE.md): crescent shading rather than concentric rings,
three tones per material (shadow, body, highlight), an ink rim on every
silhouette, and no gradients, translucent shadows or blur. Two tones read flat;
the third tone is the entire difference between an 8-bit look and a 16-bit one.

## Enclosure ground textures

Backgrounds are **ground texture only**. Each of the sixteen themes is the
substrate a keeper would actually use — cork, moss, leaf litter, sand, coir,
bark chip, gravel — seen from directly above, with a meandering trail and fine
grit. There are no canopies, pebble scatter, flower clusters or fronds.

This is deliberate. Earlier rounds painted furniture into the background, and
the result was cluttered: the spider had poor contrast against its own floor and
every enclosure looked like the same scene recoloured. Props are now placeable
decor the player buys and positions.

Background source dimensions are exactly 240×240 and fully opaque. The renderer
caches sixteen RGB565A8 sources and scales the selected image 2× into its
existing 480×480 RGB565 PSRAM canvas. Background art is not synthesized by
runtime procedural pixel functions. Backgrounds are exempt from the 12-colour
cap and the 2×2 uniformity rule because they are illustrations authored at the
panel's native resolution, not sprites.

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
RGBA mode, repeat-generation hash, preservation of 123 earlier accepted PNGs,
and the exact non-spider 2×2 grid/binary-alpha/12-colour limit. Backgrounds also
require 240×240 dimensions and opaque alpha. It retains MMAP table and decoded
QOI pixel validation for packed assets. Use `--png-only` to validate source art
before rebuilding a stale pack; runtime mapping checks remain enabled.
