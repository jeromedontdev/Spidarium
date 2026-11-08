# Spidarium v1.1.1

- Unboxed battery gauge visible only with care, safely inset below the identity
  panel; voltage and charging details remain tappable.
- Browser page ear flush with the square playfield corner, including fullscreen.
- Website promotional fangs use mint; player-selected colors are unchanged.
- Settings > Save Screenshot captures the playfield without menus or HUD.
  ESP32 writes numbered `/sdcard/spidarium-00001.bmp` files; browser downloads PNG.
- Serial `screenshot menu`, `screenshot playfield`, `screenshot spider` and
  `screenshot sd` support visual agent diagnostics. Bare `screenshot` retains
  active-screen behavior. `capturetest` verifies pixels and pet-state restoration.
- SD writes run on the existing persistence worker with PSRAM pixels, exclusive
  filenames, flush/fsync and complete byte readback. No save/schema migration,
  credential changes, card formatting or progress resets.

See [serial capture instructions](serial_console.md#screen-capture) and the
latest [progress round](../PROGRESS.md) for measured verification.
