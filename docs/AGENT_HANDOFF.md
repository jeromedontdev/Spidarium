# Agent Handoff Guide

**Author**: Copilot  
**Created**: 2026-11-07  
**Status**: Master Reference

---

## Quick Start for New Agents

You are taking over work on **Spidarium**, a virtual pet spider game on an ESP32-S3 with an AMOLED display.

### In 60 Seconds

- **Platform**: ESP32-S3-Touch-AMOLED-2.16 (480×480 AMOLED, 16MB flash, 8MB PSRAM)
- **Rendering**: Canvas-based (96×96 spider drawn from scratch each frame, not a widget tree)
- **WiFi**: Only active during scan/connect, radio must not run with display
- **Multitasking**: LVGL task (8 KB stack, display driver) + Game logic task (20 Hz) + Render task (30 Hz) + Net task (WiFi only)
- **Build**: `build.ps1`, `flash.ps1 COM7`, `stability_check.py COM7 40`
- **Test**: Serial console: `python cmd.py COM7 "help"`

**The three golden rules** (learned by breaking the device):
1. Never block the LVGL task (it drives the display; 8 KB stack)
2. LVGL is not thread-safe (call `lv_*` only from LVGL task)
3. Internal DMA RAM is scarce (512 KB; LCD needs contiguous block; radio uses a lot)

### Critical Files

| File | Purpose |
|------|---------|
| [.github/copilot-instructions.md](/.github/copilot-instructions.md) | Operating manual (read this first) |
| [README.md](/README.md) | Project overview |
| [PROGRESS.md](/PROGRESS.md) | Every development round with root causes |
| [docs/serial_console.md](/docs/serial_console.md) | Console command reference |
| [docs/memory_budget.md](/docs/memory_budget.md) | Where every KB of memory goes |

### New Docs (Added This Session)

| File | Purpose |
|------|---------|
| [IMPLEMENTATION_PLAN.md](/IMPLEMENTATION_PLAN.md) | Five-part overhaul plan (menu, WiFi, gifts, assets, docs) |
| [docs/MENU_ARCHITECTURE.md](/docs/MENU_ARCHITECTURE.md) | Menu system design: hierarchies, rendering, styling |
| [docs/WIFI_ARCHITECTURE.md](/docs/WIFI_ARCHITECTURE.md) | WiFi state machine, credential storage, user controls |
| [docs/GIFTS_CATALOG.md](/docs/GIFTS_CATALOG.md) | All 18 gift definitions, unlock levels, bonuses |
| [docs/ART_STYLE.md](/docs/ART_STYLE.md) | Color palette, icon/sprite specs, component styling |
| [docs/ASSETS_INVENTORY.md](/docs/ASSETS_INVENTORY.md) (this file) | Complete asset listing and dependencies |

---

## Code Layout

### Core Files

```
main/
  main.c              → Bring-up only (BSP)
  game.c              → Orchestrator: timers, game state, saves
  cspider.c           → Canvas spider renderer (THE CREATURE)
  spider.c            → Slingshot aim visuals + anatomy builder
  behaviour.c         → FSM: wander, stalk, aim, pounce, wall avoidance
  prey.c              → Bug spawning, fleeing, catch resolution
  needs.c             → Hunger/mood/energy/hygiene decay
  mind.c              → Intent layer (strategy)
  gift.c              → Gift bonuses (applies multipliers)
  species.c           → Bug and gift catalogue (DATA-DRIVEN)
  theme.c             → Color palettes
  terrarium.c         → Background, decor, environment
  ui_hud.c            → HUD: buttons, stat rings, emote
  ui_menu.c           → Generic menu widget (TO BE OVERHAULED)
  ui_setup.c          → First-run color picker
  net.c               → WiFi station (scan, connect, creds) (TO BE EXPANDED)
  save.c              → NVS persistence (game state)
  sd.c                → SD backup/restore
  audio.c             → I2S codec, sound effects
  assets.c            → QOI sprite linking
  test_console.c      → Serial command interpreter
  meminfo.c           → Memory reporting
```

### Key Abstractions

**`game_state_t g`** — The authoritative state
- Single global struct in `game.c`
- Persisted to NVS by `save.c`
- Modified only during logic tick (20 Hz)
- Read by render and UI tasks (no locks; 32-bit values are atomic)

**`game_ui_t ui`** — Shared UI constants
- Screen dimensions: `ui.W`, `ui.H` (480×480)
- Layout helpers: `UI_CORNER` (margin), `BTN_H` (button height)

**`playfield_t g_play`** — Current game area
- Prey positions, decorations, terrain
- Updated each logic tick

---

## Common Tasks

### Adding a New Feature

**Template**:
1. Define the data structure (in `game.h` or feature-specific header)
2. Add initialization in `state_defaults()` (main/game.c)
3. Add NVS persistence hooks in `save.c` if it should survive reboot
4. Add logic in `game_tick_logic()` callback
5. Add rendering in `game_tick_render()` callback or via UI widgets
6. Add serial console command in `test_console.c` for testing
7. Add menu item in `ui_hud.c` or `ui_menus_def.c` for user control
8. Document in `PROGRESS.md` with root cause and decisions

