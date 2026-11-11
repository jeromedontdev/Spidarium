# Spidarium v1.1.16 - a cohesive SNES-era habitat

## Artwork

- Sixteen quieter pixel-art backgrounds, with detailed margins and an open
  low-contrast center for the spider and player arrangements.
- All sixteen placeable props, five legacy props and eighteen gift furnishings
  use shaded stone, wood, foliage, shell, silk and crystal materials.
- Every prey animation/splatter, HUD glyph and achievement badge now shares
  crisp pixel clusters and finite material ramps.
- The approved spider is unchanged: both renderers and all 39 exports remain
  byte-identical. No prices, catalogue IDs, dimensions, collision or save
  layout changes.

## Menus and website

Native menus, setup/status/care cards, keyboard, sliders and minigames now use
flat navy, mint and gold chrome, two-pixel borders/corners and hard highlights.
The browser and website match without smoothing or recoloring artwork.
Readable text, touch geometry, keyboard focus, selection, destructive actions
and disabled navigation remain distinct.

## Reproducible pipeline

The generator remains the artwork source of truth. Twelve art regression tests
cover all groups, quiet centers, palettes, silhouette clipping and material
lighting. Validation checks all 158 source PNGs across deterministic generation
and compares every decoded packed QOI pixel. `--preview PATH.png` writes the
complete 119-asset non-spider review sheet. Website CI runs the same art gates.
Release packaging requires an explicit matching firmware version.
The browser/release save schema is refreshed from the verified device (v8,
35 fields, 236-byte current payload). Packaging rejects stale versions or
incomplete/out-of-range fields. This corrects the shipped schema metadata;
it does not change the firmware's save format.
The USB save inspector supports that schema, twelve decor slots and older
payload prefixes; an actual device export was decoded without modifying it.

All three games are included: Bug Hunt uses the new bug artwork for targets
and decoys; Color Match and Quick Tap keep readable native signal colors on
the new chrome. Settings Back also returns to the root menu correctly.

## Installation and verification

Use the official full-flash ZIP or the website installer: this release changes
the separate artwork partition, so application-only OTA is insufficient.
The five-image manifest excludes NVS; do not erase saved pets or credentials.
Build, device, memory and deployment evidence is recorded in the newest
[progress round](../PROGRESS.md). Physical touchscreen aesthetics remain for
the player to approve; test results are not a claim of subjective approval.

Personal, non-commercial license and bundled third-party notices are unchanged.
