# Art handoff — 16-bit SNES-era decor and ground textures

This is a scoped handoff for one job: **make the enclosure art read as a 16-bit
SNES-era game.** The gameplay, save format, menus and browser port are done and
working; do not redesign them.

Read [ART_STYLE.md](ART_STYLE.md) first — it is the written contract. This
document tells you what is wrong with the current art, what "16-bit" means
concretely here, and how to verify you actually achieved it.

---

## 0. The vision, in the player's own words

These are the actual messages that produced this handoff. They are the
requirement; everything below is interpretation and measurement.

> "I still very much dislike the backgrounds. I have created a list of different
> options but not all of them are necessary. Please make and implement some of
> them. I basically looked at what other people are doing for jumping spider
> enclosures and got the themes from there: Floral, Mushrooms and bridges,
> Gothic, Seashells, Succulent, Weeping willow, Wood, Desert, Cemetery, Indoor,
> Bright colors, Spiderweb, Glow in the dark, Christmas, Halloween,
> Thanksgiving, Mossy, Prehistoric, Pink house, Hobbit, Magical, Twilight,
> Subterranean, Vegetable garden, Crystal, Grayscale, Cavernous, Mountainous"

> "The backgrounds all look the same really, I was hoping for some really
> different stuff. I'm not in love with the current template. A lot of the logs
> look off and the colors are so muted."

> "here is what i want the backgrounds to look like" *(with reference images)*

> "yours look like they are out of a zx spectrum game while mine look like 16 bit
> snes era. you are missing a lot of detail"

> "those ferns dont look like ferns and some of the images are pretty cluttered
> which will make poor contrast with the spider"

> "We could jsut do the ground textures themselves, then add a system of buying
> decor that the player user can place themeselves. They could purchase them on
> the 'web' for silk"

> "We should use this same 16 bit snes era design across all assets and
> documentation / md files should reflect that"

> "I need to handoff to a different ai agent to refine the decor and backgrounds
> to better fit my vision of a super nintendo era 16 bit game."

The three complaints that matter most, in order:

1. **"you are missing a lot of detail"** — the art is too simple. This is the
   core issue.
2. **"the colors are so muted"** — the palettes are washed out and muddy.
3. **"they all look the same"** — sixteen themes that read as one theme
   recoloured.

### The reference images are not in the repository

The player supplied reference screenshots of the 16-bit look they want. Those
images were pasted into chat and are **not** committed anywhere in this repo.
Before starting, ask the player to re-supply them. Do not guess at the target
from the prose alone — the whole point of the handoff is that the previous
attempt drifted because it was working from description rather than reference.

---

## 1. The project in one paragraph

Spidarium is a virtual jumping spider on a Waveshare ESP32-S3-Touch-AMOLED-2.16
(480×480, 16 MB flash, 8 MB octal PSRAM, ESP-IDF 5.5.6, LVGL 9.6). The spider is
a direct-pixel 96×96 RGB565A8 canvas. The enclosure is a 240×240 ground texture
enlarged 2× into a 480×480 canvas, plus up to twelve **placeable decor items** the
player buys with Silk and positions themselves. A companion browser site mirrors
the game. All art is generated deterministically by Python, never hand-drawn.

---

## 2. The art pipeline (do not bypass it)

```
tools/gen_sprites.py  →  assets/*.png  →  idf.py build  →  storage.bin
  (Pillow, integer geometry)   (RGBA PNGs)   (spiffs_create_partition_assets, QOI)
                                    ↑
                        tools/asset_build.py (separate validation gate)
```

- `tools/gen_sprites.py` writes every PNG into `assets/`. It is deterministic:
  no `random` without a fixed seed, no floats in geometry, no fonts, no external
  downloads, no smoothing.
- `build.ps1` runs `idf.py build`, which packs `assets/` into the `storage`
  partition as QOI via `spiffs_create_partition_assets` in
  [main/CMakeLists.txt](../main/CMakeLists.txt) and generates
  `main/mmap_generate_storage.h`. Never hand-edit that header.
- `tools/asset_build.py` is a **separate** gate that `build.ps1` does not run.
  It runs the generator **twice**, checks the exact file set, count, dimensions,
  PNG format, RGBA mode, identical SHA-256 across both passes, byte-for-byte
  preservation of the accepted images, and the pixel-art contract. It then
  decodes every QOI stream in `storage.bin` and compares it pixel-for-pixel with
  the PNGs.

