---
description: "Spidarium ESP32-S3 virtual pet: build, flash, test and debug on real hardware. Use for changes to the game, renderer, WiFi or UI."
---

# Spidarium operating guide

Waveshare ESP32-S3-Touch-AMOLED-2.16: 480 x 480, 16 MB flash, 8 MB octal
PSRAM, ESP-IDF 5.5.6, LVGL 9.6.

Read [handoff](../docs/AGENT_HANDOFF.md) and the
[whole-game allocation/threading policy](../docs/PSRAM_THREADING_ARCHITECTURE.md).
The newest [progress round](../PROGRESS.md) supersedes older failed theories.

## Non-negotiable invariants

1. Never block LVGL. Game logic (20 Hz) and rendering (30 Hz) are two timers
   on its single 8 KB adapter task, not separate game/render threads.
   No WiFi calls, NVS writes/reads, SD I/O, blocking audio writes or worker
   waits in callbacks. Boot-time initialization runs before normal gameplay.
2. Workers never call `lv_*`. Queue copied requests and acknowledge completion.
   Main LVGL task uses the supported PSRAM stack API. Do not enable the
   adapter's incompatible LVGL 9.6 draw-thread patch hook.
3. Prefer PSRAM for CPU-only buffers, artwork, PCM and TLS. Ordinary static BSS
   does not move there automatically. Explicit capabilities enforce placement.
   Audio/console stacks use PSRAM; network/save/SD stacks must stay internal.
   IDF's flash driver asserts on external stacks even with XIP enabled.
4. Track internal/DMA `largest`, not just free bytes. Framebuffers already
   use 96,000 bytes in PSRAM; SPI staging/descriptors still require internal DMA.
   The historical acceptance floor is 51,200 bytes; recent firmware holds 63,488.
5. Radio jobs pause the whole display adapter with acknowledgement, drain
   active persistence/audio, do bounded work, stop AND deinit WiFi, then resume.
   Touch/composition hold during the job. Pending save/audio jobs wait.
   Pausing does not defragment memory. Guard station-netif creation once.
6. No saved credentials means no boot WiFi. Otherwise do one RTC/NTP sync
   before display startup and fully tear down the radio. Manual scans/sync/OTA
   never leave it connected. Never join arbitrary discovered open networks.
7. Log every rejection/failure. Do not declare success from a queue acceptance,
   deduplicated NVS write, build alone, or absent crash log.

## Build, flash, verify

Always use wrappers; never invoke `idf.py` directly:

```powershell
powershell -File build.ps1 *> build_out.txt 2>&1; "exit=$LASTEXITCODE"
Get-Content build_out.txt | Select-String -Pattern 'error:|warning:|exited with code'
powershell -File flash.ps1 COM7
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' run_tests.py COM7
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' stability_check.py COM7 40
python cmd.py COM7 "mem"
```

Require build exit 0 with zero main warnings, flash exit 0, soak
`VERDICT: PASS` with zero faults, and `largest >= 51200`. Allocation changes
also require `scanloop 5` and `render 500` with at most 4,096 bytes drift.
Re-read edits, preserve dirty user changes, and append a progress entry with
**Decisions made** and exact evidence.

Keep active `sdkconfig` and defaults aligned; verify generated configuration.
Initial OTA-table migration requires the full flash wrapper, including assets
and table. Do not erase NVS or flash-wide storage.

For stale build processes, inspect and stop only specific PIDs established as
belonging to this build. Do not kill all Python/CMake processes on a shared host.
Redirect output rather than trusting stale terminal scrollback.

## Console and artwork

COM7 is built-in USB Serial/JTAG (303A:1001). Install only its single driver.
No UART driver, competing VFS reader, `fgets()` or polling `getchar()`.
Clients wait for console readiness, up to 45 seconds; opening the port resets
the board. See [serial commands](../docs/serial_console.md).

The spider is a direct-pixel 96 x 96 RGB565A8 canvas. Do not reintroduce
per-frame LVGL blending allocations, widget anatomy or opaque RGB565 clearing.
No object creation/deletion per frame. LVGL is not thread-safe; static point
arrays are required for APIs retaining pointers. Use `lv_obj_set_hidden()`.

Preserve spider artwork. Generate other artwork using the deterministic
[asset pipeline](../assets/README.md) and [style contract](../docs/ART_STYLE.md).
Validate packed QOI entries and decoded pixels, not just PNG names/dimensions.

## Honest verification boundaries

Physical touchscreen appearance, authorized association/SNTP, a uniquely
versioned hosted HTTPS update, SD media and RTC backup-battery retention need
their own evidence. A reset or successful scan does not prove them.
Do not log/commit credentials, claim encrypted NVS, or claim signed firmware
and functional rollback that have not been provisioned.
