# Spidarium

A virtual jumping-spider vivarium on a Waveshare **ESP32-S3-Touch-AMOLED-2.16**.

480×480 AMOLED, 16 MB flash, 8 MB octal PSRAM, ESP-IDF v5.5.6, LVGL 9.6.

You keep a spider. It wanders, hunts, sleeps, moults and gets sick if you neglect
it. You feed it by slingshotting it at bugs, clean the tank, play with it, and
give it gifts. It remembers all of it across reboots.

---

## What makes it interesting

**The spider is one canvas, not a scene graph.** It is drawn from scratch into a
single 96×96 RGB565A8 buffer every frame — legs, body, fuzz, eyes, fangs, gait,
squash/stretch. This replaced ~46 LVGL widgets and is the architecture
[Pocket Tank](https://github.com/mediacutlet/pocket-tank) uses: the creature is a
picture, not a widget tree.

**Pixels are written directly.** `cspider.c` does not call `lv_draw_rect()` or
`lv_draw_line()`. Going through LVGL's draw layer made the software renderer
allocate temporary blending buffers for every primitive every frame; those came
out of internal DMA RAM and starved the LCD after about thirty seconds.

**The radio is only on during a check.** WiFi allocates internal DMA-capable RAM
for its RX/TX pools, and the LCD SPI driver needs the same pool. They cannot
coexist, so a scan stops the tank rendering, does its work, and tears the driver
down completely — on every path, including failure.

**The whole device is drivable over serial.** Every UI action has a console
command, so a feature can be tested from a script instead of by tapping.

---

## Hardware

| | |
|---|---|
| Board | Waveshare ESP32-S3-Touch-AMOLED-2.16 |
| Display | 480×480 AMOLED (CO5300), SPI |
| Touch | CST9217, I²C |
| Flash | 16 MB |
| PSRAM | 8 MB octal @ 80 MHz |
| Audio | ES8311 codec, I²S |

---

## Build

Requires ESP-IDF v5.5.6.

```powershell
powershell -File build.ps1
powershell -File flash.ps1 COM7
```

`build.ps1` and `flash.ps1` are thin wrappers around `idf.py` that set up the
environment. Use them rather than calling `idf.py` directly.

### First build

`sdkconfig` is **not** committed — it is generated from `sdkconfig.defaults` on
the first build. That file carries every setting the project needs, including the
ones that are easy to lose:

- `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` — without it WiFi allocates from
  internal RAM and fails with `ESP_ERR_NO_MEM`
- `CONFIG_LV_USE_CANVAS=y` and `CONFIG_LV_DRAW_SW_SUPPORT_RGB565A8=y` — the
  renderer does not work without them
- `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=65536` — keeps a floor of internal RAM
  the radio cannot take, so the display always has DMA memory

**A setting that exists only in `sdkconfig.defaults` is not applied until
`sdkconfig` is regenerated.** If you change a config option, write it to the
defaults and delete `sdkconfig`.

---

## Test

The device has a serial command console. This is the primary way to verify a
change — not tapping the screen.

```powershell
python cmd.py COM7 "help"
python cmd.py COM7 "mem"
python cmd.py COM7 "scan"
python cmd.py COM7 "scanloop 5"
python cmd.py COM7 "render 500"
python cmd.py COM7 "open wifi"
python cmd.py COM7 "set hunger 5"
```

Run the whole suite:

```powershell
python run_tests.py COM7
```

Soak the device and check for faults:

```powershell
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' stability_check.py COM7 40
```

Full command reference: **[docs/serial_console.md](docs/serial_console.md)**.

The two commands that matter most:

- **`scanloop N`** — runs N scans and fails if the largest free block drifts by
  more than 4 KB. Catches the radio leaking its DMA buffers.
- **`render N`** — runs N render ticks and fails on the same drift. Catches
  per-frame allocations in the renderer.

Both print memory before and after. A leak shows up as a number that keeps
falling, not as a corrupted screen thirty seconds later.

---

## Memory

Measured at boot:

```
internal  free  57451  largest  47104
dma-cap   free  49663  largest  47104
psram     free 6758372  largest 6684672
```

**Watch `largest`, not `free`.** A fragmented pool reports plenty free and still
fails one contiguous DMA allocation. The LCD's draw buffers need one large
contiguous block, so a falling `largest` is the early warning.

Where things live:

| Thing | Where | Why |
|---|---|---|
| LCD draw buffers | PSRAM | `use_psram = true`, 96,000 bytes |
| Code + rodata | PSRAM | `SPIRAM_FETCH_INSTRUCTIONS`, `SPIRAM_RODATA` |
| Spider canvas | PSRAM | CPU-only; 96×96 RGB565A8 = 27,648 bytes |
| WiFi RX/TX pools | PSRAM | `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` |
| Task stacks | internal | must be DMA-adjacent |

Full budget: **[docs/memory_budget.md](docs/memory_budget.md)**.

---

## Layout

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

`game_tick_render` returns early while the radio is active, so the tank is not
drawn while the radio holds the DMA pool.

---

## The three rules

These are not style preferences. Each was learned by breaking the device.

### 1. Never block the LVGL task

It drives the display and has an **8 KB stack**. Blocking it freezes the UI and
the task watchdog reboots the device. Anything that can take longer than a frame
goes on a worker task. The UI polls; it never waits.

### 2. LVGL is not thread-safe

`lv_*` may only be called from the LVGL task. Cross-thread work is a **request
plus an acknowledgement**, never a direct call. This includes the serial console:
every command that touches the UI is marshalled onto the LVGL task.
### 3. Internal DMA RAM is the scarce resource

512 KB internal SRAM, of which the LCD SPI driver needs a large **contiguous**
block. 8 MB PSRAM is plentiful but **not DMA-capable for the LCD**. Anything
read and written only by the CPU belongs in PSRAM.

---

## Documentation

- **[docs/serial_console.md](docs/serial_console.md)** — every console command,
  with real output, the output contract, and the transport traps
- **[docs/memory_budget.md](docs/memory_budget.md)** — where every kilobyte goes
- **[PROGRESS.md](PROGRESS.md)** — every development round, with root causes and
  the reasoning behind each decision
- **[.github/copilot-instructions.md](.github/copilot-instructions.md)** — the
  agent operating manual

---

## Status

Working: the spider, the hunt, needs and decay, gifts, achievements, themes,
first-run setup, save/restore, SD backup, WiFi scan, the serial console.

Not done: WiFi password entry (secured networks are listed but not selectable),
OTA updates.

---

## Credit

The architecture follows [Pocket Tank](https://github.com/mediacutlet/pocket-tank)
by mediacutlet: a direct RGB565 software renderer, the radio only during a check,
and a strict separation between the reflex layer and the decision layer. Several
of the hard-won lessons in `PROGRESS.md` are that project's lessons, relearned.
