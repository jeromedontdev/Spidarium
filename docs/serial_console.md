# Serial test console

A command interpreter on the console UART, so the device can be driven and
inspected from a script instead of by tapping the screen.

This exists because several bugs in this project were **invisible from the
outside**. A scan that silently returned nothing, a radio that leaked its
buffers, a display that starved thirty seconds in — all of those are trivially
visible from a command that runs the thing and prints what happened. Guessing at
them from the source cost several rounds; a single command would have found each
one immediately.

---

## Running a command

```
python cmd.py COM7 "help"
python cmd.py COM7 "scan"
python cmd.py COM7 "scanloop 5"
python cmd.py COM7 "render 500"
```

`cmd.py` opens the port, **waits 3 seconds for the boot to finish**, sends the
command, and reads until it sees a line starting with `OK` or `ERR`.

The wait matters: opening the serial port toggles DTR/RTS, which resets the
ESP32-S3. Anything written during the ~2.6 s boot window is lost, so without the
wait every command looks like a timeout.

Exit code is `0` if the reply ended with `OK`, `1` if it ended with `ERR` or
timed out. That makes it usable directly from a test script.

To run the whole suite at once:

```
python run_tests.py COM7
```

That runs `help`, `mem`, `stat`, `radio`, `render 200`, `scan`, `scanloop 3` and
`mem`, prints each result, and writes everything to `test_results.txt`. It ends
with `SUMMARY passed=N failed=N`.

### Which port is the console on?

**This board's console is the ESP32-S3's built-in USB Serial/JTAG**, not the UART
bridge. On the development machine COM7 is `VID:PID=303A:1001`, which is the
USB-JTAG.

Check which port you have:

```
python -c "import serial.tools.list_ports as lp; [print(p.device, p.hwid) for p in lp.comports()]"
```

The console installs **exactly one driver**, for the transport actually in use:

```c
usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
usb_serial_jtag_driver_install(&cfg);
```

and reads it with `usb_serial_jtag_read_bytes()`.

### Why not `fgets()` on stdin?

`fgets()` is the standard ESP-IDF console path and it is what Pocket Tank uses,
but it does **not** work on this board: the console VFS does not route stdin to
the USB-JTAG transport, so the task blocks forever while the device runs
normally. `getchar()` is worse — it returns EOF immediately when nothing is
pending, so the task spins.

### Why only one driver?

An earlier version installed **both** a UART driver and a USB-JTAG driver and
polled both. That worked, but it cost about 18 KB of internal RAM and dropped the
largest free DMA block from 51200 to 32768 — the pool the LCD needs for its draw
buffers. **That is what corrupted the screen when opening Settings.**

One driver, for the port in use, keeps `largest` at 38912.
`uart_vfs_dev_use_driver()` must **not** be called: it routes stdin through the
same ring buffer the console task reads, and the two compete for the bytes.

You can also just open a serial terminal at 115200 and type commands.

---

## Output contract

Every command prints one or more lines of detail, then exactly one terminator:

- `OK <command>` — the command succeeded
- `ERR <reason>` — the command failed, with the reason

A script can therefore tell success from failure without parsing prose. Detail
lines use a short uppercase tag so they are greppable:

| Tag | Meaning |
|---|---|
| `MEM` | a memory reading |
| `STAT` | game state |
| `RADIO` | radio state |
| `SCAN` | scan result |
| `LOOP` | scan-loop summary |
| `RENDER` | render-loop summary |
| `SPIDER` | spider position and state |
| `PREY` | prey count and nearest position |

---

## Commands

### `help`

Lists every command. Prints `OK help`.

```
OK commands: help mem stat scan scanloop N radio render N spider prey save reset reboot
```

---

### `mem`

Prints the full memory report and a one-line summary.

```
MEM cmd internal=60003 largest=51200 dma=52739 psram=6754008
OK mem
```

**`largest` is the number that matters**, not `internal`. A fragmented pool can
report plenty free and still fail one contiguous DMA allocation — which is
exactly how the display corruption presented. The LCD's draw buffers need one
large contiguous block, so a falling `largest` is the early warning.

See `docs/memory_budget.md` for where every kilobyte goes.

---

### `stat`

Prints the game state: position, heading, needs, level, stage, prey count.

```
STAT state=4 pos=(126,391) heading=1.57 hunger=21 mood=0 energy=9 hygiene=3 level=20 stage=1 sick=1 bond=0 prey=1
OK stat
```

Useful for confirming the simulation is actually running: if `pos` never changes
between two calls, the logic timer is not ticking.

---

### `radio`

Reports whether the radio is up, whether a job is running, and the connection
state.

```
RADIO active=0 busy=0 state=0 status="off"
OK radio
```

`active` is `net_radio_active()` — true while a radio job is running. **The tank
must not be drawn while this is true**; see the architecture note below.

---

### `scan`

Runs one WiFi scan and reports everything about it: how long it took, how many
networks, the error string, every AP found, and the memory before and after.

```
MEM scan-before internal=60003 largest=51200 dma=52739 psram=6754008
SCAN took=2140ms found=3 err=""
  AP 0 ssid="HomeNet" rssi=-47 secure=1
  AP 1 ssid="Guest" rssi=-71 secure=0
  AP 2 ssid="IoT" rssi=-83 secure=1
MEM scan-after internal=60003 largest=51200 dma=52739 psram=6754008
OK scan found 3
```

**The before/after memory lines are the point.** They must be identical. If
`largest` falls after a scan, the radio did not release its buffers — which is
the bug that corrupted the display.

A failed scan reports why:

```
SCAN took=180ms found=0 err="ESP_ERR_NO_MEM"
ERR scan failed: ESP_ERR_NO_MEM
```

This is the difference between "found nothing" and "could not scan", which used
to be indistinguishable and cost several rounds of debugging.

