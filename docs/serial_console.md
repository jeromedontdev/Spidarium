# Serial test console

A command interpreter on USB Serial/JTAG, so the device can be driven and
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
python cmd.py COM7 "silk add 5000"
```

`cmd.py` opens the port, waits up to **45 seconds for console readiness**, sends
the command, and reads until it sees a line starting with `OK` or `ERR`.

Default command timeout is 30 seconds, or 150 seconds for `render` because
forced renders run on LVGL service ticks. An optional third argument overrides
the timeout: `python cmd.py COM7 "render 2000" 150`.

With saved WiFi credentials, boot includes a bounded sync before display startup
and may exceed three seconds. Readiness is `OK console ready`, or a reply to
one help probe if opening the port did not reset the board.

The wait matters: opening the port can reset the board. Commands sent before
the console driver starts can be lost.

Exit code is `0` if the reply ended with `OK`, `1` if it ended with `ERR` or
timed out. That makes it usable directly from a test script.

To run the whole suite at once:

```
python run_tests.py COM7
```

The suite has 52 cases covering rendering, five scans, radio-off assertions,
all gift bonuses, audio/care and committed save, marshalled debug actions,
menu/editor opens, Silk-grant rejection paths and memory readings.
It asserts `largest >= 51200`, checks fault logs, and writes `test_results.txt`.
Pass a second argument to choose the output path. Host parser tests:
`python tools\test_console_client.py`. The soak tool also exits nonzero for
faults or a silent port. Examples below are illustrative/historical; use
[the latest progress round](../PROGRESS.md) for current measurements.

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

`fgets()` is the standard ESP-IDF console path, but it does **not** work on this
board: the console VFS does not route stdin to
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
| `BUTTON` | one physical button's raw level and polarity |
| `BUTTONS` | lock state and volume |

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
exactly how the display corruption presented. Framebuffers already use PSRAM;
SPI transfer staging still needs internal DMA blocks.

See `docs/memory_budget.md` for where every kilobyte goes.

The render benchmark now uses the same gameplay-quiescence request/restore
handshake as `scanloop`, waiting 100ms for pending SPI transfers before both
snapshots. One unmeasured priming frame puts both snapshots under the same
canvas-render workload, then exactly N measured renders run on LVGL.
This avoids comparing an idle heap against render-time allocations; the 4KB
drift limit and 51,200-byte largest-block floor are unchanged.

---

### `stat`

Prints the game state: position, heading, needs, level, stage, prey count, plus
the Clean predicate and the daily loop.

```
STAT state=4 pos=(126,391) heading=1.57 hunger=21 mood=0 energy=9 hygiene=3 level=20 stage=1 sick=1 bond=0 prey=1
CARE clean_needed=1 dirt=12 clean_xp=2
DAILY day=1 claimed=0 streak=0 task=1 task_done=0 progress=0/2 ready=0 silk=24 base_day=1 base=0 counters=0/0/0/0 pet_xp=0/10
OK stat
```

`CARE` reports whether there is anything to clean, the current mess in dirt
units, and the XP a clean would pay. `DAILY` reports the in-game day, whether
today's check-in is claimed, the streak, which rotating task is on offer, its
progress and whether it is ready to pay, plus the task baseline and the day's
petting budget (`pet_xp=N/10`).

Useful for confirming the simulation is actually running: if `pos` never changes
between two calls, the logic timer is not ticking.

---

### `silk add N`

Add testing currency to the **current** pet without replacing its save:

```powershell
python cmd.py COM7 "silk add 5000"
python cmd.py COM7 "stat"
```

This adds 5,000 Silk, rather than setting the total to 5,000. Run it only once
per intended grant. `N` must be a whole number from 1 to 2,147,483,647; malformed
values and grants that would overflow the uint32 balance are rejected.
`set silk 5000` is not supported.

The change runs on LVGL and queues persistence to the save worker. The console
waits for completed persistence before replying, for example
`OK silk added=5000 total=5024 saved=1`. No reboot or separate `save` is needed.
If an error reports an incomplete save, inspect `stat` before retrying: the RAM
balance may already have changed. This does not unlock level-gated items.

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

**The before/after memory lines are the point.** One-time netif/event setup
can reduce free bytes. After warm-up, repeated scans should remain stable;
`largest` must stay above the baseline and loop drift within 4 KB. A falling
number requires allocation/teardown evidence, not an automatic leak diagnosis.
The verified firmware holds `largest=63488` with zero scan-loop drift.

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
The benchmark temporarily quiesces gameplay through an LVGL request and waits
100ms on the console worker for queued LCD transfers to finish before taking
idle-display snapshots. This avoids comparing a live SPI DMA staging buffer
with an idle heap and falsely reporting a leak. Menus close; prior pause/Care
visibility is restored. The live memory floor and 4KB drift limit are unchanged.
Any failed scan or failure to restore gameplay reports `ERR`, not success.

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
One priming frame precedes the measured N ticks. Menus/minigames close and
their timers are cancelled; prior gameplay pause/Care visibility is restored.

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
Rendering 500 ticks requests one extra canvas frame per LVGL service tick
(roughly 17 seconds), not a tight worker-side loop. The client uses a longer
timeout for this real frame-paced check.

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

### `uitest`

Runs diagnostics on the LVGL task: real paged-card/buttonmatrix callbacks,
last-page selection, blank-panel/backdrop taps that do not navigate, explicit
Back navigation, no disabled dash, QWERTY/case/symbol/backspace editing, the 63-character
password limit, Show/Hide password, page-ear Care/Gift overlay, centered status
bars with non-overlapping labels and correct values, eight unique enclosures
and exact packed-background pixels scaled to the 4x4 grid. It uses synthetic text, discards the editor,
and never attempts a connection or logs passwords.
It also checks that the HUD layer is above creatures and simulates 120 seconds
of actual hunger decay, restoring fullness and its fractional accumulator.
It verifies 76px cards, 56px navigation controls, 64px care buttons and red
destructive text. Minigame checks cover all three eight-round games, correct/
incorrect and early taps, timeouts, cancellation, timer cleanup, duplicate-tap
absorption, shifting bug targets, color previews, feints, ramped deadlines and
exactly-once scoring. Mock rewards preserve the pet/save. Equipped gift pixels
are checked at 64x64; the sleep-overlay wake and battery policy are also tested.

```
OK uitest pages=1 keyboard=1 password=1 hud=1 backgrounds=1
```

This is not proof of physical typing comfort or aesthetic quality.

### `sd backup`, `sd restore`, `sd status`

`sd backup` uses the same copied-snapshot persistence job as the UI: mount,
commit the live snapshot to NVS, write `/sdcard/spidarium-save.bin`, flush/sync/
close, reopen and compare every byte. Reports `OK SD backup: Backup Saved and
Verified` only after completion, or `ERR` with logged failure details.
It writes a real backup; it is not a synthetic non-mutating diagnostic.

`sd restore` validates the complete card blob, commits it on the internal
persistence worker and adopts the restored state on LVGL. It replaces the
live pet. The SD menu's third row retains progress/completion/failure status.

`sd status` probes on the worker and reports card/backup presence. Use it after
reboot to check the backup persisted without restoring or replacing the pet.
Long filenames require `CONFIG_FATFS_LFN_HEAP=y` in both live/default configs;
the former short-name-only configuration mounted the card but rejected the
backup filename. Never format or erase the card as a workaround.

### `sound N`

Queue one synthesized effect, obeying mute. IDs 0..6 are gameplay effects
(pounce is now a soft noise whoosh, not a rising laser tone); 7 is eight seconds
of air, 8 a soft consonant bell/purr, 9 ten seconds of leaves, and 10 the
320ms rising two-note USB plug-in chime. ID11 is the matching falling unplug
chime. Existing IDs are unchanged.
Muted output explicitly says `OK sound muted`; unavailable/full queues return
`ERR`. Queue acceptance is not playback completion: the worker logs written
foreground sample counts and `ambience ... complete/interrupted/codec error`
with actual written/total samples.

The v1.1.18 automatic-noise policy is superseded by original slow ambient
music, default on and independently switchable in Sound. Master mute, zero
volume and radio/sleep drains silence it. Manual air/leaves remain diagnostics,
not the background score. The worker streams through its existing PSRAM buffer
and yields promptly to foreground effects. Manual textures use slow
attack/release envelopes, fading over 20 ms for foreground sound, mute or
radio work. Long ambience must not block the radio's three-second drain.

### `audiotest`

Non-playing deterministic PCM self-test on the console worker, with no shared
buffer or pet-state changes. Air: 128,000 samples, FNV-1a `02c0ba3f`, peak 1083;
leaves: 160,000 samples, `096a6969`, peak 1345. Both endpoints must be zero.
These little-endian 16-bit hashes are also asserted by the browser tests,
covering every sample rather than just duration. `OK audiotest` requires both
checks to pass; it does not prove speaker response or subjective sound quality.
Both USB chime checks validate all5,120 samples, silent endpoints,
nonzero/bounded peak and maximum adjacent-sample step. All ten complete music
loops must match pinned hashes and pass peak/RMS/step/seam and gain checks.
The console yields periodically; the regression harness allows60seconds for
the expanded ten-track test instead of the former three-track20seconds.

Run `python audio_check.py COM7` for actual 8/10-second codec writes plus
foreground/radio interruption checks (requires sound enabled). It opens/closes
the Sound menu to hold gameplay still, but does not change pet fields or audio
preferences. Run it alone on the port, not alongside another serial client.

### `battery`, `wake`

`battery` reads an atomic PMIC snapshot: percentage, battery voltage, USB/
charging state and screen-sleep flag. No battery and failed/unknown telemetry
are explicitly labelled rather than inventing a percentage. Example:

```
OK battery 100% / 4152 mV / USB Power policy=1 screen_sleep=0
```

While awake or screen-locked, two consecutive one-second PMIC USB samples
trigger one charge chime, including booting already plugged in and a full
battery on USB power. Two confirmed unplugged samples play the falling chime
and re-arm the rising one; read errors,
brief contact bounce and charging/full changes do not retrigger it. Master mute
and zero volume silence the chime; music's separate switch/volume do not.
The worker publishes an atomic event; LVGL queues sound without codec I/O.
USB insertion does not wake true deep sleep, which still uses RTC/right button.
`battery` also self-tests the edge policy without changing real PMIC state.

`wake` is marshalled to LVGL, hides the idle overlay and resets touch inactivity.
It does not feed, shoot, change needs or grant rewards. Use one open serial
connection for idle testing: opening a new connection can reboot the board
and restart the idle clock. Expect brightness 15 after 60 seconds and
brightness 0 / `screen_sleep=1` after 300 seconds, then `wake` restores the
selected brightness. See [battery policy](BATTERY_MANAGEMENT.md).

### `buttons`

Reports every physical button, then the lock state and volume:

```
BUTTON 0 gpio=0 level=1 active_high=0 pressed=0 stable=0
BUTTON 1 gpio=16 level=0 active_high=1 pressed=0 stable=0
BUTTON 2 gpio=18 level=1 active_high=0 pressed=0 stable=0
BUTTONS locked=0 volume=70
OK buttons
```

`level` is the raw GPIO read, `active_high` is the polarity the driver assumes
for that pin, and `pressed`/`stable` are the debounced results. GPIO16 is the
`SYS_OUT` net on this board, not a switch to GND: it idles LOW and reads HIGH
while the middle key is held, so it is the only `active_high=1` row. A pin that
could not be configured prints `unusable` instead.

`buttons lock`, `buttons unlock` and `buttons toggle` drive the same path the
middle key does, so the sleep overlay and the backlight can be exercised without
pressing anything. Locking logs `screen locked`, reports `screen_sleep=1` from
`battery`, and drives the backlight to 0; unlocking restores the selected
brightness. A touch on the overlay or `wake` also clears the lock.

### `hunttest`, `gaittest`, `preytest`

`hunttest` creates a temporary event-driven LVGL pointer, so actual hit testing
and press/move/release handlers run. It checks a far-away tap does not shoot,
Care opens and tank dismissal does not shoot, held drag freezes spider/prey,
release jumps toward the bug, catches it, and subpixel movement resumes without
snapback. The test restores pet fields, prey and existing poop; no synthetic
XP/save persists. It requires no active pounce and at least one free poop slot.
It leaves menus/Care closed, ready to hunt.

`gaittest` checks distance-phase equivalence at 20/30/60 Hz, 100 duplicate
renders and idle/pounce suppression. Eight golden neutral poses verify the
original angular anatomy; 10,240 animated samples plus resting emotes check
signed bend direction, original segment lengths and canvas reach. UI and hunting
tests assert the 44px page-ear hit target at (-1,-1), press inside the fold at
(29,29) in its inward-facing dark flap, and verify equal left/right stat-row
margins without overlapping the ear.
The X is hidden while the HUD is open and visible only for gift or decor
cancellation.
`preytest` verifies all eight level thresholds, random eligibility and rejected
locked/invalid explicit spawns, restoring the original level/prey count.

### `decortest`, `decor [place K X Y | move S X Y | block SLOT | item SLOT | clear]`

`decortest` places one of every decor kind through the real placement path,
refreshes the decor layer, and asserts that every placed item has a visible
image object at its recorded position sourced from the right asset. It then
restores the player's enclosure, Silk, level and owned mask, so it is safe to
run on a live pet.

`decor` with no argument lists all 68 kinds with cost, unlock level,
footprint radius, blocking flag and whether the kind is owned, then reports the
Silk balance and placed slots (placement can grow beyond twelve). `decor place KIND X Y`
grants the Silk and level needed for the kind (test-only), buys it if it is not
owned, and places it. `decor move SLOT X Y` repositions a placed item, keeping
its slot, kind and path. `decor block SLOT` cycles Walk Over (0), Solid (1),
Walk Behind (2), rather than toggling a boolean. Only Solid affects steering.
`decor item SLOT` opens the same inline drag/path/layer controls as a long press.
Done saves position/path/layer; Cancel discards edits. Back/Front arrows order
decor within Floor or Canopy (Walk Behind). `decor clear` empties the enclosure.
Mutating console commands persist and refresh the layer; opening controls does
not save. The UI offers Put Away while preserving ownership.

### `setupdone`

Finishes the naming screen without a tap, so the tank can be captured on a
freshly erased save. Test-only; it names the spider "Test Spider" if no name was
entered.

Use `python run_tests.py COM7 results.txt --gameplay` for regression coverage
without the default suite's care/save/spawn/state changes. The default full
suite deliberately modifies and saves the pet; do not use it casually on a
user's existing pet.

### `background [0-23]`

Without an argument, reports the active habitat. With an ID, uses the same
purchase/selection path as Menu > Backgrounds and waits off-LVGL for saving.
IDs: 0 cork, 1 moss, 2 leaf litter, 3 sand, 4 fern, 5 orchid, 6 woodland, 7 minimal.
8 mushroom, 9 gothic, 10 seashell, 11 succulent, 12 willow, 13 desert,
14 crystal, 15 cemetery, 16 house, 17 dark cave, 18 night, 19 autumn, 20 snow,
21 pond, 22 ruins, 23 marsh. Prices in ID order are
0, 12, 12, 16, 20, 24, 28, 16, 32, 40, 28, 24, 32, 36, 48, 48,
52, 60, 60, 54, 58, 64, 72, 68 Silk.
All are available from level 1; owned selections are free. Failed purchases
do not change the active theme or ownership. Save v10 includes ownership and
selection in pet backups. Older saves inherit their equipped old separate NVS
preference and cork free once, not the whole catalogue.

### `nap SECONDS`

Save and request ESP deep sleep for 5..86400 seconds, marshalled onto LVGL.
`OK nap queued=...` is acceptance, not proof of sleep. The internal save worker
must commit, drain workers, acknowledge display pause and log
`deep sleep committed` before entry. USB disconnects; wake is by ESP RTC timer
or the right/user GPIO18 button, not touch. `battery` reports `wake_cause=4`
for a real timer wake (3 for EXT1). `tools/test_deep_sleep.py COM7` verifies a
five-second timer wake plus saved habitat preservation. Do not reconnect with
a resetting serial client and call the resulting reset proof of RTC wake.
Battery profiles/timeouts/automatic delay are described in
[battery management](BATTERY_MANAGEMENT.md); auto deep sleep defaults off.

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
`play` opens a random Bug Hunt, Color Match or Quick Tap minigame. Complete
eight ramped-difficulty rounds to receive 8-32 base XP, 2-18 Silk and the existing care reward; Back cancels
without those rewards. Starting a game alone does not save or grant XP.

`clean` always works and pays XP in proportion to how much the tank actually
improved, so a spotless tank pays nothing and repeated taps cannot farm it.
The reward score is the dirt swept up plus the hygiene restored, out of
`CLEAN_SCORE_FULL` (24), rounded up and capped at `CLEAN_XP_MAX` (3). A spider
dropping is worth 12 dirt and a bug speckle 6, so one speckle pays 1 XP, two pay
2, three or more pay 3. The HUD never dims or disables Clean; only Feed is
disabled, when the tank already holds three live bugs. `stat` reports the
predicate as `CARE clean_needed=0|1`, the current mess as `dirt=`, and the XP a
clean would pay as `clean_xp=`.

```
>>> feed
OK feed
```

### `pet`

Pets the spider at its current position, exercising the same `needs_pet()` path
a tap on the body takes. Reports the resulting mood, bond, pet count, XP and the
day's petting budget.

```
>>> pet
OK pet mood=48 bond=1 pets=1 xp=167 pet_xp=2/10
```

A tap only counts as a pet when it lands within `PET_RADIUS_PX` (56 px) of the
spider, and a `PET_COOLDOWN_MS` (1.2 s) cooldown refuses a repeat. The cooldown
exists because a tap is a cheap gesture: without it a held finger could farm
mood, XP and the "Pet 4 Times" daily task. A pull that starts on the spider still
fires, because a click only arrives when the finger did not drag.

Petting pays XP only for the first `PET_XP_DAILY_MAX` (10) XP of each in-game
day, which is five pets at `PET_XP` (2). The cap is on the currency, not the
affection: mood, bond, the pet counter and the daily task all still advance on
every pet, and the spider still plays its bounce. The budget resets when the
in-game day changes, using the same RTC-first day the daily loop uses.

```
>>> pet
OK pet mood=34 bond=2 pets=2 xp=177 pet_xp=4/10
>>> pet
ERR pet refused (cooldown or out of range)
```

### `speckle [N]`

Drops `N` bug speckles (default 1, capped at `MAX_POOP` = 8) into the tank,
exercising the bug-poop path and the Clean reward scaling without waiting
`BUG_POOP_MS` for a live bug to produce one. Marshalled onto the LVGL task,
because it creates objects. Reports the resulting mess and the XP a clean would
pay.

```
>>> speckle 3
OK speckle count=3 dirt=18 clean_xp=3
```

A bug left in the tank leaves a pale speckle near itself every `BUG_POOP_MS`
(2.5 min), so parking a bug is a real consequence rather than a free meal.
Speckles share the dropping slot pool and the 10-minute `POOP_LIFETIME_MS`
decay.

### `daily [show|claim|task|reset|day N]`

Inspects and drives the daily loop. With no argument it prints the same `DAILY`
line `stat` does.

```
>>> daily
DAILY day=1 claimed=0 streak=0 task=1 task_done=0 progress=0/2 ready=0 silk=24 base_day=1 base=0 counters=0/0/0/0 pet_xp=0/10
OK daily
```

`claim` pays today's check-in and reports the streak-scaled reward. A second
claim on the same day is refused rather than silently paying again:

```
>>> daily claim
OK daily claim xp=6 streak=1 silk=28
>>> daily claim
ERR daily already claimed for day 1
```

`task` pays today's rotating objective, but only once it is complete:

```
>>> daily task
ERR daily task not ready (1/2)
```

`day N` forces the in-game day, which is how the streak and the task rotation
are exercised without waiting a real day. It snapshots the task baseline exactly
as a real rollover does, so the task starts at 0 rather than inheriting the
previous day's counters.

```
>>> daily day 3
OK daily day=3 task=3
```

`clock` drops the override so the real clock takes over again.

```
>>> daily clock
OK daily clock day=20768
```

The day number itself prefers the **RTC**: a real calendar day is what the player
expects, so the check-in resets at midnight rather than after 24 hours of play.
The RTC is optional (no backup battery, no sync without WiFi), so when it is not
valid the loop falls back to the in-game day counter, which advances with
`AGE_TICK_MS` and is compressed by `GAME_TIME_SCALE` for verification. The
console override wins over both, because the RTC refuses dates before 2000 and a
small day number therefore cannot be written to it.

`reset` clears the claim and the streak so the next `claim` pays again.

### `treat [0..11]`

Lists or buys a treat. With no argument it lists all twelve with their costs and
the current Silk balance:

```
>>> treat
TREAT 0 name="Juicy Fly" cost=6
TREAT 1 name="Silk Ribbon" cost=8
TREAT 2 name="Warm Pebble" cost=10
TREAT 3 name="Honey Moth" cost=14
TREAT 4 name="Dew Drop" cost=12
TREAT 5 name="Cozy Nest" cost=18
OK treat silk=64
```

Buying deducts Silk and applies the boost. A purchase the player cannot afford
is refused and logged, leaving the balance untouched:

Honey Moth adds 30 food/12 mood, Dew Drop 25 hygiene/12 energy, and Cozy Nest
28 energy/20 mood, each capped at 100.

```
>>> treat 0
OK treat 0 silk=58 hunger=45 mood=56 energy=80
>>> treat 2
ERR treat 2 refused: silk=0 cost=10
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
| `daily` | the daily check-in and task page |
| `shop` | the treats shop |
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

