# Spidarium v1.1.24

## Your spider, not a replacement insect

The website's circular habitat again shows the original pet spider, with just
one small beetle on a quiet moss floor. No pink furnishings or floating motion.
Both spider renderers and all 337 asset PNGs remain unchanged.

## Grids throughout

Shops, Borders, Achievements and Prey Species use six-item 3x2 grids.
Navigation, settings, contextual actions and status menus use four wider 2x2
tiles. Slider tracks, values, names and checkboxes have separate space.
Gift effects and text entry remain dedicated detail views rather than cramped
tiles. Save v12, catalogue prices, unlocks and ownership are unchanged.

## Existing-device USB updater

The web flasher defaults to **Update existing device / preserve pet**.
It checks the installed partition table before writing; incompatible layouts
stop with a backup/migration instruction. Updates write the application and
matching artwork, then initialize the application boot selection last.
Bootloader, partition table and NVS are untouched; every NVS byte is compared
after writing. Temporary NVS buffers stay local and are cleared.
Full installation remains available for new boards and layout migration.
Back up first: interrupted writes can still require USB recovery.

## GitHub latest-release OTA

**Settings > WiFi > Firmware Update > Update From GitHub** resolves the latest
stable GitHub release descriptor and its exact versioned application asset.
Identical versions report up to date; older releases are refused.
The device verifies installed-art compatibility and the downloaded application's
exact size, SHA-256, ESP image, project and version before selecting an inactive
OTA slot. NVS and artwork are not written. Press RESET after installation.
Artwork differences require the USB updater.

An advanced trusted HTTPS application URL remains available. Releases now
include standalone `spidarium.bin` and `spidarium-ota.json` assets alongside
the full ZIP. No signed-firmware provisioning or functional rollback is claimed.

## Verification before publication

- Browser 164/164; packaging/art/capture 44/44.
- Wrapper builds/full flashes exit 0 with no main-source warnings.
- Hardware 48/48; original pet restored afterward.
- Native diagnostics verify four/six-tile geometry, final-page selection,
  slider bounds and separate music-gain callback routing.
- `render 500` and `scanloop 5`: largest-block drift 0; internal/DMA largest
  63,488 bytes. Forty-second soak: PASS, zero faults/errors.
- Real saved-network association and fresh RTC/NTP synchronization pass.
- Real native/browser screenshots reviewed for achievement grids, slider grids
  and the restored spider-with-one-small-bug emblem.
- Publishing validates the standalone OTA descriptor/application against the ZIP.

Verification and publication evidence are recorded in
[the latest progress round](../PROGRESS.md). Physical browser USB flashing
requires an explicitly granted serial port; mock transport tests are not a
claim that a browser has flashed the board.
