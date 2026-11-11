# Agent playbook

Practical, task-shaped guidance for an agent picking up Spidarium. Read
[AGENT_HANDOFF](AGENT_HANDOFF.md) for current state and
[PSRAM_THREADING_ARCHITECTURE](PSRAM_THREADING_ARCHITECTURE.md) for the
allocation/threading policy first; this file is the "how do I actually do it"
companion.

## Ground rules that bite

1. **Never call `idf.py` directly.** Use `build.ps1` and `flash.ps1`. They set
   up the ESP-IDF environment and redirect output.
2. **Never block the LVGL task.** Game logic (20 Hz) and rendering (30 Hz) share
   one 8 KB adapter task. No WiFi, NVS, SD, blocking audio or worker waits in
   `lv_*` callbacks.
3. **Workers never call `lv_*`.** Queue a copied request and acknowledge.
4. **Anything parented to `lv_screen_active()` will not rotate.** Screen
   rotation is an LVGL transform on a wrapper object; use
   `power_rotation_layer()` for every new UI layer.
5. **`ui_menu_item_t.id` is `uint8_t`.** Slider values are smuggled through it
   by `ui_menu_set_item_slider()`; do not widen it without checking every menu.
6. **Save ABI is asserted at compile time.** `game.h` has `_Static_assert` on
   `sizeof(game_state_t)` and every appended field offset. Adding a field means
   updating those asserts, `SAVE_VERSION`, `migrate_automation()`,
   `save_raw_valid()`, the `test_console.c` schema table, `site/save-schema.json`
   and `site/lib/save.js`.
7. **The tail of `game_state_t` has padding.** `volume` at offset 131 did not
   grow the struct past 132 bytes. Check with a throwaway C probe before
   assuming a new byte changes the payload size.

## Common tasks

### Change a menu

Menus live in `menu_open()` in `main/game.c`. Each `case MENU_*` fills a
`static ui_menu_item_t items[20]` and calls `ui_menu_open(title, items, count,
callback)`. Row ids are positional and the callback switches on them, so
**renumbering rows means updating the callback and every
`GAME_UI_SET_*` action that routes through it.** After `ui_menu_open()` you may
call `ui_menu_set_item_checkbox(row)`, `ui_menu_set_destructive(row)`,
`ui_menu_set_item_slider(row, value, cb)` and `ui_menu_set_back(cb)`.

The browser equivalent is the `switch (name)` in `open()` in
`site/lib/device-ui.js`, with `row()` objects and `list()` rendering three per
page. Keep the two in the same order.

### Add a persisted setting

1. Append the field to `game_state_t` in `main/game.h`.
2. Update the `_Static_assert` block (size and offset).
3. Bump `SAVE_VERSION` in `main/game_config.h`.
4. Handle the new version in `migrate_automation()` and `save_raw_valid()` in
   `main/save.c`.
5. Add the field to the `fields[]` table in `cmd_save_schema()` in
   `main/test_console.c` and raise `SCHEMA_CAP` if needed.
6. Recapture the schema: `python cmd.py COM7 "save schema"` and write the JSON
   to `site/save-schema.json`.
7. Update `site/lib/save.js` (`validateSchema` version/payload limits,
   `decodeSave` defaults) and `site/lib/device-engine.js` (`freshState`,
   `validateProgress`, `restoreSim`).
8. Update the tests listed under "Tests that encode the ABI".

### Add a UI layer

Parent it to `power_rotation_layer(ui.screen)` — not `ui.screen` and not
`lv_screen_active()`. Then add it to `ui_hud_test()` if it is part of the HUD,
because that test asserts parentage and ordering.

### Tune gameplay constants

Numeric tuning lives in `main/game_config.h` and is mirrored in
`CONFIG` in `site/lib/device-engine.js`. A test
(`browser constants are the native defines`) parses the header and asserts every
matching `CONFIG` key, so **change both or the test fails**. Minigame timings are
the exception: they live in `play_round_start()` in `main/ui_menu.c` and
`startRound()` in `site/lib/minigames.js`, and are asserted by
`test_minigames()` and `browser-platform.test.mjs`.

### Capture a screenshot

```powershell
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\capture_screen.py COM7 docs\screenshots\device-hud.png
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\capture_screen.py COM7 docs\screenshots\device-settings.png --menu settings
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\capture_screen.py COM7 docs\screenshots\device-playfield.png --playfield
```

