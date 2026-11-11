# Spidarium v1.1.18

## Habitat and menus

- Automatic ambience is disabled after listening feedback. Foreground effects,
  Sound volume/mute and manual audio diagnostics remain available.
- Move decor through Arrange Decor or a long press; small finger jitter no
  longer cancels browser holds. Closed native backdrops cannot swallow input.
- Placed Gift offers free movement and putting it away. Movement preserves its
  variant, ownership, bonuses and Silk.
- Gold header ornaments no longer overlap Back. Pixel rivets, framed icons,
  Daily task glyphs and actual background thumbnails strengthen the shared
  SNES-style menus.
- Backgrounds are bought once with Silk; cork is free and owned selections can
  be reapplied without charge. Prices range from 12 to 48 Silk.

## Bugs, not the spider

All eight living prey sets were redrawn. Insects have six thorax-attached legs
with fixed roots and alternating tripod feet; cricket hind legs are jointed,
and larvae have short true legs near the head. Heads/bodies no longer jerk
between frames. Ground gait advances with travel and stops during rests;
winged bugs keep timed wing beats. Both runtimes turn sprites toward movement.
Browser bugs now use their full native 48x48 size and caught bugs use splatters.
The approved spider renderer, geometry and all 39 exports are unchanged.

## Battery and saves

Battery offers Balanced, Battery Saver and RTC Sleep profiles; screen timeout;
automatic deep-sleep delay; RTC wake interval; and confirmed Deep Sleep Now.
Automatic sleep defaults off and requires real battery power without USB.
Deep sleep commits a copied pet snapshot on the internal save worker, drains
workers and acknowledges display pause before entry. Wake is by RTC timer or
the right/user button, not touch. Browser settings are honestly labeled device
preferences; it does not pretend to sleep an MCU.

Save v9 has a 248-byte payload / 260-byte raw backup with 41 compiled fields.
Existing v2-v8 saves remain supported. Migration keeps the previously equipped
background plus cork, without charging or granting the whole catalogue.
Background ownership and power preferences are included in backups. WiFi
credentials remain separate and are never included in the release.
No NVS erase is required.

## Verification

- Wrapper build and full flash: exit 0; zero main compiler warnings.
- Browser: 124 tests passed. Art: 14 tests; release packaging: 10 tests.
- All 158 packed QOI entries decoded and compared pixel-for-pixel to source;
  deterministic generation and unchanged spider exports verified.
- Hardware: 48 passed, 0 failed; real native pointer hold/decor/gift moves pass.
- 40-second soak: PASS, zero faults/errors. Internal/DMA largest 63,488 bytes.
  Five radio scans and 500 render ticks each have zero largest-block drift.
- Real v9 export/invalid restore/exact restore/reboot and background/power
  persistence pass; console stack headroom 3,284 bytes after restore testing.
- Actual five-second deep sleep: committed-sleep log, USB disconnect/reconnect,
  `wake_cause=4` (ESP RTC timer), saved habitat preserved.
- Local real-browser pointer holds survive jitter and move decor/gifts without
  petting; icon loading, Back/header clearance and prey headings/sizes checked.

Physical-button wake, unplugged current/runtime, complete peripheral rail
shutdown, touchscreen appearance and subjective audio approval remain unmeasured.
No new claim of authorized association/SNTP, hosted HTTPS OTA installation,
SD media or external RTC backup-battery retention.

The five-image full-flash archive preserves NVS. Back up the pet before any
firmware change; see [battery policy](BATTERY_MANAGEMENT.md) and
[console tools](serial_console.md) for diagnostic and wake details.
