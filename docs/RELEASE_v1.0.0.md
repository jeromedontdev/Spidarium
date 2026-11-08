# Spidarium 1.0.0

The first official release for **Waveshare ESP32-S3-Touch-AMOLED-2.16** only.
Do not flash other ESP32 boards: display, touch, PSRAM and PMIC wiring differ.

## Features

- Direct-pixel animated jumping spider, slingshot hunting and eight prey species.
- Eight pixel-art enclosures; readable translucent care HUD and corner battery
  gauge. Three live bugs maximum; Feed disables until a slot is available.
- Silk-bordered menus with explicit Back navigation, disabled gray pagination,
  QWERTY editing and ten distinct earnable achievement badges.
- Eight-round Bug Hunt, ordered Color Match and feint-based Quick Tap.
- Eighteen earned habitat gifts, free owned repositioning and spider visits.
- On-demand WiFi, three saved networks, RTC/time synchronization and HTTPS OTA.
- Battery/USB/charging telemetry, brightness presets, idle dim/screen-off/wake.
- Asynchronous saves, verified SD backup/restore and ambient synthesized sounds.
- Browser website, USB flasher, local save backup/view/restore and WiFi setup.
  The playable web simulator is an approximate browser implementation, not an
  ESP32/LVGL emulator.

## Install or update

Visit [Spidarium](https://jeromedontdev.github.io/Spidarium/) using desktop
Chrome/Edge over HTTPS (or localhost). Close other serial consoles first.
Back up your pet before flashing. Connect the board's USB Serial/JTAG port;
follow the website's bootloader instructions if automatic reset fails.

The release ZIP contains five separate images and a SHA-256 manifest:

| Image | Offset |
|---|---:|
| bootloader.bin | 0x000000 |
| partition-table.bin | 0x008000 |
| ota_data_initial.bin | 0x010000 |
| spidarium.bin | 0x020000 |
| storage.bin | 0x920000 |

Do **not** erase all flash. The 0x9000-0xEFFF NVS partition contains the pet and
WiFi credentials and is excluded from these writes. A full installation updates
artwork as well as firmware; application-only OTA does not update artwork.
After a complete initial installation, the application image is available at
[the official OTA URL](https://github.com/jeromedontdev/Spidarium/releases/download/v1.0.0/spidarium.bin).
The SHA-256 manifest detects corruption, not publisher authenticity/signing.
Download only from the official repository/release.

Using an ESP-IDF environment manually:

```powershell
python -m esptool --chip esp32s3 --port COM7 --baud 460800 write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 ota_data_initial.bin 0x20000 spidarium.bin 0x920000 storage.bin
```

For development use the repository's build/flash wrappers instead.

## Saves and privacy

Version-2 saves retain the older 120-byte pet payload and migrate to the
132-byte payload without a reset. Raw backups are 132 or 144 bytes including
their 12-byte header. The compiled field schema accompanies the release.
Restore validates header, exact length and pet fields, writes on the internal
worker, freezes the tank and blocks subsequent saves until an explicit reboot.
This prevents live state from overwriting the restored file.

Save files contain pet data, **not WiFi passwords**. Background selection and
saved WiFi networks are separate NVS keys and are not part of the pet backup.
The browser sends credentials only to the selected USB device; do not share
private console captures. WiFi association needs an authorized working network.
The simulator uses its own local browser save and cannot restore simulated
progress onto hardware.

## Verification and limitations

See [release verification](https://github.com/jeromedontdev/Spidarium/blob/v1.0.0/docs/RELEASE_VERIFICATION.md)
for measured hardware, asset, browser and packaging results, and
[screenshots](https://github.com/jeromedontdev/Spidarium/blob/v1.0.0/docs/README.md).

Display power saving is not ESP32 deep sleep. Battery percentage is the AXP2101
estimate; charger configuration is unchanged. Unplugged runtime and physical
low-battery/charging transitions are not certified by USB tests.
Web Serial requires a supported browser; no physical-device access is available
to a remote browser test. Automated browser tests use an explicit mock transport,
while firmware/backend commands and normal flashing are tested on COM7.

Third-party firmware license texts are included in the ZIP. The repository
does not currently specify a separate project-wide source license; publication
alone does not grant additional redistribution rights.
