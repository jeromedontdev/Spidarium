---
description: "Spidarium ESP32-S3 virtual pet — build, flash, test, and debug on real hardware. Use for any change to the game, renderer, WiFi, or UI."
tools: ['codebase', 'editFiles', 'runCommands', 'search', 'problems', 'runTasks', 'fetch']
---

# Spidarium — agent guide

A virtual jumping-spider vivarium on a Waveshare ESP32-S3-Touch-AMOLED-2.16.
480×480 AMOLED, 16 MB flash, 8 MB octal PSRAM, ESP-IDF v5.5.6, LVGL 9.6.

This file is the operating manual. Read it before changing anything.

---

## 1. The three rules

These are not style preferences. Every one of them was learned by breaking the
device, and each has cost multiple rounds of debugging.

### Rule 1 — Never block the LVGL task

The LVGL task drives the display and has an **8 KB stack**. Blocking it freezes
the UI and the task watchdog reboots the device.

- No `vTaskDelay` loops, no semaphore waits, no `esp_wifi_*` calls, no NVS
  writes on the LVGL task.
- Anything that can take longer than a frame goes on a worker task.
- The UI **polls**; it never waits.

### Rule 2 — LVGL is not thread-safe

`lv_*` may only be called from the LVGL task. Calling `lv_timer_pause()` from a
worker corrupted LVGL's timer list and crashed the device on the second scan.

Cross-thread work is a **request plus an acknowledgement**, never a direct call.

### Rule 3 — Internal DMA RAM is the scarce resource

512 KB internal SRAM, of which the LCD SPI driver needs a large **contiguous**
block for its draw buffers. 8 MB PSRAM is plentiful but **not DMA-capable for
the LCD**.

- `largest` free block matters more than `free`. A fragmented pool reports plenty
  free and still fails one contiguous allocation.
- Anything read/written only by the CPU belongs in PSRAM.
- The radio takes tens of KB of internal DMA RAM. It must not run alongside
  rendering.

---

## 2. Build, flash, verify

**Always use the wrappers.** Never call `idf.py` directly.

```powershell
powershell -File build.ps1              # build
powershell -File flash.ps1 COM7         # flash
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' stability_check.py COM7 40
```

`stability_check.py` boots the device, soaks it, and prints `VERDICT: PASS` or
`FAIL` with a fault count. **A change is not done until this passes.**

### Redirect build output to a file

The terminal replays stale scrollback and can hang on long builds. Always:

```powershell
powershell -File build.ps1 *> build_out.txt 2>&1; "exit=$LASTEXITCODE"
Get-Content build_out.txt | Select-String -Pattern 'error:|exited with code'
```

### When the build fails

| Symptom | Fix |
|---|---|
| `CMake Error: File can't be removed and still exist` | Kill stale `ninja`/`cmake`/`idf`/`python` processes, retry |
| `ninja: error: opening deps log: Permission denied` | Same |
| `undefined reference to lv_draw_sw_init` | Delete `build/esp-idf/lvgl__lvgl`, rebuild |
| Terminal hangs replaying scrollback | Kill the terminal, redirect output to a file |

```powershell
Get-Process | Where-Object { $_.ProcessName -match 'ninja|cmake|idf|esptool|python' } |
    ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
```

---

## 3. Test with the serial console

**Do not guess. Run a command.**

```powershell
python cmd.py COM7 "help"
python cmd.py COM7 "mem"
python cmd.py COM7 "scan"
python cmd.py COM7 "scanloop 5"
python cmd.py COM7 "render 500"
python cmd.py COM7 "open main"
python cmd.py COM7 "set hunger 5"
```

Full reference: **`docs/serial_console.md`**.

The whole device is drivable: `feed`, `clean`, `play`, `sleep`, `menu`,
`open NAME`, `spawn [SPECIES]`, `state N`, `set FIELD VALUE`, `refresh`. Each
calls the same code path the UI calls, so a test here is a test of the real
thing.

The two commands that matter most:

- **`scanloop N`** — runs N scans and fails if the largest free block drifts by
  more than 4 KB. Catches the radio leaking its DMA buffers.
- **`render N`** — runs N render ticks and fails on the same drift. Catches
  per-frame allocations in the renderer.

Both print memory before and after. **A leak shows up as a number that keeps
falling**, not as a corrupted screen thirty seconds later.
`cmd.py` waits 3 s after opening the port, because opening it resets the device.

### The console is one driver, on the USB-JTAG

This board's console is the ESP32-S3's built-in **USB Serial/JTAG**
(`VID:PID=303A:1001`), not the UART bridge. The console installs exactly one
driver and reads it with `usb_serial_jtag_read_bytes()`.

- `fgets()` on stdin does **not** work here — the console VFS does not route
  stdin to this transport, so the task blocks forever.
