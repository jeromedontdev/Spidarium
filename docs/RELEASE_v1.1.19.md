# Spidarium v1.1.19

## Expanded habitat

- Three new treats: Honey Moth (14 Silk), Dew Drop (12), Cozy Nest (18).
- Eighteen new decor styles, for 34 total, including hides, arches, a hammock,
  dew pool, tent, lantern and stone bridge.
- Eight new backgrounds: House, Dark Cave, Night, Autumn, Snow, Pond, Ruins and
  Marsh. All 24 are bought once with Silk; owned themes remain free to reapply.
- Placement grows beyond twelve items. Resource/storage failures are explicit,
  not silent truncation. Hardware tests placed and saved 273 items, moved and
  opened slot 271, and retained the complete enclosure across reboot.
- Hold actual decor/gifts for their contextual menu. Redundant Arrange Decor,
  Placed Gift and Placed Items navigation is removed.
- Double tap the enclosure to dim/restore its lighting. Menus remain bright;
  screen brightness and battery preferences are unchanged. Double taps cannot
  also pet the spider.

## Sound and menus

- Original slow 32-second ambient chord/music loop, not intermittent noise.
  Background Music has its own persistent switch.
- One Sound checkbox toggles all audio on/off; there are no separate Sound On
  and Sound Off rows. Volume, Sound and Background Music fit on one page and
  have three distinct icons.
- Petting uses a soft consonant bell/purr instead of a falling chirp.
- Dedicated root/settings icons and actual species/treat/background thumbnails
  replace repeated clipboard placeholders.

## Persistence and memory

Save v10 preserves historical offsets and all supported v2-v9 migrations.
Its fixed payload is 260 bytes plus six bytes per extra decor slot; raw
backups include the 12-byte header. Native NVS, SD backups, USB restore/offline
viewing and browser local saves retain every record. Extended ownership uses
a second word for decor kinds 32/33. The actual compiled schema has 45 fields.

Additional records, renderer pointers, queue snapshots and serial lines use
PSRAM. Record mutations occur on LVGL; workers only use copied snapshots.
Opaque backgrounds decode to RGB565, so 24 sources use the same 2,764,800
bytes previously needed by 16 RGB565A8 sources. Rendering reuses its object
pool; there is no per-frame allocation or spider-art change.

## Verification

- 138 browser tests, 15 art tests, 11 packaging tests pass.
- All 201 packed QOI entries match PNG pixels; deterministic generation and
  all 39 unchanged spider exports are verified.
- Build and full flash succeed without main-source warnings.
- COM7 suite: 48 passed, zero failed; pointer hold, decor/gift movement,
  double-tap lighting and single Sound toggle pass.
- Actual native music start/stop/mute/resume, full pet writes and foreground/
  radio interruption pass; 40-second soak has zero faults.
- DMA/internal largest block: 63,488 bytes, above 51,200; render500 and
  scanloop5 have zero largest-block drift.
- Real committed five-second RTC sleep disconnects/reconnects USB and reports
  timer wake cause 4. Original pet snapshots are restored after mutating tests.

Speaker/touchscreen subjective approval, battery current/runtime, physical
button wake, SD media, authorized NTP/association, HTTPS OTA installation and
RTC backup-battery retention are not claimed.
