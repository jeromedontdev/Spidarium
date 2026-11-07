# Quick Reference Card

**Print this or keep it open while developing.**

---

## The Three Golden Rules

1. **Never block the LVGL task** — 8 KB stack, drives display. Any `vTaskDelay`, semaphore wait, WiFi call, or NVS write → device reboots.
2. **LVGL is not thread-safe** — Only call `lv_*` from LVGL task. WiFi work on separate task.
3. **Internal DMA RAM is scarce** — 512 KB total, LCD needs contiguous block. Radio also needs it. Can't run together.

## Key Commands

```powershell
# Build & flash
powershell -File build.ps1           # Build (output to build_out.txt if hangs)
powershell -File flash.ps1 COM7      # Flash to COM7
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' stability_check.py COM7 40

# Console commands
python cmd.py COM7 "help"            # List all commands
python cmd.py COM7 "mem"             # Memory state
python cmd.py COM7 "state"           # Game state
python cmd.py COM7 "render 500"      # 500 render frames (DMA leak check)
python cmd.py COM7 "scanloop 5"      # 5 WiFi scans (DMA leak check)
python cmd.py COM7 "open settings"   # Open a menu
```

## Memory Debugging

Watch **`largest`** (largest contiguous free block), not `free`.

```
Baseline at boot:
  internal  free 60003  largest 51200  ← LCD buffers need 51200
  dma-cap   free 52739  largest 51200
  psram     free 6758372 largest 6684672
```

**If largest is falling**: You have a memory leak.
- `render 500` (render leak) or `scanloop 5` (WiFi leak)
- Report with `mem` before and after

## File Locations

| What | Where |
|------|-------|
| Game state | `game_state_t g` in `main/game.c` |
| Menu system | `main/ui_menu.c`, `main/ui_hud.c` |
| WiFi | `main/net.c`, `main/net.h` |
| Gifts | `main/species.c`, `main/gift.c` |
| Spider rendering | `main/cspider.c` (do not change) |
| Serial console | `main/test_console.c` |
| Persistence | `main/save.c` |
| Assets | `main/assets.c` + `assets/` directory |

## Adding a Feature

### Template
```c
// 1. game.h — Add to game_state_t
uint8_t my_feature;

// 2. game.c — Initialize
g.my_feature = 0;

// 3. save.c — Persist (automatic via sizeof g)

// 4. game.c — Logic tick
void game_tick_logic(...) {
    // my_feature logic here
}

// 5. game.c — Render tick
void game_tick_render(...) {
    // my_feature rendering here (MUST be quick)
}

// 6. test_console.c — Add command
{ "set my_feature", set_my_feature, ... }

// 7. ui_hud.c or ui_menus_def.c — Add UI
// (menu item, button, etc.)

// 8. PROGRESS.md — Document with decisions
```

### Checklist
- [ ] Compiles without warnings
- [ ] No new #includes (minimize dependencies)
- [ ] Saves to NVS if needed (game state auto-saves)
- [ ] Tested via console command
- [ ] Tested via UI tap
- [ ] No memory leaks: `scanloop 5` stable
- [ ] PROGRESS.md updated

## Documentation Reference

| Need | Read |
|------|------|
| Overview | [README.md](README.md) |
| Rules | [.github/copilot-instructions.md](.github/copilot-instructions.md) |
| Commands | [docs/serial_console.md](docs/serial_console.md) |
| Memory | [docs/memory_budget.md](docs/memory_budget.md) |
| Menu design | [docs/MENU_ARCHITECTURE.md](docs/MENU_ARCHITECTURE.md) |
| WiFi design | [docs/WIFI_ARCHITECTURE.md](docs/WIFI_ARCHITECTURE.md) |
| Gifts | [docs/GIFTS_CATALOG.md](docs/GIFTS_CATALOG.md) |
| Art/Assets | [docs/ART_STYLE.md](docs/ART_STYLE.md) |
| History | [PROGRESS.md](PROGRESS.md) |
| Getting started | [docs/AGENT_HANDOFF.md](docs/AGENT_HANDOFF.md) |

## Debugging Common Issues

### Display Freezes
**Cause**: Blocked LVGL task (8 KB stack)
**Fix**: Remove `vTaskDelay()`, `xQueueReceive()`, `esp_wifi_*()` from LVGL thread

### "Draw bitmap failed: ESP_ERR_NO_MEM"
**Cause**: Radio running while rendering (both need internal DMA RAM)
**Fix**: Ensure `net_radio_active()` returns true during WiFi. Render returns early then.

