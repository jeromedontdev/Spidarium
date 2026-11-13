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
| Full background/frame decode workspace | One explicit PSRAM 172,800-byte RGB565A8 capacity; reused on explicit rebuild/test, never per frame |
| Forty-eight background/frame menu thumbnails each | Explicit PSRAM: 48x48 RGB565 floors (221,184 bytes total), RGB565A8 frames (331,776 total) |
| Additional decor records / renderer object-pointer pool | Explicit PSRAM; grows on placement/restore, never per frame |
| Placement preview image/ring and Done/Cancel controls | Retained LVGL objects created at boot; existing decoded art reused, no per-drag object/canvas creation |
| Save job snapshots and raw/SD transfer buffers | Explicit PSRAM; copied before enqueue, freed after completion/rejection |
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
- **Audio worker:** priority 7, above the adapter's priority 6, so menu
  construction cannot starve the bounded 800-sample producer. Codec writes
  block/yield on this worker only. Its existing 4 KB stack and 12,800-byte PCM
  stay in PSRAM; no extra internal DMA buffers. `audiotest` reports completed
  chunks, maximum refill gap and late (>50 ms) refill count. This measures
  scheduling headroom, not the physical speaker's audible quality.
  Ten original score tables share this same stream/oscillator; old IDs0..2
  remain byte-identical. USB attach/detach chimes reuse that PCM buffer too.
  The PMIC worker debounces edges and atomically publishes a copied effect ID;
  LVGL only queues it. Neither PMIC nor LVGL writes PCM or waits for playback.
- **Network worker:** 12 KB internal stack for credential/OTA flash writes;
  queued scan, join, time-sync, forget, OTA. Notifications
  wake it; atomics publish completion. Passwords never appear in logs.
  GitHub latest-release OTA uses a bounded 4 KB manifest and a reusable 4 KB
  streaming/hash buffer in PSRAM. All HTTP, storage compatibility reads and
  OTA flash writes remain on this worker while the display is paused. Only
  a fully size/hash/image/project/version-verified inactive slot becomes bootable;
  no NVS or artwork writes occur during latest-release OTA.
- **Save worker:** 4 KB internal stack, copies of game state queued by LVGL,
  NVS commits off-thread. Queue and stack are allocated by `save_init()` at
  boot before LVGL starts, never lazily by a gameplay save callback.
  Queue capacity is four; rejection and commit failures are logged. SD probing,
  backup and restore use the same serialized worker, not blocking menu callbacks.
  Backup snapshots the live state into one job, commits it, writes/flushes/syncs
  the card file and verifies readback before publishing completion. FATFS uses
  heap-backed long-filename workspace, not stack-backed workspace, for
  `spidarium-save.bin`.
  Screenshot jobs borrow immutable pixels from one retained LVGL snapshot.
  LVGL destroys that snapshot after worker completion/generation acknowledgement;
  workers neither free its pixels nor call LVGL. A second full-screen copy
  would exceed the expanded habitat's available PSRAM.
  Restored state is loaded on that worker and copied back to LVGL; no NVS read
  occurs from the external-stack LVGL task.
  Save acknowledgements propagate queue rejection instead of reporting success.
  Background ownership/selection, power, music and enclosure lighting share
  the v14 pet snapshot: 268-byte fixed prefix plus six bytes per extra decor
  slot. The original twelve slots/older offsets remain unchanged. The 20 KB
  serialized-payload ceiling is a storage-resource/protocol bound, not a
  twelve-item gameplay limit; storage/allocation/commit failures are explicit.
  Queue entries own their variable PSRAM payloads. SD restore transfers owned
  records to LVGL after worker completion instead of mutating active records.
  Boot reads the old separate background key only for one-time v8 migration.
  v10/v11 tails are read at their historical 260-byte offset before repacking.
  v12 extends ownership without moving old fields: byte245's low/high nibbles
  cover gifts32..35/decor64..67, u16 at246 backgrounds32..47; border ownership
  at260/264 and selection266 (255=None). Old padding never grants ownership.
  Floors/frames retain independent 48px thumbnails; a single mapped-QOI decode
  workspace feeds the existing 480px floor canvas. Transparent frames composite
  around its edge without creating a full-screen overlay or redrawing each tick.
  v13 preserves that layout: existing decor blocking bytes hold path0/1/2;
  only1 steers, 2 selects a retained Canopy layer above creatures. Existing
  record order persists within-band stacking, including inline/tail swaps.
  Position/path/layer previews use retained image/ring/controls; reparent/order
  on explicit changes only. Cancel restores saved order; Done queues one save.
  No new framebuffer, draw task or per-frame/per-drag allocation is introduced.
  Deep-sleep requests copy the live snapshot on LVGL; only this internal-stack
  worker enters sleep after an actual successful save, bounded audio/power drain
  and display pause acknowledgement. New saves/radio jobs are rejected while
  sleep is pending. Workers never call LVGL; LVGL disables/re-enables input.
- **Console worker:** one USB Serial/JTAG driver. UI commands request an action
  and wait on this worker, never on LVGL. Its explicit PSRAM stack is 8 KB;
  the old 4 KB stack overflowed during raw restore validation. Final real
  v9 restore testing left at least 3,284 bytes free. Driver RX is 1,024 bytes;
  the v10 command line is now heap-backed in PSRAM, with capacity derived from
  the maximum raw payload. Hosts pace long restore lines in 128-byte chunks;
  there is still only one driver.
  Schema/export/screenshots use bounded 128-byte direct-driver writes and
  TX-drain acknowledgements. Large ordinary VFS printf writes proved lossy.
  Screenshots use indexed parts, yield between parts and retain warning/error
  logs. Capture/release actions run on LVGL; only immutable snapshot pixels
  are read off-thread. No per-frame snapshot allocations are introduced.
- **Audio worker:** copied effects queued without blocking LVGL; synthesis and
  codec writes use a reusable PSRAM PCM buffer and a 4 KB PSRAM stack. Muting is
  atomically published. Active playback drains before radio startup; pending
  effects wait until the network job finishes.
  Original 32-second D-Bm-G-A ambient music streams in 50 ms chunks through the
  same buffer, fades in over 1.5 seconds and yields with a 20 ms fade to effects,
  mute, its independent toggle, radio or sleep. It continues in menus. No new
  PCM buffer or LVGL codec calls. Three original selectable tracks share that
  buffer; worker-applied music gain is multiplied by master volume and never
  reduces foreground effects. Track changes fade out/reset/fade in.
  Save v11 uses old padding bytes 254/255 without moving the decor tail.
  Manual filtered air (8 seconds) /
  leaves (10 seconds) remain diagnostic effects. No automatic chirps, pitched
  drops or sweeps. Two-second attacks and three-second releases
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