### The preservation gotcha

`asset_build.py` snapshots the PNGs **before** it runs the generator, then fails
if any preserved file changed. Decor sprites are in that preserved set
(backgrounds are not). So the order matters — always run the generator first, so
the snapshot already contains your new art:

```powershell
# 1. Regenerate the PNGs.
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\gen_sprites.py

# 2. Build. This repacks assets/ into storage.bin via spiffs_create_partition_assets.
powershell -File build.ps1 *> build_out.txt 2>&1; "exit=$LASTEXITCODE"

# 3. Validate sources AND the freshly packed storage.bin.
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\asset_build.py
```

If you add or remove asset files, update `ASSET_COUNT` (158), `NON_SPIDER_COUNT`
(119) and `PRESERVED_ASSET_COUNT` (84) in `tools/asset_build.py` to match.
`PRESERVED_ASSET_COUNT` deliberately excludes the 39 spider frames, which are a
generated export of `main/cspider.c` and must be free to change.

---

## 3. The validator rules you must satisfy

From `tools/asset_build.py`:

| Rule | Sprites | Backgrounds |
|---|---|---|
| Dimensions align to the 2×2 pixel grid | **required** | exempt |
| Every aligned 2×2 RGBA block identical | **required** | exempt |
| Binary alpha (0 or 255) | **required** | **required** |
| Fully opaque | n/a | **required** |
| ≤ 12 visible RGBA colours | **required** | exempt |
| Deterministic across two generator passes | **required** | **required** |

Backgrounds are exempt from the grid and palette rules because they are
illustrations authored at the panel's native 240×240, not sprites. That exemption
is the headroom you have for the ground textures.

---

## 4. What is wrong right now

### 4.1 The decor sprites are 8-bit, not 16-bit

Measured from the current `assets/decoritem_*.png`:

| Sprite | Colours | Opaque coverage |
|---|---:|---:|
| rock_small | 4 | 30% |
| rock_large | 4 | 65% |
| pebbles | 4 | 30% |
| log | 4 | 27% |
| bark | 4 | 30% |
| fern | 4 | 44% |
| moss_clump | 4 | 39% |
| succulent | 5 | 47% |
| flower_white | 6 | 41% |
| flower_pink | 7 | 41% |
| mushroom | 6 | 51% |
| shell | 4 | 37% |
| crystal | 3 | 35% |
| leaf_pile | 7 | 44% |
| twig | 3 | 16% |
| bone | 4 | 31% |

**Three to seven colours per prop.** The cap is twelve. A 16-bit-era prop spends
that budget: a rock has a lit face, a mid face, a shadowed face, a rim, a
crevice, and a couple of speckles — six to nine tones. The current props are
ink rim + body + one highlight + one shadow, which is exactly the 8-bit look the
player is rejecting.

The `_blob()` helper in `gen_sprites.py` already does crescent shading, but it
only ever stamps four tones: ink rim, body, one lit crescent, one shadowed
crescent. It needs a fifth and sixth: a terminator band between the lit and
shadowed crescents, and a bounce-light sliver on the shadow side. That single
change lifts every rounded prop at once.

### 4.2 The ground textures are noise, not material

Measured from the current `assets/background_*.png`:

| Theme | Colours | Mean saturation | Mean luminance |
|---|---:|---:|---:|
| cork | 731 | 0.57 | 0.41 |
| moss | 766 | 0.46 | 0.31 |
| leaf_litter | 788 | 0.62 | 0.37 |
| sand | 121 | 0.61 | 0.61 |
| fern | 706 | 0.43 | 0.26 |
| orchid | 190 | 0.42 | 0.29 |
| woodland | 486 | 0.52 | 0.34 |
| minimal | 1522 | 0.12 | 0.69 |
| mushroom | 560 | 0.56 | 0.38 |
| gothic | 75 | 0.40 | 0.30 |
| seashell | 159 | 0.34 | 0.69 |
| succulent | 196 | 0.38 | 0.25 |
| willow | 537 | 0.45 | 0.28 |
| desert | 155 | 0.62 | 0.49 |
| crystal | 145 | 0.41 | 0.23 |
| cemetery | 141 | 0.36 | 0.23 |

