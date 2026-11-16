# v1.1.31 - Modern pixel art, companion life and nineteen fixes

## Artwork

Every enclosure floor, frame, prop and gift was redrawn as modern hand-crafted
pixel art with real lighting and pixel shading, replacing the flat 8-bit-era
textures. All 48 floors, all 48 borders, all 68 decor props and all 36 gifts
were rebuilt; the spider is unchanged.

- **Floors** are ground textures with a little extra, not scenes. Organic
  surfaces are deliberately irregular; only brick, tiling and flooring repeat.
- **Borders** are narrow carved frames with a hollow middle, at most 16 colours
  and an exactly 8-logical-pixel rim. Crystal is now a generated violet-slate
  frame rather than the last remaining PixelLab import.
- **Props and gifts** read as their subject rather than as speckle. A frond is a
  solid tapering blade, a fern arches instead of radiating, a succulent is a
  layered rosette, and each gift is a solid body with a second material so it
  stays legible at 16x16.

Artwork is hash-pinned: `tools/approved_sources.py` records the approved floor
and border sources, and generation is deterministic and byte-preserving.

## Care, sleep and automation

- **Sleep/wake oscillation fixed.** Idle slept below the energy threshold while
  sleep woke on low hunger, so the two rules fired on the same tick and the
  closed eyes flickered. Auto-wake now needs energy 60 and the hunger shortcut
  needs energy above the sleep threshold, giving a real hysteresis band.
- **Feeding wakes the spider.** Prey only becomes food when caught, so feeding a
  sleeping spider previously did nothing visible.
- **The spider poops on its own** every seven minutes while awake and not
  starving, instead of only after a catch.
- **Auto Feed targets 50% hunger** instead of a fixed 20-minute spawn. Hunger
  decays at 3/min, so the old rule cost 60 points per cycle and the spider was
  permanently starving. The interval now derives from the hunger deficit.
- **Auto Clean** is a new setting: it sweeps droppings and restores hygiene
  every three minutes, pays no XP or achievement, and leaves live prey alone.
- **Lock Decor** is a new setting that refuses placement and movement.
- **Decor is refused while live prey is out**, so a bug in the tank cannot be
  hidden by an item being dragged over it.
- **Solid decor can no longer be stood on.** The pounce landing is pushed clear
  of the item, with a last-resort nudge for a spider embedded by a restored save.
- **Walk-behind covers only the top half** of an item, so the spider passes
  behind the artwork rather than behind its base.
- **Tricks have their own poses.** Wave and Bow previously fell through to the
  same idle crouch; Spin was already visible via the heading rotation.

## Menus and Today

- **The decor placement overlay** collapsed from a full-screen stack into two
  transparent rows pinned to the bottom of the screen, so the item being
  positioned and the spider stay readable underneath.
- **Today** shows Check In, the rotating task and one request row with no
  submenu. A completed request stays visible with its reward line.
- **Toggling a setting no longer jumps to the first page.** This fixed every
  "toggle and stay on this page" action, not just Auto Hunt.

## Website

- The feature-grid spider is awake and alert instead of asleep.
- The Help section is a proper section with a heading block and a two-column
  card grid instead of a bare stack of collapsible rows.
- The large circular hero portrait draws its fangs lower on the face. The
  device canvas, exported PNGs and in-page game keep the native geometry, so
  pixel-for-pixel parity still holds.

## Saves and installation

Save v18 appends `decor_locked` and `auto_clean` at offsets 3096 and 3097 plus
two reserved bytes, keeping the payload 4-byte aligned. The fixed prefix is
3100 bytes, the maximum payload is 23308 bytes and the compiled schema has 55
fields. Old v2-v17 backups remain readable; v17 and earlier default both new
settings to off. Older firmware cannot read new v18 saves. Export a backup
before updating or considering a downgrade.

Use the save-preserving USB updater or the complete five-image release ZIP.
No NVS image is included. This release changes artwork, so use USB rather than
a hosted OTA update.

## Verified capacity

App 2,112,160 / 3,145,728 bytes: **1,033,568 bytes (0.99 MiB) free per app slot**.
Artwork 3,497,969 / 6,291,456 bytes: **2,793,487 bytes (2.66 MiB) free**.

After the final 40-second soak: internal free 128,827 bytes, largest 63,488,
DMA-capable free 121,095, PSRAM free 1,823,948. Contiguous internal/DMA capacity
remains the tighter resource; largest exceeds the 51,200-byte floor.

## Verification

Warning-free build and full flash exit 0 with `Hash of data verified`. Browser
246/246 pass. Art and tooling tests pass, including deterministic generation,
byte-preserved sources and hash-pinned approved frames. `asset_build.py`
reports 387 valid PNGs, 348 non-spider PNGs with binary alpha and uniform 2x2
blocks, 48 opaque 240x240 enclosures, 68 props and 36 gifts at 8-12 material
tones each, and 48 hash-verified border frames with a transparent `[16,224)`
opening.

`render 500` and `scanloop 5` both report `largest_drift=0` with all five scans
successful. A 40-second software-reboot soak reports `VERDICT: PASS` with zero
faults. A device capture confirms the new frame, floor and props render together
with the spider readable.

The tools suite reports 218 tests with 4 errors, all of them the same
`OSError: [WinError 4551] An Application Control policy has blocked this file`
raised when the harness spawns a native test binary. That is a local
Application Control policy on this machine, not a firmware or artwork failure;
the affected tests are the native album, audio and save harnesses.

Physical appearance on the panel, and whether each redrawn floor, frame, prop
and gift suits its theme in play, still need human eyes. The measurements prove
geometry, colour caps and hash pinning, not aesthetic approval. No completed
hosted OTA/reboot, signed firmware, encrypted NVS, functional rollback, physical
touchscreen, SD media or RTC backup-battery claim is added.

See the latest [progress rounds](../PROGRESS.md) for full evidence.
