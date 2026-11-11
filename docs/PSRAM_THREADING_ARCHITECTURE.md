# PSRAM and threading: whole-game design

This is an implementation reference, not a claim that all previous theories
about heap fragmentation were proven. It supersedes Round 25/26 conclusions.
Pausing does **not** defragment the heap or free existing allocations.

## Allocation policy

The ESP32-S3 has 8 MB external PSRAM and a much smaller internal SRAM budget.
Choose memory by the consumer's capabilities, not just allocation size.

| Consumer | Allocation policy |
|---|---|
| Spider canvas / CPU-only decoded art | Explicit `MALLOC_CAP_SPIRAM` |
| Selected 64x64 RGB565A8 gift canvas | Explicit PSRAM, 12,288 bytes; reused for replacements |
| Battery worker stack | 3,072 bytes explicit PSRAM; no flash writes |
| Static 480x480 RGB565 habitat canvas | Explicit PSRAM, 460,800 bytes; reused for every background |
| Eight 240x240 RGB565A8 enclosure sources | Explicit PSRAM, 1,382,400 bytes total; decoded at boot |
| Audio and serial-worker stacks | `xTaskCreateWithCaps`, SPIRAM + 8BIT |
| Network and save/SD worker stacks | Explicit internal RAM: flash operations disable caches |
| Synthesized audio PCM buffer | Explicit PSRAM, 12,800 bytes |
| Worker control blocks | Internal, allocated by FreeRTOS |
| LCD framebuffer | BSP PSRAM buffer (`use_psram`) |
| SPI DMA descriptors / transfer staging | Internal DMA-capable heap |
| Main LVGL adapter stack | Supported `stack_in_psram` API |
| LVGL-created draw-thread stacks | Internal; adapter patch hook incompatible with LVGL 9.6 |
| TLS working buffers | `MBEDTLS_EXTERNAL_MEM_ALLOC` |
| LVGL objects and general CPU heap allocations | PSRAM preferred for all sizes |
| WiFi static TX pool | Four buffers, rather than sixteen |
| WiFi/LwIP eligible allocations | `SPIRAM_TRY_ALLOCATE_WIFI_LWIP` |
| Code and read-only data | PSRAM instruction/rodata mapping enabled |

`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=0` is a **numeric threshold**;
`CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=65536` reserves internal memory for
capability-specific users. Both active and defaults configurations must agree.
PSRAM preference permits fallback; use explicit capabilities where placement
is an invariant.

A plain static variable lives in ordinary BSS, **not** automatically in PSRAM.
`SPIRAM_RODATA` does not relocate writable BSS. Allowing external task stacks
does not automatically move stacks there. ESP-IDF stack sizes are in **bytes**.
The network worker uses 12,288 bytes, not a portable FreeRTOS word count.
Flash-writing workers must use **internal stacks**. IDF 5.5.6's active flash
driver unconditionally checks the stack before disabling caches. Instruction
and rodata mapping in PSRAM does not bypass this check. A real periodic NVS
write on an external stack asserted in `cache_utils.c:141`; symbolization
identified `save_worker -> save_save -> nvs -> esp_partition_write`.
Do not disable the assertion or assume a deduplicated NVS write proves safety.

## Ownership and scheduling

- **LVGL adapter task:** touch dispatch, object tree, 20 Hz logic and 30 Hz
  rendering callbacks. These are two timers on one task, not separate threads.
- **Network worker:** 12 KB internal stack for credential/OTA flash writes;
  queued scan, join, time-sync, forget, OTA. Notifications
  wake it; atomics publish completion. Passwords never appear in logs.
- **Save worker:** 4 KB internal stack, copies of game state queued by LVGL,
  NVS commits off-thread. Queue and stack are allocated by `save_init()` at
  boot before LVGL starts, never lazily by a gameplay save callback.
  Queue capacity is four; rejection and commit failures are logged. SD probing,
  backup and restore use the same serialized worker, not blocking menu callbacks.
  Backup snapshots the live state into one job, commits it, writes/flushes/syncs
  the card file and verifies readback before publishing completion. FATFS uses
  heap-backed long-filename workspace, not stack-backed workspace, for
  `spidarium-save.bin`.
  Restored state is loaded on that worker and copied back to LVGL; no NVS read
  occurs from the external-stack LVGL task.
  Save acknowledgements propagate queue rejection instead of reporting success.
  Background preference writes share this worker; boot reads the separate NVS
  key before LVGL. Changing a habitat never changes the pet-save payload format.
- **Console worker:** one USB Serial/JTAG driver. UI commands request an action
  and wait on this worker, never on LVGL. Its explicit PSRAM stack is 8 KB;
  the old 4 KB stack overflowed during raw restore validation. Final real
  restore testing leaves 4,060 bytes free. Driver RX is 512 bytes to hold the
  complete 301-byte restore command; there is still only one driver.
  Schema/export/screenshots use bounded 128-byte direct-driver writes and
  TX-drain acknowledgements. Large ordinary VFS printf writes proved lossy.
  Screenshots use indexed parts, yield between parts and retain warning/error
  logs. Capture/release actions run on LVGL; only immutable snapshot pixels
  are read off-thread. No per-frame snapshot allocations are introduced.