The colour counts are fine. The problem is **structure**. `_ground()` is three
octaves of bilinear value noise plus a vignette plus grit. Value noise has no
edges, so the result is a smooth mottle — it reads as mud, fog or wallpaper, not
as a material. A 16-bit game floor is built from *discrete, readable elements*:
individual bark chips, individual moss cushions, individual gravel stones, each
with its own rim and its own two or three tones, packed edge to edge.

That is the single biggest visual gap. Noise cannot be fixed by adding octaves;
it has to be replaced with a **tiling element scatter**.

Also note the luminance spread: `fern` at 0.26 and `succulent` at 0.25 are very
dark, while `minimal` at 0.69 and `seashell` at 0.69 are very pale. The spider is
a mid-grey creature, so the dark floors give it poor contrast. Aim for a mean
luminance band of roughly **0.34–0.58** across all sixteen, and keep the
saturation at or above **0.35** so nothing reads as grey sludge.

The player's complaint that "the colors are so muted" is measurable: `minimal`
sits at 0.12 saturation and `seashell` at 0.34, both below the floor. But do not
fix "muted" by simply cranking saturation — the browns at 0.57–0.62 are already
saturated and still read as mud, because saturation without structure is just
louder mud. Fix the structure first, then tune saturation.

### 4.2.1 The sixteen themes and where they came from

The player supplied a 28-item inspiration list drawn from real jumping-spider
enclosure styles, and said "not all of them are necessary." Sixteen were
implemented. The mapping below records the intent, so a redraw keeps each theme's
identity rather than drifting toward a generic substrate:

| Implemented | Player's inspiration | Intended material |
|---|---|---|
| `cork` | Wood | warm ochre bark mulch |
| `moss` | Mossy | dense green moss cushion |
| `leaf_litter` | (implied) | fallen brown leaves |
| `sand` | Desert | pale dune sand |
| `fern` | Floral | fern-frond litter |
| `orchid` | Floral | orchid bark mix |
| `woodland` | Wood | dark forest floor |
| `minimal` | Grayscale | clean neutral paper |
| `mushroom` | Mushrooms and bridges | rich compost with spawn |
| `gothic` | Gothic | violet-black slate |
| `seashell` | Seashells | pale shell grit |
| `succulent` | Succulent | teal gravel |
| `willow` | Weeping willow | olive leaf drift |
| `desert` | Desert | terracotta grit |
| `crystal` | Crystal | dark mineral bed |
| `cemetery` | Cemetery | cold grey earth |

Themes from the player's list that were **not** implemented and are available if
the next agent wants more variety: Indoor, Bright colors, Spiderweb, Glow in the
dark, Christmas, Halloween, Thanksgiving, Prehistoric, Pink house, Hobbit,
Magical, Twilight, Subterranean, Vegetable garden, Cavernous, Mountainous. Adding
one means a new `BACKGROUND_NAMES` entry, a new `BACKGROUND_PALETTES` entry, a new
`_bg_*` scene, a new `BACKGROUNDS` entry in `tools/asset_build.py`, and a new
`BACKGROUNDS` entry in `site/lib/device-engine.js` — plus the asset counts.

### 4.3 The spider is the style anchor, and it is only 8 colours

The spider is **not** a sprite sheet. `main/cspider.c` draws it procedurally into
a 96×96 RGB565A8 canvas every frame, and `site/lib/spider-renderer.js` is a pixel
port of that renderer. The 39 `spider_*.png` files are a **generated export** of
the same geometry and palette (`draw_spider()` in `tools/gen_sprites.py`), so the
published art shows the creature the panel actually draws. They are not preserved
art: they regenerate whenever the renderer changes.

`assets/spider_s_walk_0.png` uses 8 colours at 96×96:

| Colour | Pixels | Role |
|---|---:|---|
| `#8C96A5` | 756 | body mid (RGB565 of the charcoal palette) |
| `#63656B` | 641 | leg grey |
| `#F7FBFF` | 213 | eye white and fangs |
| `#94969C` | 170 | body highlight |
| `#293039` | 142 | body base |
| `#000000` | 88 | pupils |
| `#6B7184` | 52 | secondary eyes |
| `#FFFFFF` | 10 | eye glints |

That is a cool blue-grey creature with black pupils, grey legs and white fangs.
Every floor and every prop has to sit behind it without competing with it. Two
consequences:

- **Warm floors, cool spider.** The browns and ochres are correct in hue; they
  are wrong in *structure*. Do not shift the whole palette cool to "match" the
  spider — that would flatten the contrast that makes it readable.