## Network provisioning and RTC commands

```powershell
python cmd.py COM7 "wifi saved"
python cmd.py COM7 "wifi add My Network|my-passphrase" 40
python cmd.py COM7 "wifi sync 0" 40
python cmd.py COM7 "wifi forget 0"
python cmd.py COM7 "wifi ota latest" 430
python cmd.py COM7 "wifi ota https://your-server/firmware.bin" 150
python cmd.py COM7 "otatest"
python cmd.py COM7 "rtc"
python cmd.py COM7 "rtcset 1794078000"
python cmd.py COM7 "gifttest"
```

`wifi add` uses `|` to separate SSID and password, permitting spaces in either.
Prefer the masked on-device editor: passwords passed to a shell may remain in
its history. No passwords are printed. Successful association saves credentials
and attempts RTC sync; failed association does not persist them.

`wifi saved` prints indices and SSIDs, never passwords. Sync/forget takes an
index 0-2. OTA requires HTTPS and a saved network; missing credentials and
non-HTTPS URLs return `ERR`, with no radio startup. Successful download is
verified before selecting the next boot slot; run `reboot` to apply it.
The firmware version must differ from the running version.
`wifi ota latest` resolves GitHub's latest stable release descriptor, rejects
older versions or incompatible installed artwork, verifies the exact application
size/SHA-256 and project/version, and only then selects the inactive slot.
Same-version checks report up to date without installing. `otatest` validates
the descriptor parser without radio/flash writes and reports the running
firmware version and partition, useful after the explicit reboot.
The advanced URL path does not provide the latest descriptor's art/hash checks.

