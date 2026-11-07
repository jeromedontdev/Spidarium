# Memory budget

Where every kilobyte goes, and why. Written after three separate bugs in this
project turned out to be the same bug: **internal RAM running out**, presenting
as `Draw bitmap failed: ESP_ERR_NO_MEM` and a corrupted screen.

| Bug | What actually happened |
|---|---|
| SD card probing at boot | `esp_vfs_fat_sdmmc_mount()` allocated DMA-capable buffers the LCD driver then could not get |
| WiFi scanning | The radio's RX/TX pools took internal RAM the LCD needs every frame |
| Menu rendering | A 50%-alpha full-screen backdrop forced a full-framebuffer blend every frame |

None of them said "out of memory" in a way that pointed at the cause. This
document exists so the next one is obvious before it ships.

---

## The two pools

The ESP32-S3 has **512 KB of internal SRAM** and, on this board, **8 MB of octal
PSRAM**. They are not interchangeable.

| | Internal SRAM | PSRAM |
|---|---|---|
| Size | 512 KB | 8 MB |
| Speed | Fast, no cache miss | Slower, goes through cache |
| DMA-capable for the LCD | **Yes** | No |
| Task stacks | Yes | Yes (with `stack_in_psram`) |
| WiFi/LWIP buffers | Yes | Yes (with `SPIRAM_TRY_ALLOCATE_WIFI_LWIP`) |

**Internal SRAM is the scarce resource.** Everything that matters competes for
it. PSRAM is plentiful and should absorb anything that does not need to be
internal.

---

## Measured baseline

From `mem_report_detail("boot complete")` on real hardware, with the game
running and WiFi not yet started:

```
internal  free  51983  largest  40960
dma-cap   free  44711  largest  40960
psram     free 6791324  largest 6684672
```

Read this as: **~51 KB of internal RAM free, and the largest single block is
40 KB.** That 40 KB figure is the one that matters — it is what the LCD driver
has to fit a DMA buffer into.

### Why "largest block" and not "free"

A pool can report plenty free and still fail one large allocation if it is
fragmented. The LCD needs a contiguous DMA buffer, so `largest` is the number
that predicts whether the display will work. `mem_report_detail()` warns when it
drops below 32 KB.

---

## Flash

| Region | Size | Used | Notes |
|---|---|---|---|
| `factory` app | 8 MB | 1.54 MB | `spidarium.bin` = 0x18ae70 = 1,617,520 bytes |
| `storage` (SPIFFS) | 7 MB | 50 KB | QOI sprites, mmap'd |
| `nvs` | 24 KB | < 4 KB | game save + WiFi credentials |
| `phy_init` | 4 KB | — | |

**81% of the app partition is free** (0x675190 bytes). Flash is not a constraint.
This is the opposite of Pocket Tank, which fits a 7.56 MB model into its budget —
they had to care, we do not.

---

## Internal SRAM consumers

Approximate, in descending order of size. The first three are the ones that
have actually caused bugs.

### 1. WiFi and LWIP — up to ~40 KB, now in PSRAM

The radio's RX/TX pools are the single largest consumer. Defaults were 10 static
RX + 32 dynamic RX + 32 dynamic TX buffers.

**Fixed** by `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`, which moves them to PSRAM.
Also reduced the pool sizes, since this device sends almost nothing:

```
CONFIG_ESP32_WIFI_STATIC_RX_BUFFER_NUM   10 -> 4
CONFIG_ESP32_WIFI_DYNAMIC_RX_BUFFER_NUM  32 -> 16
CONFIG_ESP32_WIFI_DYNAMIC_TX_BUFFER_NUM  32 -> 16
```

**Rule: WiFi must never take internal RAM.** If a future change reintroduces
that, the display breaks.

### 2. LCD draw buffers — ~48 KB, must be internal

The panel is 480×480 RGB565. The board config uses `buffer_height = 50` with
`require_double_buffer = true`:

```
480 px wide × 50 px tall × 2 bytes × 2 buffers = 96,000 bytes
```

These are allocated with `MALLOC_CAP_INTERNAL` because the SPI driver DMAs from
them. **This is the allocation that fails first**, and it is why the 40 KB
largest-block figure matters.

### 3. LVGL heap — grows with object count

`CONFIG_LV_USE_BUILTIN_MALLOC` is off, so LVGL uses the C library allocator and
its objects come from the general heap. The project creates:

