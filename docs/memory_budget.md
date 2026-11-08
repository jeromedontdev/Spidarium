# Memory budget

## Policy

8 MB octal PSRAM absorbs CPU-only allocations. Internal SRAM (512 KB total)
must retain contiguous DMA-capable blocks for SPI and hardware-required radio
buffers. `free` alone is insufficient: record `largest` before and after
operations. Pausing does not defragment or release existing allocations.

| Consumer | Placement |
|---|---|
| LCD framebuffers, two 480 x 50 x 2 buffers | PSRAM, 96,000 bytes |
| Spider RGB565A8 canvas, 96 x 96 x 3 | PSRAM, 27,648 bytes |
| Enlarged equipped gift, 64 x 64 x 3 | Explicit PSRAM, 12,288 bytes; reused |
| Battery worker stack | Explicit PSRAM, 3,072 bytes; no flash writes |
| Static RGB565 pixel-art habitat, 480 x 480 x 2 | Explicit PSRAM, 460,800 bytes, allocated once |
| Eight authored enclosure source images, 240 x 240 x 3 each | Explicit PSRAM, 1,382,400 bytes total, decoded at boot |
| Decoded art and general CPU heap | PSRAM preferred |
| Audio / console stacks | Explicit PSRAM capability allocation |
| Console command stack (browser restore and screen capture) | PSRAM, 8,192 bytes; measured minimum-free headroom |
| Explicit diagnostic screen snapshot | Temporary PSRAM, 480x480 RGB565 or 96x96 ARGB; freed after transfer |
| Network / save / SD worker stacks | Internal: flash operations disable caches |
| Synthesized PCM audio buffer | PSRAM, 12,800 bytes |
| Main LVGL adapter stack | PSRAM through `stack_in_psram` |
| LVGL draw-worker stacks / task control blocks | Internal |
| SPI descriptors and DMA staging | Internal DMA-capable |
| Eligible WiFi/LwIP buffers | PSRAM preference; not every radio buffer |
| TLS working allocations | PSRAM (`MBEDTLS_EXTERNAL_MEM_ALLOC`) |
| Instructions and rodata | PSRAM mapping enabled |
| Ordinary writable static BSS | Internal unless explicitly attributed |

`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=0` prefers PSRAM for ordinary allocations.
`CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=65536` reserves internal memory for
capability-specific consumers; it does not exclude WiFi from that pool.
Static WiFi RX/TX counts are four, dynamic RX/TX sixteen.
External-stack permission alone does not relocate any task.
Keep both configuration files aligned and inspect `build\config\sdkconfig.h`.

The adapter's draw-thread PSRAM Kconfig option fails its LVGL 9.6 source-layout
check. Leave it disabled; the supported main-task API is independent.

## Measurements and gate

The historical acceptance target is internal/DMA `largest >= 51200` bytes,
including after menu, rendering and repeated scanning tests. Drift allowance
for `scanloop` and `render` is at most 4,096 bytes. Do not lower a test threshold
to hide a regression.

Round 28, after the persistent HUD, modal gates and static PSRAM background:

| Point | Internal free | DMA free | Largest |
|---|---:|---:|---:|
| Console before tests | 183,735 | 175,947 | 81,920 |
| Render before | 183,991 | 176,203 | 81,920 |
| Render after | 183,991 | 176,203 | 81,920 |
| First scan teardown | 160,391 | 152,603 | 63,488 |
| Five additional scans | 160,391 | 152,603 | 63,488 |
| After 44 console checks | 172,423 | 164,635 | 81,920 |
| Standalone memory check after reset | 169,399 | 161,611 | 63,488 |

Final `render 500` and `scanloop 5` both reported zero largest-block drift.
All 44 success/rejection checks passed; a 40-second soak had zero faults.
Snapshots include concurrent driver/background activity and can differ;
do not infer individual allocation causes from these totals alone.
First-time network infrastructure consumes free bytes and may reduce the largest
block once (81,920 to 63,488 observed); subsequent five scans stayed stable.
The 51,200-byte gate is not weakened. The eight-scene background reuses its
460,800-byte PSRAM buffer; changing scenes performs no new canvas allocations.
Before tuning, recovered firmware had largest 30,720 at boot and 26,624 after
its first scan; that build failed the baseline gate and was not accepted.
Use the newest [progress evidence](../PROGRESS.md) for complete firmware results.

Round 29 replaces procedural backgrounds with eight packed images and makes
the Care toggle a small icon with aligned status bars. Final COM7 measurements:

| Point | Internal free | DMA free | Largest |
|---|---:|---:|---:|
| Console before regression suite | 167,983 | 160,195 | 63,488 |
| Quiescent five-scan baseline | 176,639 | 168,851 | 73,728 |
| Quiescent five-scan end | 176,639 | 168,851 | 73,728 |
| After 39 console cases | 173,311 | 165,523 | 73,728 |
| Standalone post-reset detailed report | 169,007 | 161,219 | 63,488 |

Final render/scan-loop drift was zero; 40-second soak passed with zero faults.
All eight scene preferences survived reset and passed exact-pixel diagnostics.
Approximately 3.01 MB PSRAM remains free. All images are decoded before LVGL:
background selection only copies source pixels into the existing canvas.

Live SPI transfers temporarily allocate/free internal DMA staging buffers, so
different unsynchronized memory reads can differ within one console report.
The scan-loop benchmark now quiesces gameplay through the LVGL task and lets
pending transfers finish before comparing idle snapshots. Its 4KB threshold
was not weakened, and the separate live 51,200-byte floor still applies.

## Flash budget

| Region | Offset | Size |
|---|---:|---:|
| NVS | 0x9000 | 24 KB |
| PHY | 0xf000 | 4 KB |
| OTA metadata | 0x10000 | 8 KB |
| Factory app | 0x20000 | 3 MB |
| OTA 0 | 0x320000 | 3 MB |
| OTA 1 | 0x620000 | 3 MB |
| Asset storage | 0x920000 | 6 MB |

The integrated app is 1,886,816 bytes. Asset partition capacity is not the
packed image size: the final 123-entry MMAP write is 155,620 bytes.
Initial migration must flash the table and assets with the app.

## Repeatable checks

```powershell
python cmd.py COM7 "mem"
python cmd.py COM7 "scanloop 5"
python cmd.py COM7 "render 500"
python run_tests.py COM7 results.txt --gameplay
```

Use [whole-game architecture](PSRAM_THREADING_ARCHITECTURE.md) for ownership,
and [serial console](serial_console.md) for output interpretation.
