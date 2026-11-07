# Documentation Index & Quick Reference

**Created**: 2026-11-07  
**Status**: Complete — All major documentation created

---

## 📋 Master Document List

### Core Operating Manuals
- **[README.md](README.md)** — Project overview, build instructions, feature status
- **[.github/copilot-instructions.md](.github/copilot-instructions.md)** — Device operating rules (three golden rules + memory constraints)
- **[PROGRESS.md](PROGRESS.md)** — Historical development log (decisions, root causes, learning)

### Technical Reference
- **[docs/serial_console.md](docs/serial_console.md)** — Every console command with examples
- **[docs/memory_budget.md](docs/memory_budget.md)** — Where every kilobyte of RAM goes
- **[docs/MENU_ARCHITECTURE.md](docs/MENU_ARCHITECTURE.md)** — Menu design: hierarchy, rendering, API
- **[docs/WIFI_ARCHITECTURE.md](docs/WIFI_ARCHITECTURE.md)** — WiFi state machine, credentials, user flow
- **[docs/GIFTS_CATALOG.md](docs/GIFTS_CATALOG.md)** — All 18 gift definitions, effects, unlock levels
- **[docs/ART_STYLE.md](docs/ART_STYLE.md)** — Color palette, icon/sprite specs, component styling
- **[docs/ASSETS_INVENTORY.md](docs/ASSETS_INVENTORY.md)** — Asset catalog, status, creation priority

### Agent Resources
- **[docs/AGENT_HANDOFF.md](docs/AGENT_HANDOFF.md)** — Everything new agents need to know
- **[IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md)** — Five-part overhaul roadmap (menu, WiFi, gifts, assets, docs)

---

## 🎯 Quick Navigation by Task

### I Want to...

**Build & Test**
→ See [README.md](README.md) "Build" section + [docs/serial_console.md](docs/serial_console.md)

**Understand the Architecture**
→ Read [.github/copilot-instructions.md](.github/copilot-instructions.md) first (3 min)
→ Then [README.md](README.md) "Layout" section (5 min)
→ See codebase map in [docs/AGENT_HANDOFF.md](docs/AGENT_HANDOFF.md)

**Overhaul the Menu System**
→ [docs/MENU_ARCHITECTURE.md](docs/MENU_ARCHITECTURE.md) (design spec)
→ Current code: `main/ui_menu.c`, `main/ui_hud.c`
→ Plan in [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) Phase 2

**Fix WiFi Issues**
→ [docs/WIFI_ARCHITECTURE.md](docs/WIFI_ARCHITECTURE.md) (design spec)
→ Current code: `main/net.c`, `main/net.h`
→ Plan in [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) Phase 2

**Add a New Gift**
→ [docs/GIFTS_CATALOG.md](docs/GIFTS_CATALOG.md) (all 18 gifts defined)
→ Edit `main/species.c` (add row to `k_gifts[]`)
→ Edit `main/gift.h` and `main/gift.c` (add multipliers)

**Create Game Assets**
→ [docs/ART_STYLE.md](docs/ART_STYLE.md) (color palette, icon specs, design rules)
→ [docs/ASSETS_INVENTORY.md](docs/ASSETS_INVENTORY.md) (what to create, priority order)
→ See priority phases: UI icons → Gift icons → Prey sprites

**Debug a Crash**
→ [docs/AGENT_HANDOFF.md](docs/AGENT_HANDOFF.md) "Testing a Crash" section
→ Check [PROGRESS.md](PROGRESS.md) for similar issues

**Run Serial Console Commands**
→ [docs/serial_console.md](docs/serial_console.md) (full reference)
→ Examples: `python cmd.py COM7 "help"`, `"mem"`, `"scan"`, `"render 500"`

**Understand Memory Constraints**
→ [docs/memory_budget.md](docs/memory_budget.md) (detailed budget)
→ [.github/copilot-instructions.md](.github/copilot-instructions.md) Rule 3

**Hand Over to Another Agent**
→ Ensure all docs are current
→ Point to [docs/AGENT_HANDOFF.md](docs/AGENT_HANDOFF.md)
→ Have them run: `stability_check.py COM7 40` (validation)

---

## 📊 Overhaul Status

### Phase 1: Planning & Documentation ✅ COMPLETE

