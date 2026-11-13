# v1.1.29 - Decor paths and layer controls

## Arrange the enclosure without leaving the preview

- New and moved decor offer three explicit paths: **Solid** steers the spider
  around the item, **Walk Over** lets it cross above, and **Walk Behind** lets
  it pass underneath the artwork without a collision.
- **Back / Front arrows** change which overlapping decor draws in front.
  The label shows its current layer and band: Floor is below the spider;
  Canopy (Walk Behind) is above it. Arrows reorder within that band, with
  higher layer numbers in front and disabled buttons at either boundary.
- Position, path and layer preview immediately. **Done** saves them together;
  **Cancel** discards every staged change. **Put Away** retains ownership.
  Gifts keep their existing movement controls without decor physics/layers.
- Native device and browser have matching retained controls, overlap selection,
  collision rules and preview ordering. Canopy stays below lighting and HUD.
  The approved spider and every artwork asset are unchanged.
- Help, website instructions, save documentation and console reference explain
  the new controls. `decor block SLOT` now cycles all three path values.

## Saves and installation

Save v13 reuses the existing six-byte item records and 268-byte fixed prefix:
path0/1/2 occupies the existing blocking byte, and record order persists layers.
Older v2-v12 backups remain readable. Older firmware cannot read new v13
saves; export a backup before updating or considering a downgrade.

Use the save-preserving USB updater or the complete five-image release ZIP.
No NVS image is included. This release keeps the v1.1.28 artwork pack.
A full hosted OTA download/reboot is still unproven on the current hardware
link; no signed firmware, encrypted NVS or functional rollback claim is added.

## Verification

Warning-free build and full flash exit0. Host75/75, browser189/189 and
native52/52 pass. Actual native pointer tests report all ten flags passing,
including staged paths/layers and cancel/commit behavior. Compiled schema is
v13/52 fields; all three modes and record order survive actual inline/extended
save/reboot checks with 273 placements. Invalid path3 and old-version path2
are rejected.

Render500 and scanloop5 have zero largest-block drift; internal/DMA largest
is63,488 bytes. A40-second soak reports zero faults and PASS. An actual LVGL
capture and browser pointer checks verify the controls; mobile390px has no
overflow and all buttons remain within the game screen. The fresh280-byte
pet is restored exactly, retaining ownership/settings, Silk4541 and
Crystal14/14 after reboot. Physical finger feel remains a human judgement.
Publication evidence is recorded in the latest [progress round](../PROGRESS.md).
