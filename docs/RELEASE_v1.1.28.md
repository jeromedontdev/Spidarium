# v1.1.28 - Richer habitats and the complete material-art pass

## One coherent pixel-art world

- All 48 backgrounds have independent thin matching frames. The approved
  Crystal floor remains exact; its selected border source is fitted offline
  to the thin frame. Moss retains its approved offline pair, with 46 other
  pairs using named deterministic material recipes.
- All 68 decor props, five legacy derivatives and 36 gift furnishings share
  the richer SNES-era direction: fractured stone, chipped wood, overlapping
  foliage, folded petals, gills, shell ribs and crisp material planes.
- Remaining themed props have individually positioned construction details:
  book pages/bookmarks, cycad pinnae, bonsai cushions, masonry cracks,
  equipment seams/rivets and cloth folds. No generic scattered sparkle filter.
- All 40 prey frames, 30 menu icons, 11 badges and 12 treat icons receive
  scale-appropriate material highlights. Glyph meaning, prey gait and species
  silhouettes remain readable; small furnishings keep their original scale.
- The approved spider, catalogue IDs/order, prices, blocking defaults,
  footprints, save schema and persistent render buffers are unchanged.
- Native menus and the website/browser use the same generated artwork.
  Deterministic sources, approved imports, finite palettes, pixel-grid/alpha
  checks and packed QOI pixel validation are documented in the
  [asset guide](../assets/README.md) and [style contract](ART_STYLE.md).
  No PixelLab credits were spent on the offline catalogue work.

## More reliable lighting and documented test currency

- Double taps use repeatable 450ms/48px pairs with finger jitter tolerance.
  Holds, drags, hunting, placement and HUD/menu interactions cannot accidentally
  switch the lights. Native and browser behavior remains aligned.
- `silk add 5000` adds 5,000 Silk for testing, with overflow/input rejection and
  acknowledgement only after the save worker finishes. It does not replace
  the pet or reset the balance. See [console commands](serial_console.md).

## Install with USB

The artwork pack changes, so use the website's save-preserving USB updater
and export a pet backup first. The ZIP contains the complete five-image set
without NVS. Standalone app/OTA descriptor files are also provided, but
on-device OTA requires matching installed art and will reject the older pack.
Complete hosted OTA download/reboot remains unproven on the current hardware
link. No claim of signed firmware, encrypted NVS or functional rollback.

## Verification

Build/flash wrappers exit0 with no compiler warnings. Host75/75, browser180/180
and native52/52 tests pass; all337 packed QOI pixel entries are verified.
Decor68/gift36 checks and all eight pointer flags pass. Render500/scanloop5
have zero largest-block drift, internal/DMA largest63,488 bytes, and the
40-second clean-boot soak passes with zero faults. The fresh280-byte pet is
restored exactly: Silk4541, ownership/settings and Crystal14/14 retained.
Exact deployment evidence belongs in the latest [progress round](../PROGRESS.md).
Actual LVGL captures are evidence of composition; physical appearance and
finger feel remain human judgements.