- **Audio worker:** copied effects queued without blocking LVGL; synthesis and
  codec writes use a reusable PSRAM PCM buffer and a 4 KB PSRAM stack. Muting is
  atomically published. Active playback drains before radio startup; pending
  effects wait until the network job finishes.
  Quiet filtered air (8 seconds) / leaves (10 seconds) are considered every
  14-20 seconds after the first 12 seconds, only when audio is idle. No automatic
  chirps, pitched drops or sweeps. Two-second attacks and three-second releases
  prevent abrupt entrances/exits. Stream 100 ms chunks in the existing PCM
  buffer; pending foreground effects, mute or radio work interrupt with a
  20 ms fade instead of holding the network drain for the whole texture.
  Recheck volume between chunks. All synthesis/codec writes stay on this worker.
  `audiotest` runs pure deterministic synthesis on the console worker, without
  touching the speaker, live audio state or PCM buffer.
- **IDF system workers:** retain essential driver/event/network responsibilities.
- **Battery worker:** AXP2101 reads and brightness changes; atomic telemetry
  consumed by LVGL. Brightness changes pause/resume the display off-thread.
  Publish `power_writing()` and recheck `net_busy()`; networking drains it before
  requesting its display pause. Charging parameters and power rails are unchanged.
  Sleep overlay/touch wake remain LVGL-owned; saves use the persistence queue.

Game state and LVGL mutations belong on the LVGL task. Copy a snapshot into a
worker-owned job; never hand a mutable object tree or live game struct to a
worker. Console spawn/state/set/refresh and forced-render requests now run on LVGL as well.
Raw restore sets a pending guard before queueing the copied bytes. The tank
and later saves stay paused until reboot, preventing stale live-state writes.
Validation or worker failure reports an error and releases the guard.
Render stress commands request one canvas render per service tick rather than
calling LVGL from the console or blocking a frame with a whole stress loop. Diagnostic
reads are developer telemetry, not transactional simulation snapshots; use one
console client and sequential commands during tests.

Ordinary menu/editor modals and the Care overlay skip gameplay logic and tank rendering while keeping
the LVGL/service timer and job-completion polling alive. Do not confuse this
cheap modal gate with the worker-side full-adapter radio handshake. Static
background pixel art is rebuilt only on selection; it has no animated dust,
large web object tree or mandatory props. Authored 240x240 packed enclosure art
is nearest-neighbor expanded into the same reused PSRAM buffer. Selection
allocates no new canvas or decoded image memory; source images are decoded
explicitly into PSRAM at boot.
Visible Care-overlay labels update only on
actual value changes, at at most 4 Hz.
Gift art is CPU-expanded into one reusable 64x64 buffer on placement, not each
frame. Spider visits use existing movement plus a short rest/care event.
Minigame tiles/images/bar are built once per modal; rounds only mutate them.
The 50ms game timer never blocks, and closing the modal deletes it.
Spider gait uses subpixel walking distance; forced render requests cannot speed
it up. Holding a drag freezes behaviour/prey on this same task, not through
semaphores or another thread. Diagnostic pointer injection also runs on LVGL,
restores pet/prey snapshots before returning, and never persists synthetic XP.
Scan-loop memory benchmarks quiesce gameplay via an LVGL request and leave the
service timer running. The console worker waits 100ms for pending LCD transfers
before sampling, so idle snapshots are not confused with temporary SPI DMA
staging buffers. Original pause/Care visibility is restored afterward; the
4KB drift limit and 51,200-byte live DMA baseline remain unchanged.

## Exclusive networking handshake

1. UI queues a job and returns. While busy, gameplay callbacks skip mutations.
2. Network worker calls `esp_lv_adapter_pause(3000)`.
3. The adapter acknowledges after its current handler finishes and stops its
   tick/worker. **LCD composition and both game timers stop**, not merely the
   spider animation. The last frame remains visible; touch is temporarily paused.
4. Worker waits up to three seconds for active persistence and audio playback
   to drain. Pending saves and sound effects wait until the radio job completes.
   A drain timeout aborts networking and resumes the display without starting WiFi.
5. Worker initializes WiFi, performs a bounded operation, then calls both
   `esp_wifi_stop()` and `esp_wifi_deinit()` on success and failure.
6. Worker resumes the adapter. The UI polls the completion generation and
   rebuilds the WiFi menu. No render timer was deleted or paused independently.

This intentionally favors reliable DMA ownership over an interactive scanning
menu. A scan takes seconds, not an assumed 100 ms. The LVGL task itself never
waits on a network semaphore, NTP, flash download, or WiFi API.
Gameplay-only `game_pause/resume` are LVGL-task-only and leave the render/service
timer alive. Workers must not call them.

## Boot and RTC

Before display startup, initialize the shared BSP I2C bus and read PCF85063A.
Valid UTC restores the system clock. No saved credentials means **no WiFi init
or start at boot**. Otherwise run one bounded sync on the network worker,
write received NTP UTC to the RTC, fully stop/deinit WiFi, then start the display.
The external RTC, with functioning backup power, is what keeps time while off.
NTP is not a substitute for a backup battery.

## Rules for new systems

1. No blocking WiFi, NVS writes, file I/O, or large locals on LVGL callbacks.
2. No `lv_*` from workers; use copied requests plus completion acknowledgement.
3. Allocate CPU-only buffers once in PSRAM. Mutate prebuilt objects each frame.
4. Explicitly free capability allocations and shut down peripherals on failure.
5. Measure `largest` as well as `free`; do not infer leaks from display corruption.
6. Keep `sdkconfig` and defaults aligned; verify generated configuration.

See [WiFi](WIFI_ARCHITECTURE.md), [memory budget](memory_budget.md),
[serial tests](serial_console.md), and [handoff](AGENT_HANDOFF.md) for evidence
and remaining validation requirements.