`rtc` reports `RTC utc=<epoch> system=<epoch>` and fails when the oscillator-stop
flag/calendar is invalid. `rtcset` is manual RTC recovery/testing, UTC epoch
2000-2099 only. It does not pretend to be an NTP sync; reboot restores the
system clock from that RTC. Use an accurate current UTC epoch, not the example
timestamp. Backup-battery retention requires a physical power-off test.

`gifttest` runs on LVGL, prints all thirty-six effective bonuses, verifies removal
returns neutral values, and restores the original game state without saving.
It checks bonus computation, not every probabilistic catch or pixel appearance.

Additional menu names: `open saved`, `open password`, `open ota`, `open about`,
`open species`, `open close`. `open password` begins manual SSID/password entry;
`open close` dismisses modal menus and the gift picker.

Commands exceeding 383 bytes are rejected, never executed as truncated input.

## Architecture note: the radio and the display

The rule this device follows:

> **The radio is only on during a check, and the tank is not being drawn while
> it is.**

The radio and LCD SPI driver compete for internal DMA-capable memory. The
network worker temporarily owns that budget; a paused frame does not free or
defragment existing allocations.

So a scan:

1. Queues a job on the net task. The caller returns immediately.
2. The worker requests `esp_lv_adapter_pause(3000)` and waits for acknowledgement.
   LCD composition, touch, and game timers pause; the last frame remains visible.