- **Keep decor at 8–10 tones and let the ground carry the detail.** If decor
  jumps to twelve rich tones and the ground becomes crisp tiled material, the
  spider will look flat by comparison. Raising the spider's tone count is the
  other option, but the spider is drawn by code the player has explicitly
  approved (see §7), so prefer the first.

Sickness is expressed by **dulling the palette**, never by making the canvas
translucent. `cspider.c` used to apply `LV_OPA_60` to the whole canvas while
sick; against the pale floors that shipped in v1.1.13 the ground showed straight
through the body and read as a rendering bug. `sick_aware()` now blends each
colour 45% toward its own luma and darkens 12%, so every pixel stays opaque.

**Both ports must compute in binary32.** `cspider.c` uses `float`, so the
generator rounds every intermediate through `_f32()` and the browser port through
`Math.fround`. This is not pedantry: `cosf(PI_F / 2)` is `-4.37e-8` while
`Math.cos(Math.PI / 2)` is `+6.12e-17`, and a coordinate that lands exactly on an
integer truncates one pixel apart between the two. The generator also uses C
integer division (`_idiv`) rather than Python's floor division, because the two
disagree for negative operands.

Draw order matters as much as arithmetic. `cspider_render()` draws **both** eyes,
then **both** pupils, then **both** fangs. Interleaving them per side lets the
second eye overwrite the first pupil's inner edge, which is a visible notch at
this size. A browser test decodes all 39 exported PNGs and compares them to the
renderer pixel for pixel, so neither mistake can return silently.

---

## 5. What "16-bit SNES-era" means concretely

Not "more colours". These are the specific, checkable properties:

1. **Discrete elements with rims.** Every element on the floor is a shape with a
   dark outline, not a gradient blob. A bark chip is a rounded rectangle with an
   ink rim, a lit top edge and a shadowed bottom edge.
2. **Three-to-five tone ramps per material**, in a fixed order: ink rim → shadow
   → body → highlight → specular. Never two.
3. **A hard terminator.** The boundary between lit and shadowed faces is a
   visible band, not a smooth falloff. This is what separates 16-bit from
   airbrushed.
4. **Bounce light.** A one-pixel sliver of the material's own hue, lightened,
   along the shadow side. This is the single most "16-bit" trick there is.
5. **Consistent light from the top-left.** Every element, every theme.
6. **Readable at 2×.** The panel shows the 240×240 source at 2×, so a 1 px detail
   becomes a 2×2 block. Design for that: features should be 2–6 source pixels,
   not 1.
7. **No gradients, no blur, no soft alpha.** A gradient is a filtered resize in
   disguise.
8. **Palette discipline per theme.** Each theme gets a tight ramp — one hue
   family for the substrate, one for the accent material, plus a neutral. Sixteen
   themes should look like sixteen *places*, not sixteen recolours of one place.

---

## 6. The work, in order

### Step 1 — Rebuild `_ground()` as a tiling element scatter

Replace the noise field with a scatter of discrete elements over a base fill:

- Pick a base substrate tone from the theme's ground ramp.
- Scatter N elements (bark chips, moss cushions, gravel stones, shell fragments,
  flagstones — per theme) on a jittered grid so they pack without overlapping
  badly. Each element: ink rim, shadow tone, body tone, highlight tone, and a
  bounce-light sliver.
- Keep a meandering trail where the theme has one, but build it from the same
  discrete vocabulary (packed flat stones, worn earth with rimmed edges) rather
  than a blended band.
- Keep the fine grit, but as sparse rimmed specks, not noise.

Target: **mean luminance 0.34–0.58, mean saturation ≥ 0.35, 200–900 distinct
colours** per theme. Verify with the measurement snippet in §8.

### Step 2 — Add the missing tones to `_blob()` and `_leaf_shape()`

- `_blob()`: add a terminator band and a bounce-light sliver. Every rounded prop
  gains two tones for free.
- `_leaf_shape()`: add a lit half, a shadowed half, a midrib highlight and a rim
  highlight. A leaf should be 5 tones, not 3.
- `_frond()`: the pinnae need their own rim and highlight, not a single line.

### Step 3 — Redraw the sixteen decor props at 8–10 tones

Keep the existing silhouettes where they read (rock_large, log, mushroom, shell,
crystal, bone are recognisable). Fix these specifically:

- **fern** — currently reads as a green starburst. Needs a visible crown, fronds
  that taper, and pinnae with individual rims.
- **twig** — 3 colours and 16% coverage; it is nearly invisible. Needs bark
  texture, a rim, and a fork with thickness.
- **crystal** — 3 colours. A crystal is the easiest prop to make look 16-bit:
  facets with distinct tones, a bright specular edge, and a dark base.
- **pebbles / moss_clump / leaf_pile** — these are clusters; each individual
  stone or leaf needs its own rim, or the cluster reads as one blob.
- **bark** — needs grain lines with a lit and shadowed side, not diagonal ticks.

### Step 4 — Rebalance the sixteen palettes

Bring every theme into the luminance and saturation band from §4.2. The dark ones
(`fern`, `succulent`, `crystal`, `cemetery`) need lifting; the pale ones
(`minimal`, `seashell`) need a little more colour. Keep the hue identity of each
theme — `gothic` stays violet, `desert` stays terracotta, `succulent` stays teal.

### Step 5 — Verify

Run the full verification in §8. Do not claim success from a build alone.

---

## 7. Hard constraints — do not break these

- **Do not change the spider's look.** The player has explicitly approved the
  procedural spider drawn by `main/cspider.c` and ported in
  `site/lib/spider-renderer.js`. Do not restyle it, and do not reintroduce a
  sprite-sheet renderer. The 39 `spider_*.png` files are a generated export of
  that renderer — if you change the renderer, regenerate them with
  `tools/gen_sprites.py` so the published art stays truthful. They are not
  preservation-protected.
- **Do not touch the living prey art** for the original four species
  (fly/cricket/moth/beetle). Their PNGs must stay byte-identical.
- **Do not change the decor catalogue order.** `decor_kind_t` in `main/decor.h`,
  `k_decor_kinds[]` in `main/decor.c`, `k_decor_items[]` in `main/sprites.c`,
  `DECOR_ITEMS` in `tools/asset_build.py` and `DECOR_KINDS` in
  `site/lib/device-engine.js` must all agree. A browser test parses the C table
  and compares it field by field.
- **Do not change decor footprint radii** without also updating the browser
  catalogue, or the art and the collision circle will disagree.
- **Do not add props to the background path.** Backgrounds are ground texture
  only. Everything on the floor is a placeable decor item. This was a deliberate
  architectural decision and reverting it will be rejected.
- **Do not introduce per-frame LVGL allocations.** Decor renders through twelve
  image objects created once in `terrarium_init()`.
- **Do not exceed the storage partition.** `storage.bin` is currently 691,288
  bytes in a 6 MB partition, so there is ample room, but check after a rebuild.
- **Do not hand-edit** `main/mmap_generate_storage.h`.

---

## 8. Verification

### Build, flash, test

```powershell
powershell -File build.ps1 *> build_out.txt 2>&1; "exit=$LASTEXITCODE"
Get-Content build_out.txt | Select-String -Pattern 'error:|warning:'
git checkout -- build_out.txt
powershell -File flash.ps1 COM7
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' run_tests.py COM7
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' stability_check.py COM7 40
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' cmd.py COM7 "uitest"
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' cmd.py COM7 "decortest"
```

Acceptance: build exit 0 with **zero `main/` warnings**, flash exit 0,
`run_tests.py` **47 passed / 0 failed**, `uitest` reports `backgrounds=1`,
`decortest` reports `placed=12 slots=12`, `stability_check.py` reports
`VERDICT: PASS` with 0 faults, and internal DMA `largest >= 51200`.

### Measure the art

```powershell
@'
from PIL import Image
import colorsys, os
names = ["cork","moss","leaf_litter","sand","fern","orchid","woodland","minimal",
         "mushroom","gothic","seashell","succulent","willow","desert","crystal","cemetery"]
for n in names:
    im = Image.open("assets/background_%s_0.png" % n).convert("RGB")
    cols = im.getcolors(maxcolors=200000)
    px = list(im.getdata())[::37]
    sats, lums = [], []
    for r,g,b in px:
        h,l,s = colorsys.rgb_to_hls(r/255,g/255,b/255)
        sats.append(s); lums.append(l)
    print("%-12s colours=%5d meanSat=%.2f meanLum=%.2f" % (
        n, len(cols), sum(sats)/len(sats), sum(lums)/len(lums)))
'@ | & 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' -
```