**Example**: Adding a new stat (e.g., "Stress")
```c
// game.h: Add to game_state_t
uint8_t stress;                  // 0-100

// game.c: Initialize
g.stress = 50;                   // Default

// save.c: Already covered by sizeof(g)

// needs.c: Decay logic
void needs_tick(void) {
    g.stress = needs_decay(g.stress, ...);
}

// ui_hud.c: Add ring
make_ring(..., COL_STRESS);

// species.c: If gifts affect it
gift_def_t.stress_mult = 0.8;

// test_console.c: Add command
"set stress 75" → g.stress = 75;
```

### Adding a New Gift

**Steps**:
1. Add row to `k_gifts[]` in `main/species.c`
2. Update `GIFT_KINDS` in `main/game_config.h`
3. Add multiplier fields to `gift_bonus_t` in `main/gift.h`
4. Extend `gift_compute()` in `main/gift.c` to apply multiplier
5. Add icon asset in `assets/gifts/` (32×32px PNG)
6. Update `docs/GIFTS_CATALOG.md` with description
7. Create icon sprite entry in `assets.c`
8. Test: Spawn gift via console `set gift_kind 7`, verify bonus applied

### Adding a Menu Item

**Steps**:
1. Define item in menu table (see `docs/MENU_ARCHITECTURE.md`)
2. Add callback function
3. Update menu definition (array or dynamic builder)
4. Test: Navigate menu via console or taps
5. Verify no memory leaks: `scanloop 5` shows stable largest free block

### Testing a Crash

**Procedure**:
1. Look at boot log: `boot.log`
2. Reproduce via serial console command if possible
3. Run under debugger (if GDB attached)
4. Check memory: `python cmd.py COM7 "mem"` (if device is responsive)
5. If hang/watchdog reboot:
   - May have blocked LVGL task → check for `vTaskDelay` on LVGL thread
   - May have exhausted DMA RAM → check `scanloop 5` for drift
   - May have stack overflow → check stack watermark in logs
6. Document in `PROGRESS.md`

---

## Memory Management

### The Scarce Resource: Internal DMA RAM (512 KB)

**Largest contiguous block matters more than total free.**

Current baseline at boot:
```
internal  free  57451  largest  47104
dma-cap   free  49663  largest  47104
psram     free 6758372  largest 6684672
```

**LCD draw buffers need one contiguous 51200-byte block from internal RAM.**

### What Lives Where

| Thing | Location | Size | Notes |
|-------|----------|------|-------|
| LCD draw buffers | PSRAM | 96 KB | `use_psram = true` |
| Code + rodata | PSRAM | ~600 KB | `SPIRAM_FETCH_INSTRUCTIONS`, `SPIRAM_RODATA` |
| Spider canvas | PSRAM | 27 KB | 96×96 RGB565A8, CPU-only |
| WiFi RX/TX pools | PSRAM* | Variable | `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` |
| Task stacks | Internal | Variable | Must be DMA-adjacent |
| Game state | PSRAM or NVS | ~1 KB | `game_state_t g` lives in RAM during game |
| NVS data | Flash | < 4 KB | Persisted player data |

*WiFi **must not run while rendering** because radio also needs internal DMA RAM.

### Memory Leak Detection

**Use these commands frequently**:
```powershell
python cmd.py COM7 "mem"                    # Current state
python cmd.py COM7 "render 500"             # 500 render frames
python cmd.py COM7 "scanloop 5"             # 5 wifi scans
```

Both `render` and `scanloop` report memory before and after. **If `largest` falls by more than 4 KB, you have a leak.**

---

## WiFi Constraints

**Current**: Scan only; limited to one saved network  
**After overhaul**: On/off toggle, multiple saved networks, manual selection

### Key Rules

1. **Radio only during operation**: When `net_radio_active()` is true, don't draw the tank
2. **Shutdown is mandatory**: `esp_wifi_stop()` then `esp_wifi_deinit()` on EVERY path, including error paths
3. **Non-idempotent calls**:
   - `esp_netif_create_default_wifi_sta()` — **ASSERTS** on duplicate (guard separately)
   - `esp_wifi_init()` — Returns `ESP_ERR_NO_MEM` if pools are gone
4. **Task separation**: WiFi work runs on its own task (already implemented), not LVGL task

### Testing WiFi

```powershell
python cmd.py COM7 "wifi status"           # Current state
python cmd.py COM7 "wifi scan"              # Initiate scan
python cmd.py COM7 "scanloop 5"             # 5 scans (DMA leak check)
# Verify 'largest' stays within 4 KB
```

---

## Asset Pipeline

### Current Assets

Located in `assets/`:
- Spider sprites (multi-frame per state): `spider_[gender]_[state]_[frame].png`
- Prey sprites (multi-frame per species): `prey_[species]_[state]_[frame].png`
- UI icons: `ui_[action].png` (feed, sleep, etc.)

**To be replaced**: Everything except spider

### Asset Linking