- [x] IMPLEMENTATION_PLAN.md — Five-part roadmap
- [x] MENU_ARCHITECTURE.md — Menu design spec
- [x] WIFI_ARCHITECTURE.md — WiFi state machine
- [x] GIFTS_CATALOG.md — 18 gift definitions
- [x] ART_STYLE.md — Visual design spec
- [x] ASSETS_INVENTORY.md — Asset catalog
- [x] AGENT_HANDOFF.md — Handoff guide

**All architectural documentation complete. Ready for Phase 2.**

### Phase 2: Infrastructure (Next)

**Menu System Overhaul**
- [ ] Refactor `ui_menu.c` with hierarchical support
- [ ] Update `ui_menu.h` API
- [ ] Create `ui_menus_def.c` with all menu definitions
- [ ] Add menu selection/scroll callbacks
- [ ] Test all menu flows
- [ ] Update serial console: `open [menu_name]`

**WiFi System Expansion**
- [ ] Add state machine to `net.c`
- [ ] Implement multi-network credentials storage
- [ ] Add WiFi toggle, scan, manual connect
- [ ] Update `net.h` public API
- [ ] Create WiFi menu in `ui_hud.c`
- [ ] Add console commands: `wifi on/off`, `wifi scan`, `wifi add`, `wifi list`

**Gift Definitions**
- [ ] Expand `k_gifts[]` in `species.c` (5→18 gifts)
- [ ] Extend `gift_bonus_t` struct in `gift.h`
- [ ] Update `gift_compute()` in `gift.c`
- [ ] Add visual effect support to `cspider.c`
- [ ] Test gift spawning and bonus application

### Phase 3: Assets (Parallel with Phase 2)

**Batch 1: Menu & Gift Icons (Critical Path)**
- [ ] Create 5 menu section icons (32×32px)
- [ ] Create 18 gift icons (32×32px)
- [ ] Add to `assets/` directory
- [ ] Update `assets.c` sprite registry
- [ ] Update CMakeLists.txt

**Batch 2: UI Polish (High Priority)**
- [ ] Refresh 4 action button icons (48×48px)
- [ ] Create 3 status icons (32×32px)
- [ ] Update asset registry
- [ ] Test in-game appearance

**Batch 3: Content (Nice to Have)**
- [ ] Refresh 14 prey sprites (48×48px)
- [ ] Create 5 beetle sprites
- [ ] Create 6-8 terrarium decorations

### Phase 4: Integration & Testing

- [ ] Menu system + assets working together
- [ ] WiFi menu + system working together
- [ ] Gift UI + gift system working together
- [ ] All features stability tested
- [ ] No memory leaks (DMA stable)
- [ ] Build passes with zero warnings

### Phase 5: Documentation & Handoff

- [ ] All docs updated with implementation details
- [ ] Serial console docs updated with new commands
- [ ] PROGRESS.md updated with all decisions
- [ ] README.md updated with new features
- [ ] Next agent can take over immediately

---

## 📖 How to Use This Documentation

### For Development

1. **Start with**: [.github/copilot-instructions.md](.github/copilot-instructions.md) — Operating rules (5 min read)
2. **Then**: Specific architecture doc for your task
3. **Reference**: [docs/serial_console.md](docs/serial_console.md) for testing
4. **Debug**: [docs/memory_budget.md](docs/memory_budget.md) + [PROGRESS.md](PROGRESS.md)

### For Design

1. **Start with**: [docs/ART_STYLE.md](docs/ART_STYLE.md) — Color palette + icon specs
2. **Reference**: [docs/ASSETS_INVENTORY.md](docs/ASSETS_INVENTORY.md) — What to create
3. **Export**: Follow naming conventions in [docs/ART_STYLE.md](docs/ART_STYLE.md)

### For New Agents

1. **Required reading**: [docs/AGENT_HANDOFF.md](docs/AGENT_HANDOFF.md) (30 min)
2. **Verify**: Run `stability_check.py COM7 40` (5 min)
3. **Choose task**: Pick from [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md)
4. **Deep dive**: Read architecture doc for your task

---

## 🔗 File Cross-References

### Menu System
- Design: [docs/MENU_ARCHITECTURE.md](docs/MENU_ARCHITECTURE.md)
- Plan: [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) Phase 2
- Code: `main/ui_menu.c`, `main/ui_hud.c`, `main/game.c`
- Assets: 5 menu icons (planned in Phase 3)