Target: every theme in **meanLum 0.34–0.58**, **meanSat ≥ 0.35**, **200–900
colours**.

### Look at it

Build a montage and actually view it — do not judge from numbers alone:

```powershell
@'
from PIL import Image
import os
names = ["cork","moss","leaf_litter","sand","fern","orchid","woodland","minimal",
         "mushroom","gothic","seashell","succulent","willow","desert","crystal","cemetery"]
cell = 240
sheet = Image.new("RGB", (cell*4, cell*4))
for i,n in enumerate(names):
    sheet.paste(Image.open("assets/background_%s_0.png" % n).convert("RGB"),
                ((i%4)*cell, (i//4)*cell))
sheet.save(os.path.join(os.environ["TEMP"], "bg_montage.png"))
'@ | & 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' -
```

Then capture the real panel, which is the only honest check:

```powershell
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\capture_screen.py COM7 "$env:TEMP\panel.png"
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\capture_screen.py COM7 "$env:TEMP\panel_decor.png" --menu decor
```

A capture takes about 48 s. The tool waits for `OK console ready` itself.

### Browser parity

```powershell
cd site; $env:PATH = "$pwd;$env:PATH"
.\node.exe npm-tool\package\bin\npm-cli.js test
.\node.exe npm-tool\package\bin\npm-cli.js run build
cd ..; & 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\prepare_website.py --assets-only
```

`prepare_website.py` must run **after** `build`, because `build.mjs` wipes
`dist/`. Expect **99/99** browser tests.

---

## 9. Definition of done

- [ ] `_ground()` builds the floor from discrete rimmed elements, not noise.
- [ ] Every decor prop uses 8–10 tones and has a rim, a terminator and bounce light.
- [ ] Every theme measures inside the luminance/saturation band in §8.
- [ ] The montage reads as sixteen distinct *places*, not sixteen recolours.
- [ ] The panel capture shows the spider clearly separated from every floor.
- [ ] `asset_build.py` passes with determinism and preservation intact.
- [ ] Build exit 0, zero `main/` warnings, 47/47 hardware tests, stability PASS,
      `uitest backgrounds=1`, `decortest placed=12 slots=12`, DMA largest ≥ 51200.
- [ ] Browser suite 99/99.
- [ ] `docs/ART_STYLE.md` updated if any rule in it changed.
- [ ] A `PROGRESS.md` round appended with **Decisions made** and exact evidence.

---

## 10. Where things live

| Path | What |
|---|---|
| `tools/gen_sprites.py` | The whole art generator. `_ground()` ~line 1260, `_blob()` ~835, `_leaf_shape()` ~873, `_frond()` ~895, `draw_decor_item()` ~930, `BACKGROUND_PALETTES` ~1119, `_bg_*` scenes ~1318. |
| `tools/asset_build.py` | Validator. `validate_pixel_art()` ~115, counts at the top. |
| `main/decor.c` / `main/decor.h` | Catalogue: names, costs, unlock levels, radii, blocking flags. |
| `main/terrarium.c` | Ground texture blit and the twelve decor image objects. |
| `main/sprites.c` | `k_decor_items[]` and `sprite_decor_item_index()`. |
| `site/lib/device-engine.js` | `DECOR_KINDS`, `BACKGROUNDS`. |
| `site/lib/device-ui.js` | Decor shop rows and icons. |
| `docs/ART_STYLE.md` | The written style contract. |
| `docs/ASSETS_INVENTORY.md` | File inventory and dimensions. |
| `PROGRESS.md` | Append a round when done. |

---

## 11. Ready-to-paste prompt

Copy everything below the horizontal rule into the next agent's first message.

---

You are taking over the art for **Spidarium**, a virtual jumping spider game on a
Waveshare ESP32-S3-Touch-AMOLED-2.16 (480×480, 16 MB flash, 8 MB octal PSRAM,
ESP-IDF 5.5.6, LVGL 9.6), with a companion browser site. The gameplay, save
format, menus, decor placement system and browser port are finished and working.
**Your job is one thing: make the enclosure art read as a 16-bit SNES-era game.**

Start by reading, in this order:

1. `.github/copilot-instructions.md` — the operating manual and the invariants you
   must not break.
2. `docs/ART_HANDOFF_SNES.md` — **your brief.** It contains the measured evidence
   of what is wrong, the concrete definition of "16-bit" for this project, the
   ordered work plan, the hard constraints, and the exact verification loop.