**File**: `main/assets.c`
```c
/* QOI images are packed into a flash partition and indexed here */
const lv_image_dsc_t *assets_image(int idx);
int assets_image_count(void);
```

**Sprite names** (for dynamic lookup):
```c
int sprite_icon_index(const char *icon_name);
/* "feed" → index 5, "play" → index 7, etc. */
```

### Adding a New Asset

1. Create PNG file (transparent background) in `assets/` folder
2. Add entry to sprite name table in `assets.c`
3. Rebuild: `build.ps1` (asset partition is generated)
4. Test: `python cmd.py COM7 "open main"` (should show new asset)
5. If asset is for a menu item, update menu definition

---

## Testing Checklist

Before marking a feature complete:

- [ ] Builds without warnings: `build.ps1` exits 0
- [ ] Flashes: `flash.ps1 COM7` exits 0
- [ ] Boots: Device starts, shows main screen
- [ ] Feature works: Test via serial console
- [ ] No leaks: `scanloop 5` shows stable `largest` block
- [ ] UI responsive: `render 500` completes without drift
- [ ] Saves persist: Reboot and verify state is restored
- [ ] Stability: `stability_check.py COM7 40` prints `VERDICT: PASS`
- [ ] Documented: Update PROGRESS.md with decisions and root causes

### Automated Test

```powershell
# Full validation suite
python run_tests.py COM7

# Extended soak test (20 minutes of continuous rendering + logic)
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' stability_check.py COM7 40
```

---

## Common Gotchas

### The Display Freezes When I Open a Menu

**Cause**: Blocking call on LVGL task (or `esp_wifi_*` call from wrong thread)

**Solution**:
- Check for `vTaskDelay()` on LVGL task
- WiFi work must be on its own task (use `queue_job()` in `net.c`)
- Menu callbacks must return immediately; no waits

### "Draw bitmap failed: ESP_ERR_NO_MEM"

**Cause**: Not enough contiguous internal DMA RAM (radio is running while rendering)

**Solution**:
- Ensure `net_radio_active()` is true while radio is up
- `game_tick_render()` returns early when `net_radio_active()`
- Check `esp_wifi_deinit()` is called on all paths (not just success path)

### Changes Build But Device Doesn't Boot

**Causes** (in order):
1. Stack overflow (task stack too small)
2. NVS partition corrupted (factory reset: erase flash)
3. Out of memory at startup
4. Assertion failure in boot code

**Debug**:
```powershell
python monitor.py COM7     # See boot log
# Look for "assertion failed" or "Stack overflow"
```

### Menu Item Doesn't Respond to Taps

**Common causes**:
- Callback is NULL
- Item is marked `enabled = false`
- Callback closed the menu but didn't save state
- Menu item `id` doesn't match what callback expects

**Debug**:
- Add `ESP_LOGI(TAG, "tap id %d", id)` in callback
- Check state after tap: `python cmd.py COM7 "state"`

---

## Documentation Standards

When you add a feature, update these files **in this order**:

1. **Feature docs** (e.g., `docs/WIFI_ARCHITECTURE.md`): Detailed design
2. **PROGRESS.md**: Dated entry with root cause + decisions
3. **README.md**: If user-facing feature, add to feature list
4. **Serial console docs**: If adds a command
5. **Comments in code**: Only where logic is non-obvious

---

## Getting Help

### Logs & Debugging

- **Boot log**: `boot.log` (power-on sequence)
- **Serial output**: Run `python monitor.py COM7` (real-time console)
- **Memory state**: `python cmd.py COM7 "mem"` (heap report)
- **Game state**: `python cmd.py COM7 "state"` (all game values)

### Reference

- [copilot-instructions.md](/.github/copilot-instructions.md) — Operating manual
- [serial_console.md](/docs/serial_console.md) — Every command
- [memory_budget.md](/docs/memory_budget.md) — Memory layout
- [Pocket Tank](https://github.com/mediacutlet/pocket-tank) — Reference architecture

---

## Handoff Criteria

You are ready to take over when you can:

- [ ] Build and flash without errors
- [ ] Boot the device and see the main game screen
- [ ] Run `python cmd.py COM7 "help"` and understand the command structure
- [ ] Run `stability_check.py COM7 40` and get a PASS
- [ ] Read and understand the three architectural docs (MENU, WIFI, GIFTS)
- [ ] Explain why the LVGL task cannot be blocked
- [ ] Explain why the radio must shut down completely after each scan
- [ ] Know where to find the game state (`game_state_t g` in game.c)
- [ ] Know how to add a serial console command

**Then you are ready to implement any of the planned features.**

---

## Next Steps

The overhaul plan has 5 phases:

1. ✅ **Planning & Documentation** (COMPLETE)
2. **Infrastructure** (Next): Menu system + WiFi state machine + gift definitions
3. **Assets**: Create new UI icons, prey sprites, terrarium elements
4. **Integration & Testing**: Hook everything together, validate, stress-test
5. **Final Documentation & Handoff**: Ensure next agent can take over

See [IMPLEMENTATION_PLAN.md](/IMPLEMENTATION_PLAN.md) for detailed timeline.
