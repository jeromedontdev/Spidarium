# SNES artwork and menu handoff

## Current request and reference

The player supplied a nine-panel pixel-art reference and requested new
SNES-era backgrounds and matching decor, **without touching the spider**.
They specifically want somewhat plain backgrounds so their own decor and
the spider stand out.

The reference uses wooded, ruined, sandy, autumn, snow, shore, cave and pond
materials, shaded object volume and crisp clustered texture. It is an art
direction reference, not a requirement to duplicate its crowded composition.
It was provided in chat and is not a repository asset.

[ART_STYLE.md](ART_STYLE.md) is the current contract. This document replaces
the earlier 642-line brief: its recommendations to use 200-900 colors, smooth
shading and noisy scatter are superseded. Do not restore them.

## What changed

- Sixteen backgrounds now use discrete material ramps, low-contrast clustered
  substrate and stronger detail only at the margins. No bilinear value noise,
  continuous color interpolation, smooth vignette or bright grit field.
- Each central 160x160 region has only three substrate tones with luminance
  span <=24/255. Background palettes are capped at 64 colors.
- Edge bark chips, leaf fragments, shallow moss/foliage, mineral facets and
  sand ripples distinguish terrain. Small edge vegetation is intentional;
  major furniture remains placeable. It adds no invisible collision.
- All sixteen decor sprites use 8-12 material tones. Stone planes, cut wood,
  bark furrows, fern pinnae, layered succulent leaves, shell ribs and crystal
  facets replace the former flat shapes. Five legacy props share this art.
- All eighteen gift furnishings are also redrawn with the same shaded
  materials, ink rims and 8-12 tones. The scope includes shelters, lookouts,
  garden, launchpad, hammock and toys, not just the `decoritem_` catalogue.
  Their 32x32 source size and existing 64x64 runtime buffer are unchanged.
- All forty prey frames, thirteen icons and eleven badges now share crisp,
  finite material ramps. Existing species silhouettes and glyph/badge motifs
  remain recognizable; splatters keep their distinct species and droplets.
- Native/browser menus, HUD cards, setup, editor keys and minigames now use
  flat navy/mint/gold chrome, two-pixel corners/borders and hard highlights.
  The website carries that palette without filters on the artwork.
- Rounded shading stays inside its silhouette; its former offset ellipses
  could paint over/outside the ink rim.
- Asset filenames, theme/decor IDs, dimensions, prices, blocking flags,
  footprint radii, saves and persistent render buffers remain unchanged.
  Chrome adds style properties, not per-frame objects or new worker activity.
- The approved spider renderer, browser port and 39 PNG exports are untouched.

The low-bit LCG output also caused correlated edge placement; selecting higher
bits removes the repeated top/bottom-only scatter while retaining fixed seeds.

## Source of truth

| Surface | Source |
|---|---|
| Geometry, palettes, fixed seeds | [tools/gen_sprites.py](../tools/gen_sprites.py) |
| PNG contract and packed QOI validation | [tools/asset_build.py](../tools/asset_build.py) |
| Quiet-center, palette, silhouette regressions | [tools/test_art_style.py](../tools/test_art_style.py) |
| Generated source art | [assets](../assets/) |
| Catalogue and collision | [main/decor.c](../main/decor.c) |
| Device mappings | [main/sprites.c](../main/sprites.c) |
| Browser catalogue/rendering | [site/lib/device-engine.js](../site/lib/device-engine.js), [site/lib/device-ui.js](../site/lib/device-ui.js) |
| Exact latest evidence | [PROGRESS.md](../PROGRESS.md), newest round |

Never edit a generated PNG or `main/mmap_generate_storage.h` by hand.
Never add per-frame allocations or block LVGL to support artwork.

## Authoring and source checks

```powershell
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' -m unittest discover -s tools -p test_art_style.py -v
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\gen_sprites.py
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\asset_build.py --png-only
powershell -File build.ps1
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\asset_build.py
```

The explicit generator run establishes the new approved art baseline before
the validator snapshots all 158 source files. Validation still checks all names,
dimensions, deterministic output, style, runtime mappings and packed pixels.
Confirm the spider and every non-target PNG match a pre-edit hash snapshot.
Use `tools\asset_build.py --png-only --preview PATH.png` to inspect every
non-spider asset, not only backgrounds or the placeable-decor catalogue.

## Visual acceptance

Inspect a 4x4 background montage, a decor sheet at native scale and an actual
composed scene with the approved spider. Check:

1. Crisp material clusters and top-left shading, not blurred texture or flat
   icons with arbitrary extra colors.
2. No bright speckles or strong seams competing with legs in the central area.
3. Props remain legible on both light sand and darker mineral/soil floors.
4. Edge details are restrained: the player still has a useful area to arrange.
5. Fern pinnae, wood grain, shell ribs and crystal facets remain readable.
6. Prey, splatters, icons and badges use material clusters rather than flat
   unshaded fills. Native/browser menus have no gradients or soft shadows;
   selected, disabled, destructive and keyboard-focus states stay distinct.

Do not claim subjective player approval or physical panel appearance from
source metrics, a browser screenshot or a framebuffer capture alone.

## Runtime acceptance

```powershell
powershell -File flash.ps1 COM7
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' run_tests.py COM7
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' stability_check.py COM7 40
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' cmd.py COM7 "uitest"
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' cmd.py COM7 "decortest"
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' cmd.py COM7 "mem"
```

Require build/flash exit 0, no main warnings, all hardware tests passing,
`backgrounds=1`, `placed=12 slots=12`, soak `VERDICT: PASS`, zero faults,
internal/DMA largest >=51,200 bytes. Allocation changes additionally need
`scanloop 5` and `render 500` with <=4,096 bytes drift.
Use one serial client at a time; opening COM7 resets the board.

Capture with [tools/capture_screen.py](../tools/capture_screen.py); do not
alter the saved enclosure merely to stage a screenshot.

Browser tests and distribution:

```powershell
Set-Location site
$env:PATH = "$pwd;$env:PATH"
.\node.exe --test tests\*.test.mjs
.\node.exe build.mjs
Set-Location ..
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\prepare_website.py --assets-only
```

The copy step follows build because build recreates the distribution.
Append a progress round with **Decisions made**, exact evidence and any
unverified boundaries. A build alone is not art or runtime acceptance.