### Memory leak (largest drifts)
**Debug**:
```powershell
python cmd.py COM7 "mem"        # Record before
python cmd.py COM7 "render 500" # OR "scanloop 5"
python cmd.py COM7 "mem"        # Record after
# If largest fell >4KB, log it with memory report
```

### Device doesn't boot after flashing
**Sequence**:
1. Check boot.log for "assertion failed" or "Stack overflow"
2. If stack overflow, increase task stack size
3. If assertion, check for duplicate initialization
4. If nothing, erase flash: `idf.py erase-flash` then rebuild

### Menu item doesn't respond
**Check**:
- Is callback NULL?
- Is item marked `enabled = false`?
- Did callback close menu but not save state?
- Did you rebuild the menu? (State changes need `ui_menu_refresh()`)

## Before Committing

```powershell
# 1. Verify build
build.ps1    # Should exit 0, zero warnings in main/

# 2. Verify device
flash.ps1 COM7
python cmd.py COM7 "mem"      # Baseline memory
stability_check.py COM7 40    # Should print VERDICT: PASS

# 3. Test feature
python cmd.py COM7 "your_new_command"
python cmd.py COM7 "mem"      # Check it didn't leak

# 4. Update docs
# — Edit PROGRESS.md
# — Edit any architecture doc that changed
# — Update README.md feature list if user-facing

# 5. Commit
git add -A
git commit -m "Implement [feature]: [brief description]

Root cause: [if fixing a bug]
Changes: [what you modified]
Testing: [how you validated]

Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

## Performance Targets

| Metric | Target | How to Measure |
|--------|--------|----------------|
| Display refresh | 30 Hz | `render 500` completes in ~16 sec |
| Game logic | 20 Hz | No lag in game state updates |
| Menu responsiveness | <100ms | Tap feedback immediate |
| Memory leaks | 0 | `scanloop 5` largest stable ±4KB |
| DMA largest block | ≥47KB | `mem` shows largest at boot baseline |
| Build time | <2 min | `build.ps1` completes quickly |
| Flash time | <30 sec | `flash.ps1` completes quickly |

## Serial Console Syntax

```
# Commands are lowercase, space-separated
python cmd.py COM7 "command arg1 arg2"

# Common patterns:
python cmd.py COM7 "open [menu]"           # Open menu (main, settings, wifi, etc.)
python cmd.py COM7 "set [field] [value]"   # Set game state
python cmd.py COM7 "feed"                  # Trigger action
python cmd.py COM7 "[command]"             # Run command

# Dangerous commands (reset state):
python cmd.py COM7 "reset"                 # Full factory reset
python cmd.py COM7 "save"                  # Force NVS write
```

## Git Workflow

```powershell
# 1. Create branch for feature
git checkout -b feature/menu-overhaul

# 2. Implement + test
# (edit code, run tests, verify build)

# 3. Update docs
# (PROGRESS.md, architecture doc, README)

# 4. Commit
git add -A
git commit -m "..."

# 5. Merge to main when stable
git checkout main
git merge --no-ff feature/menu-overhaul
git push origin main
```

## Quick Wins (If Stuck)

1. **Build fails?** → Run `build.ps1 *> build_out.txt 2>&1` and check output
2. **Device hangs?** → Check boot.log for assertion; maybe erase flash
3. **Memory leak?** → Run `scanloop 5`; if largest falls, check allocations
4. **Menu doesn't work?** → Check callback is not NULL; rebuild with `ui_menu_refresh()`
5. **WiFi fails?** → Verify `esp_wifi_deinit()` called on all paths
6. **Not sure what to do?** → Read [docs/AGENT_HANDOFF.md](docs/AGENT_HANDOFF.md) "Common Gotchas"

## Contact Points in Code

| System | Main Files | Key Functions |
|--------|-----------|---|
| State | `game.c` | `game_init()`, `game_tick_logic()`, `game_tick_render()` |
| Menus | `ui_menu.c`, `ui_hud.c` | `ui_menu_open()`, `on_row()` |
| WiFi | `net.c` | `net_scan_start()`, `queue_job()`, `net_task()` |
| Gifts | `species.c`, `gift.c` | `k_gifts[]`, `gift_compute()` |
| Rendering | `cspider.c` | `cspider_render()` |
| Console | `test_console.c` | `test_console_run()`, `cmd_set()` |

---

**Keep this open while developing. Update if you find errors or add new patterns.**

**Last Updated**: 2026-11-07