- `getchar()` returns EOF immediately, so the task spins.
- Installing **both** a UART and a USB-JTAG driver costs ~18 KB of internal RAM
  and drops the largest DMA block from 51200 to 32768. **That corrupted the
  screen when opening Settings.** One driver keeps it at 38912.
- `uart_vfs_dev_use_driver()` must not be called — it competes with the task for
  the same ring buffer.

---

## 4. Memory

Measured baseline at boot:

```
internal  free  60003  largest  51200
dma-cap   free  52739  largest  51200
psram     free 6754008  largest 6684672
```

**Watch `largest`, not `free`.** 51,200 is the figure the LCD's draw buffers
have to fit into.

Full budget: **`docs/memory_budget.md`**.

### Where things live

| Thing | Where | Why |
|---|---|---|
| LCD draw buffers | PSRAM | `use_psram = true`, 96,000 bytes |
| Code + rodata | PSRAM | `SPIRAM_FETCH_INSTRUCTIONS`, `SPIRAM_RODATA` |
| Spider canvas | PSRAM | CPU-only; 96×96 RGB565A8 = 27,648 bytes |
| WiFi RX/TX pools | PSRAM | `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` |
| Task stacks | internal | must be DMA-adjacent |

### `sdkconfig` is the source of truth

**`sdkconfig.defaults` only applies when `sdkconfig` is regenerated.** A setting
that exists only in the defaults is a setting that is not applied.

This cost three rounds of WiFi debugging: `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`
was enabled in the defaults and **disabled in `sdkconfig`**, so WiFi allocated
from internal RAM and failed with `ESP_ERR_NO_MEM` every time.

When changing a config option, **write it to both files** and verify:

```powershell
Select-String -Path build\config\sdkconfig.h -Pattern 'YOUR_OPTION'
```

---

## 5. The renderer

The spider is **one 96×96 canvas**, drawn from scratch each frame. This is the
Pocket Tank architecture: the creature is a picture, not a scene graph.

It replaced ~46 LVGL widgets, which had a `MAX_PARTS` budget that silently
dropped parts, needed a `LV_EVENT_DRAW_MAIN` callback for the fangs, and faked
squash/stretch by moving every part individually.

### Pixels are written directly

`cspider.c` writes into the canvas buffer with `put_px()`, `fill_circle()`,
`draw_line()`, `fill_triangle()`. It does **not** use `lv_draw_rect()` or
`lv_draw_line()`.

That is deliberate. Going through LVGL's draw layer made the software renderer
allocate temporary blending buffers for every primitive every frame. Those came
out of internal DMA RAM and starved the LCD after ~30 s:

```
E spi_master: setup_dma_priv_buffer: Failed to allocate priv TX buffer
E esp_lvgl:bridge_v9: Draw bitmap failed: ESP_ERR_NO_MEM
```

Writing pixels directly allocates nothing.

### Format

`LV_COLOR_FORMAT_RGB565A8` — an RGB565 colour plane followed by an 8-bit alpha
plane, 3 bytes per pixel.

- Plain **RGB565 has no alpha**, so clearing to zero gives opaque black. That was
  the "transparent black box around the spider".
- ARGB8888 works but costs 4 bytes per pixel.
- RGB565A8 gives a real alpha channel for 3.

Zero alpha is transparent. `memset(px_alpha(), 0, ...)` clears the frame.

### What lives where

| File | Owns |
|---|---|
| `cspider.c` | the whole creature: legs, body, fuzz, eyes, fangs, gait, squash/stretch, emote |
| `spider.c` | **only** the slingshot aim visuals; the anatomy builder survives for the setup preview |
| `ui_hud.c` | derives the emote from state, sends it to the canvas |

---

## 6. WiFi

The rule, taken from Pocket Tank:

> **The radio is only on during a check, and the tank is not being drawn while
> it is.**

The radio allocates internal DMA-capable RAM for its RX/TX pools. The LCD SPI
driver needs the same pool. They cannot coexist.

### The scan sequence

1. `net_scan_start()` queues a job and returns immediately.
2. `net_radio_active()` becomes true → `game_tick_render()` stops drawing the
   tank. LVGL still composites, so the menu stays usable.
3. The net task brings the radio up, scans, copies results into `s_scan`.
4. **`esp_wifi_stop()` then `esp_wifi_deinit()`** — on **every** path, including
   failure.
5. `net_radio_active()` becomes false, the tank resumes.

### Two traps

**`esp_wifi_stop()` alone is not enough.** It stops the radio but leaves the
driver's RX/TX pools allocated, so the internal RAM stays gone for the rest of
the session. `esp_wifi_deinit()` is what releases them.

