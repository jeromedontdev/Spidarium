# Spidarium v1.1.22

## Twice the habitat catalogue

Preserving every existing ID and all 201 earlier PNGs, this release doubles:

| Catalogue | Previously | Now |
|---|---:|---:|
| Placeable decorations | 34 | 68 |
| Treats | 6 | 12 |
| Gifts | 18 | 36 |
| Backgrounds | 24 | 48 |

New pixel-art material palettes distinguish porcelain, brass, cold stone,
bamboo, amber, neon circuits and spacecraft steel. Background centers stay
quiet so furnishings and the unchanged spider remain readable.

| Theme | New backgrounds | Example new decor and gifts |
|---|---|---|
| Woodland fairy | Fairy Glade, Moonflower Grove | Fairy Ring, Pixie Door, Fairy Cottage, Petal Swing |
| Gothic | Cathedral, Rose Courtyard | Gargoyle, Iron Gate, Rose Urn, Moon Bed, Velvet Throne |
| Academia | Library, Observatory | Book Stack, Globe, Quill Desk, Reading Nook, Scholar Perch |
| Household | Kitchen, Sewing Room | Tea Cup, Spool, Thimble, Teapot Hide |
| Witchy | Witch Garden, Moon Altar | Cauldron, Broom, Potion Rack, Witch Hat, Potion Hide |
| Prehistoric | Primeval Jungle, Amber Basin | Amber Fossil, Cycad, Dinosaur Egg, Fern Nest, Fossil Cradle |
| Cyberpunk | Neon Alley, Circuit Garden | Neon Sign, Holo Terminal, Cable Coil, Neon Den |
| Sci-fi | Lunar Base, Starship | Moon Rover, Solar Array, Oxygen Pod, Star Capsule, Holo Garden |
| Fantasy | Enchanted Court, Dragon Heath, Cloud Sanctuary | Wizard Tower, Dragon Statue, Treasure Chest, Dragon Roost |
| Ancient lost civilization | Lost City, Sunken Temple | Sun Obelisk, Glyph Tablet, Broken Column, Sun Temple |
| Japanese zen | Zen Garden, Bamboo Grove, Sakura Courtyard | Stone Lantern, Bamboo Fountain, Bonsai, Zen Pavilion, Torii Retreat |

Six additional fictional game treats have their own icons: Moon Mousse, Amber
Drop, Neon Jelly, Star Wafer, Lotus Nectar and Rune Biscuit. The original six
retain their names/effects and now also have dedicated art. These are game
items, not real animal-care recommendations.

## Buy and mix enclosure borders

**Menu > Borders** offers **48 transparent frames**, matching every old and
new background. Their corner motifs include leaves, books, arches, fossils,
runes, wires, solar panels and torii. Frames use fifteen silhouette families
rather than just recoloring one edge.

- Buy once with Silk: `10 + floor(background price / 2)`.
- Ownership and selection are independent of backgrounds. A frame purchase
  neither buys nor changes its matching floor.
- Reapply owned frames free; **No Border** removes one free without losing it.
- Frames composite below furnishings, leave the center open, and dim with the
  enclosure. Menus/HUD remain unaffected.
- Gift and decor long-press movement, growing placement capacity, soundtracks
  and separate music volume remain available.

## Persistence and memory

Save v12 has 52 hardware-compiled fields, a 268-byte fixed prefix and a counted
six-byte decor tail. Historical v10/v11 tails still load from offset 260;
all supported older pet saves migrate without granting ownership from padding.
Byte 245 packs gifts 32..35/decor 64..67 in low/high nibbles, u16 at 246 stores
backgrounds 32..47, and the full u32 at 248 covers decor 32..63.
Border ownership is at 260/264 and selection at 266 (255=None).

The expanded art pack contains **337 PNGs**. Native runtime retains 48px menu
thumbnails and a single 172,800-byte PSRAM decode workspace, rather than keeping
96 full backgrounds/frames resident. Explicit rebuilds copy into the existing
floor canvas; no new per-frame allocation or full-screen overlay is needed.
Console diagnostics acknowledge real completion on the console task.

See [gift effects/prices](GIFTS_CATALOG.md), [art rules](ART_STYLE.md),
[memory/threading policy](PSRAM_THREADING_ARCHITECTURE.md) and
[console/save protocol](serial_console.md).

## Verification

- Wrapper build/full flash exit 0, no main-source warnings.
- Browser 158/158, art 22/22, package 13/13; all 337 packed QOI entries verified
  against decoded source pixels and deterministic regeneration.
- Real hardware 48/48; all 48 floors/frames purchased and rendered, free
  mix/reapply/remove and reboot ownership checked; all 68 decor and 12 treats.
- Actual v12/52-field schema captured; 273 placements survive export/restore,
  reboot and high-slot editing, with high ownership and floor/frame 47 intact.
- `render 500` and `scanloop 5`: largest-block drift 0; internal/DMA largest 63,488.
- Real SD roundtrip restores high ownership, floor/frame 47 and music choices.
  Three distinct BMPs pass full readback; repeated capture PSRAM drift 20 bytes.
- Five-second RTC timer deep sleep commits state and preserves the habitat.
  Final 40-second soak: `VERDICT: PASS`, zero faults/errors.
- Real browser migrates v11 preferences, reaches the last page of all
  catalogues, buys/mixes frames, removes/reapplies free and persists on reload.
  Original hardware/browser pets restored after mutating tests.

These checks do not establish subjective touchscreen/speaker approval, measured
battery life, physical-button wake or RTC backup-battery retention.

## Published

[Download v1.1.22](https://github.com/jeromedontdev/Spidarium/releases/tag/v1.1.22)
or use the [live website](https://jeromedontdev.github.io/Spidarium/).
Source 89541a2; Pages run 43103300355 succeeded. Independent public checks verify
all five firmware hashes, full save schema, current app/style bytes and all 337
artwork PNGs. Production reload preserves old progress and opens Borders.
