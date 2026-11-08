# Spidarium v1.1.0

## Manual care, larger browser world

- Independent Settings checkboxes for Auto Feed and Auto Hunt. Both default
  off, including migration of existing pet saves. Without them, Feed and
  dragging/releasing a pounce are required.
- Browser game uses the native 480x480 world and menu/HUD coordinates, scaled
  to a larger responsive square; fullscreen preserves its aspect ratio.
- Native FSM, need/progression/prey/gift rules and eight-round minigame timing
  are ported to JavaScript. The browser is not an ESP32/LVGL emulator.
- Local progress persists across reloads. Settings > SD Card provides browser
  JSON backup/restore; invalid or unavailable storage is reported explicitly.
- Promotional spiders have purple fangs. Player color choices are unchanged.
- Published Personal, Non-Commercial License: free for personal use, not
  commercial use. Third-party licenses and notices remain in the ZIP.
- On-demand WiFi no longer joins stored networks at boot. A short radio
  initialization/teardown primes persistent network allocations before LCD
  creation; actual WiFi/time sync/OTA still require a player request.

## Updating safely

Back up the pet before flashing. The five-image ZIP preserves NVS; never erase
all flash. Save schema version 3 retains the 132-byte payload and 144-byte raw
backup. Version 2 backups migrate with automation off: their old padding bytes
are never interpreted as enabled checkboxes. Version 3 backups are not accepted
by older firmware; keep an original v2 backup if planning a downgrade.

WiFi, RTC, SD hardware and PMIC battery telemetry remain physical-board
features. Browser menus identify these boundaries and route to real USB tools
or local browser backups instead of inventing hardware readings.

Verification and platform limitations are recorded in
[the verification report](BROWSER_PARITY.md). The original v1.0.0 tag/archive
remain unchanged.