---

### `scanloop N`

Runs `N` scans back to back (1–20) and checks the memory does not drift.

```
MEM loop-before internal=60003 largest=51200 dma=52739 psram=6754008
  0: found=3 err="" largest=51200
  1: found=3 err="" largest=51200
  2: found=3 err="" largest=51200
MEM loop-after internal=60003 largest=51200 dma=52739 psram=6754008
LOOP ok=3 failed=0 largest_drift=0
OK scanloop
```

Fails if `largest` drops by more than 4 KB across the loop:

```
ERR memory leaked 8192 bytes across 5 scans
```

**This is the command that would have caught the radio leak.** The failure mode
was not a crash — it was the display dying thirty seconds later, far from the
cause. A drift check makes it immediate and local.

---

### `render N`

Runs `N` render ticks (1–2000) and checks the memory does not drift.

```
MEM render-before internal=60003 largest=51200 dma=52739 psram=6754008
MEM render-after internal=60003 largest=51200 dma=52739 psram=6754008
RENDER ticks=500 largest_drift=0
OK render
```

Fails if `largest` drops by more than 4 KB:

```
ERR render leaked 12288 bytes over 500 ticks
```

**This is the command that would have caught the per-frame draw-layer
allocations.** The canvas renderer originally went through LVGL's draw layer,
which allocated temporary blending buffers for every primitive every frame. Those
came out of internal DMA RAM and starved the LCD after about thirty seconds.
Rendering 500 ticks in a loop makes that leak obvious in two seconds.

---

### `spider`

Prints the spider's position, heading, state, colour and sickness.

```
SPIDER pos=(126,391) heading=1.57 state=4 colour=2 sick=0
OK spider
```

---

### `prey`

Prints the prey count and the nearest bug's position.

```
PREY count=2
  nearest=(210,180)
OK prey
```

---

### `save`

Forces a save to NVS. Prints `OK save`.

---

### `reset`

Erases the save. Prints `OK reset (reboot to take effect)`. The device keeps
running with the old state in memory until it reboots.

---

### `reboot`

Restarts the device. Prints `OK rebooting` first, then restarts.

---

## Device control

These drive the game the way the on-screen buttons do, so a feature can be
exercised from a script without tapping. Each calls the same code path the UI
calls, so a test here is a test of the real thing.

### `feed` / `clean` / `play` / `sleep`

Press the corresponding HUD button.

```
>>> feed
OK feed
```

### `menu`

Opens the main menu — the same thing the hamburger button does. Useful for
testing menu rendering without tapping.

```
>>> menu
OK menu
```

### `open NAME`

Opens a specific UI surface:

| Name | Opens |
|---|---|
| `main` | the main menu |
| `gift` | the gift picker |
| `chrome` | the HUD buttons and stat rings |
| `nochrome` | hides the HUD |

```
>>> open main
OK open main
```

### `spawn [SPECIES]`

Spawns a bug. With no argument, a random unlocked species. With an index, that
species specifically (0 fly, 1 cricket, 2 moth, 3 beetle).

```
>>> spawn
OK spawn (random)
>>> spawn 3
OK spawn species=3
```

### `state N`

Forces the spider into a behaviour state, so a state's rendering can be
inspected without waiting for it to happen naturally.

| N | State |
|---|---|
| 0 | idle |
| 1 | wander |
| 2 | stalk |
| 3 | aim |
| 4 | pounce |
| 5 | eat |
| 6 | sleep |
| 7 | play |
| 8 | molt |
| 9 | sick |

```
>>> state 6
OK state=6
```
### `set FIELD VALUE`

Sets a need directly, so a UI state can be reached without waiting for it to
decay. Value is clamped to 0..100.

Fields: `hunger`, `mood`, `energy`, `hygiene`, `bond`, `sick`.

```
>>> set hunger 5
OK set hunger=5
>>> set sick 1
OK set sick=1
```

### `refresh`

Rebuilds the visual layers — re-applies the palette and rebuilds the terrarium.
Use after changing colours or reloading a save.

```
>>> refresh
OK refresh
```

---

## Architecture note: the radio and the display

The rule this device follows, taken from Pocket Tank:

> **The radio is only on during a check, and the tank is not being drawn while
> it is.**

The radio allocates internal DMA-capable RAM for its RX/TX pools. The LCD SPI
driver needs the same pool for its draw buffers. They cannot coexist.

So a scan:

1. Queues a job on the net task. The caller returns immediately.
2. `net_radio_active()` becomes true, and `game_tick_render()` stops drawing the
   tank. LVGL still composites, so the menu stays usable.
3. The net task brings the radio up, scans, copies the results out.
4. **`esp_wifi_stop()` then `esp_wifi_deinit()`** — on every path, including
   failure. `stop()` alone leaves the driver's pools allocated, so the internal
   RAM stays gone for the rest of the session.
5. `net_radio_active()` becomes false and the tank resumes.
The teardown running on the failure paths is not incidental. The early returns
used to skip it, so every failed scan left the radio up and the next attempt
stacked another driver on top. A few taps was enough to starve the display
completely — which is exactly the "click it a couple more times and rendering
goes berserk" report.

---

## Adding a command

1. Write a `static void cmd_foo(void)` in `main/test_console.c`.
2. Print detail lines, then exactly one `OK foo` or `ERR reason`.
3. Add an `else if (!strcmp(line, "foo")) cmd_foo();` to `run_line()`.
4. Add it to `cmd_help()`.
5. Document it here.

Keep commands **idempotent and non-blocking where possible**. A command that
blocks the console task is fine (it has its own task); a command that blocks the
LVGL task is not.

For anything that allocates, print the memory before and after and fail on
drift. That pattern is what makes these commands worth having.