**The teardown must run on failure paths too.** The early returns used to skip
it, so every failed scan left the radio up and the next attempt stacked another
driver on top. A few taps starved the display completely.

### Non-idempotent calls

| Call | Behaviour on repeat |
|---|---|
| `esp_netif_create_default_wifi_sta()` | **ASSERTS** — does not return an error |
| `esp_wifi_init()` | returns `ESP_ERR_NO_MEM` if buffers are gone |

Guard them separately. Treating them as one unit turned a recoverable failure
into a reboot:

```
assert failed: esp_netif_create_default_wifi_sta wifi_default.c:422 (netif)
```

---

## 7. LVGL constraints

| Constraint | Consequence |
|---|---|
| `LV_USE_MATRIX` is **off** | cannot scale objects; move parts individually or use a canvas |
| No triangle widget | use `fill_triangle()` in the canvas, or a draw callback |
| `LV_OPA_*` only in steps of 10 | `LV_OPA_60`, not `LV_OPA_65` |
| `lv_line_set_points()` stores the pointer | point arrays **must be `static`** |
| Hit test walks children in **reverse creation order** | use `lv_obj_move_background()` to be explicit |
| Never create/delete objects per frame | build once, mutate after |
| LVGL adapter task stack is **8 KB** | no WiFi, no NVS, no big locals |

Use `lv_obj_set_hidden()`, not `lv_obj_add_flag(LV_OBJ_FLAG_HIDDEN)`.

---

## 8. Code layout

```
main/
  main.c          BSP bring-up only
  game.c          orchestrator: timers, menus, tap handling, pause
  cspider.c       canvas spider renderer  ← the creature
  spider.c        slingshot aim visuals + setup preview
  behaviour.c     FSM: wander, stalk, aim, pounce, wall avoidance
  prey.c          bugs: spawn, flee, catch resolution
  needs.c         hunger/mood/energy/hygiene decay
  mind.c          intent layer
  gift.c          gift bonuses
  species.c       data-driven bug table
  theme.c         palettes
  terrarium.c     background, decor
  ui_hud.c        buttons, stat rings, emote derivation
  ui_menu.c       generic list menu
  ui_setup.c      first-run colour picker
  net.c           WiFi: scan, connect, credentials
  sd.c            SD card backup/restore
  meminfo.c       memory reporting
  test_console.c  serial command interpreter
  save.c          NVS persistence
  audio.c         I2S codec
  assets.c        QOI sprite partition
```

### Timers

- `game_tick_logic` — 20 Hz. Needs, behaviour, prey, save, heartbeat.
- `game_tick_render` — 30 Hz. Terrarium, canvas, aim visuals, prey, HUD.

`game_tick_render` **returns early while the radio is active**, so the tank is
not drawn while the radio holds the DMA pool.

---

## 9. Working method

### Capture before diagnosing

Two rounds of plausible-sounding WiFi theories were wrong. One 90-second serial
capture found the truth immediately: **zero lines from `net.c`**, while the
heartbeat continued normally. The absence of log lines was the evidence, and it
was available from the start.

When something is broken, **get the log first**. Do not reason from the source
and guess.

### A silent failure is worse than a loud one

`queue_job()` returning `false` with no log is what made one bug take three
rounds. Every rejection now logs. Every failed scan reports its error string in
the menu.

If you write a function that can fail, make it say so.

### Verify every edit

Several edits in this project's history produced corrupted blocks — mangled
comments, dropped lines, merged functions. **Re-read after editing.** Never leave
the tree uncompilable.

### One change at a time

Do not fix a bug and rewrite an architecture in the same round. Two changes means
two things to debug.

### Log every round

Append to `PROGRESS.md` with:

- what was reported
- the root cause
- the fix
- verification (build / flash / stability)
- **decisions made** — the reasoning, not just the diff

---

## 10. Definition of done

A change is done when **all** of these hold:

- [ ] `build.ps1` exits 0 with **zero warnings in `main/`**
- [ ] `flash.ps1 COM7` exits 0
- [ ] `stability_check.py COM7 40` prints `VERDICT: PASS`
- [ ] `python cmd.py COM7 "mem"` shows `largest` at or above baseline
- [ ] If it allocates: `scanloop` / `render` show no drift
- [ ] `PROGRESS.md` has an entry with a "Decisions made" section
- [ ] The tree compiles; no dead code left behind

---

## 11. Reference

- `docs/serial_console.md` — every command, with output examples
- `docs/memory_budget.md` — where every kilobyte goes
- `PROGRESS.md` — every round, with root causes and decisions
- [Pocket Tank](https://github.com/mediacutlet/pocket-tank) — the reference
  architecture: a direct RGB565 software renderer, the radio only during a check,
  and a strict separation between the reflex layer and the decision layer