3. `docs/ART_STYLE.md` — the written style contract.
4. `docs/PSRAM_THREADING_ARCHITECTURE.md` — the allocation and threading policy.

**Before you write any code, ask the player to re-supply their reference
screenshots.** They pasted 16-bit reference images into an earlier chat and those
images are not in the repository. The previous attempt drifted toward an 8-bit
look precisely because it worked from prose instead of reference. Do not repeat
that.

### The problem in one paragraph

The player says the current art "looks like it is out of a ZX Spectrum game"
while their reference "looks like 16-bit SNES era — you are missing a lot of
detail." They are right, and it is measurable. The sixteen ground textures are
built from three octaves of bilinear value noise, which has no edges, so they read
as mud or wallpaper rather than as bark, moss, sand or gravel. The sixteen decor
props use only three to seven colours each against a cap of twelve, so they read
as flat 8-bit blobs. Both need to be rebuilt around discrete, rimmed elements with
three-to-five tone ramps, a hard terminator between lit and shadowed faces, and a
bounce-light sliver on the shadow side.

### What to do

Follow §6 of `docs/ART_HANDOFF_SNES.md` in order:

1. Replace `_ground()` in `tools/gen_sprites.py` with a tiling scatter of discrete
   rimmed elements over a base substrate fill, instead of a noise field.
2. Add a terminator band and a bounce-light sliver to `_blob()`, and a lit half, a
   shadowed half, a midrib highlight and a rim highlight to `_leaf_shape()`.
3. Redraw the sixteen decor props at 8–10 tones each. The brief names the specific
   ones that are worst: `fern`, `twig`, `crystal`, `pebbles`, `moss_clump`,
   `leaf_pile`, `bark`.
4. Rebalance the sixteen palettes into the measured band: mean luminance
   0.34–0.58, mean saturation ≥ 0.35, 200–900 distinct colours per theme. Keep each
   theme's hue identity.
5. Verify with the full loop in §8. Do not claim success from a build alone.

### Non-negotiables

- **All art is generated by `tools/gen_sprites.py`.** Never hand-draw or hand-edit
  a PNG. The generator must stay deterministic: fixed seeds, integer geometry, no
  fonts, no downloads, no smoothing.
- **Do not restyle the spider.** The player approved the procedural spider in
  `main/cspider.c` / `site/lib/spider-renderer.js`. `draw_spider()` in the
  generator is a faithful export of that renderer — keep it in step, and
  regenerate the 39 `spider_*.png` if the renderer changes. Do not touch the
  original four prey species (fly/cricket/moth/beetle); their PNGs must stay
  byte-identical and `tools/asset_build.py` enforces it.
- **Do not change the decor catalogue order or footprint radii.** `decor_kind_t`,
  `k_decor_kinds[]`, `k_decor_items[]`, `DECOR_ITEMS` and `DECOR_KINDS` must all
  agree, and a browser test parses the C table to check it.
- **Backgrounds are ground texture only.** Everything that sits on the floor is a
  placeable decor item the player buys and positions. Do not bake props back into
  the background path; that was deliberately removed because it fought the spider
  for contrast.
- **Never block LVGL.** No per-frame allocations, no object creation per frame, no
  WiFi/NVS/SD/blocking-audio calls in callbacks. Decor renders through twelve image
  objects created once in `terrarium_init()`.
- **Use the wrappers, never `idf.py` directly:** `build.ps1`, `flash.ps1`,
  `run_tests.py`, `stability_check.py`, `cmd.py`.

### Acceptance

Build exit 0 with **zero `main/` warnings**; `tools/asset_build.py` passes with
determinism and preservation intact; flash exit 0; `run_tests.py COM7` reports
**47 passed / 0 failed**; `uitest` reports `backgrounds=1`; `decortest` reports
`placed=12 slots=12`; `stability_check.py COM7 40` reports `VERDICT: PASS` with 0
faults; internal DMA `largest >= 51200`; browser suite **99/99**. Then append a
`PROGRESS.md` round with **Decisions made** and exact evidence, and update
`docs/ART_STYLE.md` if any rule in it changed.

Judge the result by looking at it: build the 4×4 montage of all sixteen themes and
capture the real panel with `tools/capture_screen.py`. The montage must read as
sixteen distinct *places*, not sixteen recolours of one place, and the spider must
be clearly separated from every floor.

