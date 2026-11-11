# Spidarium v1.1.21

## Soundtracks and music volume

Sound page 2 now offers Music Volume and Soundtrack. Choose an original calm
score: **Silk Garden**, **Moonlit Hollow** or **Rainy Canopy**.
Music gain is separate from master volume; it does not change effect volume.
Master Sound mute and master volume still control everything. Track changes
fade instead of jumping, and choices persist across reboot/browser reload.

## SD and screenshot fixes

- SD Card keeps a visible status/result row: checking, backing up, restoring,
  verified completion or failure. Results no longer disappear behind generic
  Write/Read labels.
- Fix native screenshots failing PSRAM allocation: retain a single snapshot
  until the worker finishes rather than allocating a second full-screen copy.
  Save Screenshot writes a numbered, verified playfield BMP to the real card.
- Browser backup/restore/screenshot feedback appears inside the game, including
  fullscreen. Browser file actions explicitly do not access the device SD card.
  Downloads are reported as requested, not proof the browser saved to disk.
- Slider labels update in place without losing keyboard focus.

Save v11 retains the 260-byte prefix, historical offsets and complete variable
decor tail. Music volume/track occupy former padding; older saves migrate to
100/track 0. No spider artwork or renderer changes.