3. The net task brings the radio up, scans, copies the results out.
4. **`esp_wifi_stop()` then `esp_wifi_deinit()`** — on every path, including
   failure. `stop()` alone leaves the driver's pools allocated, so the internal
   RAM stays gone for the rest of the session.
5. The adapter resumes, completion is published, and the UI refreshes results.
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

---

## Browser save tools (1.0.0)

The [website](https://jeromedontdev.github.io/Spidarium/) uses the same console,
not a network service. Only one host may own USB at a time; close monitors before
using Web Serial. Browser credentials go only to the selected USB device.

| Command | Framed response |
|---|---|
| `save schema` | `SAVE SCHEMA {JSON}` followed by `OK save schema` |
| `save export` | `SAVE HEX <hex>` followed by `OK save export` |
| `save stored` | Same framing, reads committed NVS without saving live state |
| `save restore <hex>` | `OK save restore; reboot required`, or `ERR ...` |

The schema is generated from compiled `offsetof(game_state_t, ...)` values,
not guessed browser offsets. It includes the header magic/version, 12-byte
header size, legacy 120/current 268-byte fixed payload sizes and 52 named fields with
little-endian types/offsets. The website bundles a captured schema so saved
files can also be inspected offline.

Export snapshots on LVGL, waits for the existing internal-stack persistence
worker on the console task, then reads the raw NVS blob. A new v14 backup is
280 bytes plus six bytes per extra decor slot; supported v2-v13 payloads remain
readable. `decorTail` identifies offset 268, stride 6 and the count field;
v10/v11 tails retain their historical offset 260 when read.
v13 reuses each six-byte record's blocking byte for path0/1/2; only path1
collides. Record order encodes layer order within Floor and Canopy, so reordering
may change slot indices. Path2 in older versions and path3 are rejected.
v14 additionally permits music tracks0..9 without moving offsets; v11-v13
permit only0..2. Old firmware cannot read v14 backups: export before updating.
`maxPayload` is the 20 KB NVS/protocol storage bound. It contains no
network credentials. Background ownership/selection and power preferences are
included, along with extended decor ownership, music and enclosure lighting;
saved WiFi credentials remain separate NVS keys.
Music volume and track occupy former padding at offsets 254/255. Older saves
default to 100 and track 0. `set music_volume 0..100` and `set music_track 0..9`
use copied LVGL requests; invalid values are rejected. Tracks are Silk Garden,
Moonlit Hollow, Rainy Canopy, Crystal Echoes, Mosslight Waltz, Ember Hearth,
Starlit Web, Bamboo Rain, Clockwork Fern and Dewdrop Lullaby. Master mute and
master volume still win.
v12 ownership uses byte245 for gift32..35 (low nibble) and decor64..67 (high),
u16 at246 for background32..47, and the full u32 at248 for decor32..63.
Border ownership is u32 at260/u16 at264, with selection at266 (255=None).
Earlier saves ignore those padding bytes and begin with no borders.
`border [0..47|none]` shows or buys/applies a frame using the real Silk economy.
`open borders` opens its picker; frame and floor are independent purchases.
Use `save stored` to compare committed bytes without normal live needs/mood/
clock advancement producing a new snapshot.

Restore rejects malformed hex, wrong magic/version/size, incomplete blobs and
invalid pet fields before replacing NVS. Valid data is copied into a worker
job. The tank freezes and later save requests are rejected until explicit
`reboot`; this prevents automatic saves from overwriting restored progress.
A failed restore clears the pending guard. Export while pending reads the
restored bytes directly instead of saving the old live state. Nothing resets
or erases WiFi. Back up first: a valid restore deliberately replaces the pet.

The single USB-JTAG driver's RX buffer is 1,024 bytes. The command line is now
PSRAM heap-backed and sized for the complete maximum raw restore. Long commands
must be paced in 128-byte chunks, 20 ms apart, with the newline sent last.
The browser and hardware contract script implement this pacing. Exports stream
bounded hex chunks; browser framing accepts the full variable-length save.
No second driver is installed.
Console stack is 8KB in
PSRAM; `mem` reports `STACK console_free=N` (minimum-ever free bytes). Real
valid/invalid restore tests check at least 2KB headroom and byte-exact
restoration, including after reboot:

```powershell
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\test_web_device.py COM7
```

This test restores the current snapshot, never substitutes a synthetic pet.
Invalid restore inputs must return ERR without rebooting or altering NVS.

## Screen capture

`screenshot` captures the active 480x480 LVGL screen; `screenshot spider`
captures the native 96x96 creature canvas with alpha, not the obsolete setup
sprite sheets. Snapshot creation/release happen only on LVGL; transfer happens
on the serial task.

`screenshot menu` is an explicit alias for the active screen, including whatever
menu is open. `screenshot playfield` captures the tank without any HUD, page ear,
bubble, menu or keyboard. Visibility is restored before the LVGL callback
returns, without advancing logic, closing the menu or changing pet state.
`capturetest` (with a menu open) compares menu pixels before/after clean capture
and asserts the playfield differs, is 480x480, and leaves pet state unchanged.

Settings > Save Screenshot and `screenshot sd` always select the clean playfield.
Files are numbered `/sdcard/spidarium-00001.bmp` through `99999`, never overwrite
existing files, and are RGB565 bitfield BMPs with top-down rows (460,866 bytes at
480x480). The existing persistence worker mounts on demand without formatting,
writes/flushes/fsyncs, then compares every header/pixel/padding byte on readback.
The UI polls Saving/filename/error status and never waits for card I/O.
One LVGL snapshot remains immutable while the worker writes/verifies its pixels.
After completion, LVGL destroys the snapshot; no duplicate full-screen copy is
allocated, and the worker never frees LVGL-owned pixels.
Capture allocation, queue, card, write and verification failures are explicit.
All capture targets reject while WiFi is busy: snapshots must not draw the tank
while the radio owns internal DMA RAM. Settings disables capture until it finishes.

The stream starts with `SCREEN BEGIN width height rgb565le` (or `bgra8888`),
then `SCREEN PART row byte_offset hex` chunks and `OK screenshot`. Chunks contain
at most 96 raw bytes and the serial task yields 10ms between them. This avoids
USB buffering loss and idle-task watchdog starvation. During transfer only
informational verbosity is reduced; WARN/ERROR remain enabled and the prior
log level is restored afterward. Corrupt/incomplete/interleaved data fails
explicitly rather than producing a plausible screenshot.

```powershell
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\capture_screen.py COM7 docs\screenshots\device-hud.png --menu chrome
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\capture_screen.py COM7 native-spider.png --menu chrome --spider
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\capture_screen.py COM7 build\diagnostic-menu.png --menu settings
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\capture_screen.py COM7 build\diagnostic-playfield.png --menu settings --playfield
python cmd.py COM7 "screenshot sd"
```

The host waits for startup to settle and allows 150 seconds for a full transfer.
PNG colors are decoded directly from captured pixels; native spider alpha is
preserved. The snapshot buffer is temporary PSRAM and released after transfer.
These are explicit diagnostic allocations, not per-frame renderer allocations.
Keep diagnostic captures out of source control unless deliberately documenting
a public screenshot. Opening the port may reset the device: use a persistent
serial connection for comparisons or multi-command diagnoses.

Feed now returns ERR when three live bugs are already present. The button is
gray/disabled, and both the callback and needs helper guard full-tank calls.
`preytest` checks repeated rejection without increasing taps, and re-enablement
when a bug is no longer alive. Existing prey and pet state are restored by tests.

## Automation checkboxes and save v3

Version 3 keeps the same 132-byte payload/144-byte backup. `auto_feed` and
`auto_hunt` are `u8` checkboxes at payload offsets 129/130, both `minVersion:3`.
Version 2 padding always migrates to OFF, even if its padding is nonzero.
Version 3 rejects checkbox bytes other than 0/1.

`set auto_feed 0|1` and `set auto_hunt 0|1` use the same LVGL-task Settings
callback as the checkboxes, including persistence. These commands can open
Settings; use `open close` to resume the tank. Invalid checkbox values reject.