`--menu NAME` accepts the names `game_open_menu()` understands: `main`,
`settings`, `battery`, `display`, `sound`, `wifi`, `sd`, `about`, `species`,
`power`, `gift`, `chrome`, `nochrome`. There is no `achievements` name; use
`main` and page to it. Opening the port resets the board, so the first capture
after a flash needs the ~3 s settle the tool already waits for.

### Verify a change

```powershell
powershell -File build.ps1 *> build_out.txt 2>&1; "exit=$LASTEXITCODE"
Get-Content build_out.txt | Select-String -Pattern 'error:|warning:|exited with code'
powershell -File flash.ps1 COM7
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' run_tests.py COM7
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' stability_check.py COM7 40
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' cmd.py COM7 "scanloop 5"
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' cmd.py COM7 "render 500"
cd site; .\node.exe --test tests/*.test.mjs
```

Acceptance: build exit 0 with zero `main/` warnings, flash exit 0, device suite
all pass, soak `VERDICT: PASS` with zero faults, `largest >= 51200`, and
`largest_drift=0` on both allocation loops.

### Publish

```powershell
python tools/package_release.py --version X.Y.Z
git tag -a vX.Y.Z -m "..."
git push origin main; git push origin vX.Y.Z
gh release create vX.Y.Z release/spidarium-vX.Y.Z.zip release/spidarium-vX.Y.Z/spidarium.bin release/spidarium-vX.Y.Z/spidarium-ota.json --notes-file docs/RELEASE_vX.Y.Z.md
```

The `Publish website` workflow runs on `release: published`. **The
`github-pages` environment has a custom branch policy that lists each allowed
tag**, so a new tag fails with "Tag ... is not allowed to deploy" until you add
it:

```powershell
gh api -X POST repos/jeromedontdev/Spidarium/environments/github-pages/deployment-branch-policies -f name=vX.Y.Z -f type=tag
gh run rerun <run-id>
```

The site is served under `/site/`, not the repo root.

## Tests that encode the ABI

Changing the save layout or a shared constant breaks these on purpose:

| Test | Asserts |
|---|---|
| `site/tests/device-engine.test.mjs` | `sizeof(game_state_t)`, field offsets, `SAVE_VERSION`, `test_console.c` schema rows and `SCHEMA_CAP` |
| `site/tests/save.test.mjs` | bundled schema field count, payload sizes, version handling |
| `site/tests/sim.test.mjs` | `CONFIG` vs `game_config.h`, reward formulas, restore behaviour |
| `site/tests/browser-platform.test.mjs` | minigame durations, shuffle interval, reaction window |
| `main/ui_hud.c` `ui_hud_test()` | HUD parentage, ordering, battery position |
| `main/ui_menu.c` `ui_menu_test()` | paging, keyboard, slider rows |
| `main/ui_menu.c` `test_minigames()` | minigame durations and reward callback |
| `main/save.c` `save_format_test()` | version migration and field validation |

## Traps found the hard way

- **`lv_obj_update_layout()` must run before reading geometry.** `ui_hud_test`
  asserted button heights before the layout pass and every button reported
  `h=0`.
- **`save_raw_valid()` brightness check.** It once accepted only 25/50/75, which
  silently rejected every value from the new slider and made `hunttest` fail.
  Range checks must match the UI's range.
- **`bsp_display_rotation_set()` is a no-op in practice.** It writes MADCTL,
  which the adapter never re-reads. Do not reintroduce it.
- **The BSP keeps its touch handle `static`.** There is no getter, so touch
  flags cannot be updated after init. The panel is square, so this does not
  matter for rotation.
- **`Select-String -Context` on large generated files can return truncated
  snippets repeatedly.** Use `view` with `view_range` instead.
- **`site/dist/site` is locked while a preview server runs.** Stop the server
  before `node build.mjs` or it fails with `EBUSY`.

## Honest verification boundaries

Physical touchscreen appearance, authorized WiFi association/SNTP, a uniquely
versioned hosted HTTPS update, SD media, RTC backup-battery retention and
rotated-panel appearance need their own evidence. A reset or a successful scan
does not prove them. Do not log or commit credentials, claim encrypted NVS, or
claim signed firmware and functional rollback that have not been provisioned.
