# Spidarium v1.1.23

## More shop items, less menu clutter

- Treats, Backgrounds, Borders, Decor and Gifts use six-item **3x2 grids**,
  twice the previous three-card page capacity.
- Every tile has an individual icon/thumbnail, readable name and price/status.
  Purchase rules, unlocks, ownership and catalogue IDs are unchanged.
- Gift tiles open a complete effect preview before placement. Back returns to
  the same shop page without spending Silk.
- Navigation, settings and read-only catalogues keep their larger list cards.

## A real folded-corner HUD control

A small paper/gold page fold replaces the square/clipped upper-left control.
Its visible artwork is 34px, with a transparent 56x56 hit target inset 12px
from the screen edges. Tap to show care controls, then Menu; tap again to hide.
The browser retains M and exposes its expanded state to assistive technology.

The website's circular floating-spider emblem is now a static blue-green
woodland beetle against Fairy Glade. **The pet spider is unchanged**, including
both renderers and its artwork. All 337 asset PNGs and save v12 remain intact.

## Verification

- Wrapper build/full flash exit 0; no main-source warnings.
- Browser 162/162, including all five grid layouts, final-page ID routing,
  gift preview/cancellation, fold geometry and the new website emblem.
- Package/art/capture regressions 42/42 (13/22/7); website build includes all
  337 PNGs and five hash-verified firmware images, excluding NVS.
- Real hardware 48/48; native grid geometry/pagination/selection diagnostics.
  Actual 480x480 LVGL captures confirm six tiles and the folded corner.
- `render 500` and `scanloop 5`: largest-block drift 0; internal/DMA largest
  63,488 bytes, above the 51,200-byte acceptance floor.
- Original hardware pet restored after regression tests.
- Final 40-second soak: `VERDICT: PASS`, zero faults/errors.

Physical touchscreen aesthetic approval is not inferred from screenshots or
automated pointer tests. Publication evidence is recorded in
[the latest progress round](../PROGRESS.md).