```
26 x lv_obj_create      (spider parts, terrarium layers, panels)
21 x lv_label_create
 8 x lv_button_create
 3 x lv_line_create
 2 x lv_arc_create
 2 x lv_image_create
```

The spider alone registers **46 parts** (`MAX_PARTS = 2*FUZZ_COUNT + 18`), each
an `lv_obj`. That is the largest single block of LVGL objects in the project and
it is why `MAX_PARTS` silently dropping parts was such a persistent bug.

`CONFIG_LV_DRAW_LAYER_SIMPLE_BUF_SIZE = 24576` — LVGL allocates a 24 KB scratch
buffer for layers. This is internal RAM.

### 4. Task stacks

| Task | Stack | Where |
|---|---|---|
| LVGL worker | 8 KB | internal |
| `spidarium_net` | 8 KB | internal |
| main / idle / timer | IDF defaults | internal |

The LVGL worker's 8 KB is why WiFi calls cannot run on it — `esp_wifi_init()`
needs more. That is why `net.c` has its own task.

### 5. Everything else

Audio (I2S buffers), the game state struct (~200 bytes), the prey array
(3 × ~400 bytes), the terrarium's static point arrays. Together these are small
— a few KB — and have never caused a problem.

---

## PSRAM consumers

Barely used. 6.79 MB free of 8 MB.

| Consumer | Size |
|---|---|
| LVGL frame buffers | ~96 KB (if `use_psram` is honoured) |
| WiFi/LWIP pools | ~40 KB |
| Everything else | negligible |

**There is 6.7 MB of headroom.** Any future feature that needs a large buffer
should go here, not into internal RAM.

---

## Rules

These are the rules that would have prevented all three bugs.

1. **Anything over ~4 KB that is not DMA'd by the LCD goes in PSRAM.**
   Use `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`.

2. **Never allocate internal RAM at boot for something optional.**
   SD card, WiFi, and anything else the player might not use should be
   initialised on demand. Both of those caused bugs by probing at boot.

3. **Check `largest`, not `free`.** A fragmented pool fails large allocations
   while looking healthy. `mem_report_detail()` prints both.

4. **Call `mem_report_detail()` after adding any subsystem.** The boot line is
   the baseline; a new line that drops `largest` below 32 KB is a bug.

5. **A full-screen translucent overlay is expensive.** It forces LVGL to blend
   the whole framebuffer every frame. Use an opaque backdrop.

---

## Where this is heading

The long-term goal is to move toward Pocket Tank's architecture. Their split is
worth understanding because it is a direct answer to the problems this project
keeps hitting:

| | Spidarium now | Pocket Tank |
|---|---|---|
| Rendering | LVGL scene graph, ~60 objects | Hand-written RGB565 software renderer |
| Spider/fish | 46 `lv_obj` parts, repositioned per frame | Drawn into a framebuffer each frame |
| Menus | LVGL widgets | Same renderer, pixel font |
| Model | none | 7.56 MB 4-bit LLM in flash, mmap'd |
| Internal RAM | ~51 KB free, 40 KB largest block | Documented per-stage budget |

**What that would buy us:** no `MAX_PARTS` budget, no draw callbacks for
triangles, no `LV_USE_MATRIX` limitation on scaling, and no scene-graph overhead
for what is really a sprite. The spider becomes a few dozen `fill_circle` calls.

**What it would cost:** rewriting `spider.c`, `terrarium.c`, `prey.c` rendering
and the HUD. Roughly a week. Not worth doing for its own sake, but worth doing
if the spider's rendering keeps fighting the toolkit.

**What to take from them now, without a rewrite:**

- This document, and the `mem_report_detail()` calls that keep it honest.
- Their habit of a per-stage frame profile in the boot log.
- Their rule that the model owns its decisions with no fallback heuristics —
  the same principle as `mind.c` publishing intent without driving the FSM.

---

## Quick reference

```
mem_report_detail("where")   // full breakdown, warns below 32 KB largest
mem_report("where")          // one line, for frequent checkpoints
```

Expected boot output:

```
--- memory at boot complete ---
  internal  free  51983  largest  40960
  dma-cap   free  44711  largest  40960
  psram     free 6791324  largest 6684672
```

If `internal largest` is below 32768, the LCD will start dropping frames.