### WiFi System
- Design: [docs/WIFI_ARCHITECTURE.md](docs/WIFI_ARCHITECTURE.md)
- Plan: [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) Phase 2
- Code: `main/net.c`, `main/net.h`, `main/test_console.c`
- Assets: 3 status icons (planned in Phase 3 Batch 2)
- Commands: [docs/serial_console.md](docs/serial_console.md) (to be updated)

### Gifts System
- Catalog: [docs/GIFTS_CATALOG.md](docs/GIFTS_CATALOG.md)
- Plan: [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) Phase 2
- Code: `main/species.c`, `main/gift.c`, `main/gift.h`, `main/cspider.c`
- Assets: 18 gift icons (planned in Phase 3 Batch 1)
- UX: Menu system + HUD display

### Assets
- Specs: [docs/ART_STYLE.md](docs/ART_STYLE.md)
- Inventory: [docs/ASSETS_INVENTORY.md](docs/ASSETS_INVENTORY.md)
- Plan: [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) Phase 3
- Code: `main/assets.c`, `main/cspider.c` (for effects)
- Directory: `assets/` (all image files)

---

## ✅ Pre-Handoff Checklist

Before handing off to the next agent:

**Code Quality**
- [ ] `build.ps1` exits 0 with zero warnings
- [ ] `flash.ps1 COM7` exits 0
- [ ] `stability_check.py COM7 40` prints VERDICT: PASS
- [ ] No dead code or TODOs left behind

**Documentation**
- [ ] All architecture docs complete
- [ ] PROGRESS.md updated with latest decisions
- [ ] Serial console docs updated for new commands
- [ ] README.md reflects completed features
- [ ] AGENT_HANDOFF.md is current

**Testing**
- [ ] All new features tested via console commands
- [ ] All menus navigable and responsive
- [ ] WiFi on/off works, scan works, connect works
- [ ] Gifts spawn and apply bonuses correctly
- [ ] Assets display correctly at target sizes
- [ ] No memory leaks: `scanloop 5` stable

**Knowledge Transfer**
- [ ] New agent read AGENT_HANDOFF.md
- [ ] New agent ran: `build.ps1`, `flash.ps1 COM7`, `cmd.py help`
- [ ] New agent understands three golden rules
- [ ] New agent can explain why radio must shut down after each scan

---

## 📝 Document Maintenance

### When to Update

- After implementing each phase: Update corresponding architecture doc
- After major bug fix: Add entry to PROGRESS.md
- After asset creation: Update ASSETS_INVENTORY.md status
- Before agent handoff: Run through this entire checklist

### Version Control

All `.md` files are committed to git alongside code. Use:
```powershell
git add docs/*.md IMPLEMENTATION_PLAN.md PROGRESS.md README.md
git commit -m "Update documentation for [feature]"
```

---

## 🚀 Getting Started Right Now

### If you're a new agent:

1. Read this file (you're doing it! ✓)
2. Read [docs/AGENT_HANDOFF.md](docs/AGENT_HANDOFF.md)
3. Run: `build.ps1`
4. Run: `flash.ps1 COM7`
5. Run: `python cmd.py COM7 "help"`
6. Run: `stability_check.py COM7 40` (wait ~5 min)
7. **Then choose a task from [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md)**

### If you're continuing development:

1. Pick your task from [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md)
2. Read the corresponding architecture doc
3. Check current status in that doc
4. Implement
5. Test with serial console
6. Update PROGRESS.md
7. Update architecture doc if needed

---

## 📞 Support

**Can't find something?** Search this file for keywords.

**Stuck on a problem?** Check:
1. [PROGRESS.md](PROGRESS.md) for similar past issues
2. [docs/AGENT_HANDOFF.md](docs/AGENT_HANDOFF.md) "Common Gotchas"
3. [.github/copilot-instructions.md](.github/copilot-instructions.md) "When the build fails"

**Documentation out of date?** Update it immediately so the next agent has correct info.

---

**Last Updated**: 2026-11-07  
**Status**: All major documentation complete. Ready for Phase 2 implementation.  
**Next Step**: Menu system overhaul + WiFi expansion (see IMPLEMENTATION_PLAN.md Phase 2)
