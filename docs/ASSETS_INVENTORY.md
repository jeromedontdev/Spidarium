# Assets Inventory

**Author**: Copilot  
**Last Updated**: 2026-11-07  
**Status**: Living Document (Update as assets are added)

---

## Overview

Complete listing of all game assets, their specifications, current status, and where they're used.

**Status Legend**:
- ✅ Complete (implemented, tested)
- 🚧 In Progress
- ❌ Broken (known issues)
- 🔲 Not Started (planned)
- ⚠️ Placeholder (temporary, needs replacement)

---

## UI Icons (32-48px, PNG with transparency)

### Action Buttons (48×48px)

Located: `assets/ui_*.png`

| Name | ID | Current | Status | Usage | Notes |
|------|----|---------|----|-------|-------|
| feed | ui_feed.png | ✅ | ⚠️ | Main HUD, action bar | Needs refresh; low quality |
| play | ui_play.png | ✅ | ⚠️ | Main HUD, action bar | Needs refresh |
| sleep | ui_sleep.png | ✅ | ⚠️ | Main HUD, action bar | Needs refresh |
| clean | ui_clean.png | ✅ | ⚠️ | Main HUD, action bar | Needs refresh |
| menu/hamburger | (no separate file) | ❌ | 🚧 | Top-left corner | Currently uses Unicode character |

### Status Icons (32×32px)

| Name | Current | Status | Usage | Notes |
|------|---------|--------|-------|-------|
| online | N/A | 🔲 | WiFi menu | Not yet implemented |
| offline | N/A | 🔲 | WiFi menu | Not yet implemented |
| error | N/A | 🔲 | Error display | Not yet implemented |
| locked | ui_lock.png | ✅ | Achievement/species lists | Reuse existing |
| unlocked | (no icon) | 🔲 | Achievement/species lists | Need to create |

### Section Icons (32×32px)

| Name | Current | Status | Usage | Notes |
|------|---------|--------|-------|-------|
| settings | N/A | 🔲 | Menu | Part of menu redesign |
| wifi | N/A | 🔲 | WiFi settings | Part of WiFi overhaul |
| achievements | N/A | 🔲 | Main menu | Part of menu redesign |
| species | N/A | 🔲 | Main menu | Part of menu redesign |
| about | N/A | 🔲 | Main menu | Part of menu redesign |

### Stat Ring Icons (24×24px)

| Name | Current | Status | Color | Usage |
|------|---------|--------|-------|-------|
| hunger | ui_hunger.png | ✅ | #f5b342 | Stat ring label |
| mood | ui_mood.png | ✅ | #58d68d | Stat ring label |
| energy | ui_energy.png | ✅ | #7c9cff | Stat ring label |
| hygiene | ui_hygiene.png | ✅ | #4fd1c5 | Stat ring label |

---

## Prey Sprites (48×48px, multi-frame, PNG with transparency)

### Fly

Located: `assets/prey_fly_*.png`

| Frame | Current | Status | Notes |
|-------|---------|--------|-------|
| walk_0 | prey_fly_walk_0.png | ✅ | ⚠️ Low quality |
| walk_1 | prey_fly_walk_1.png | ✅ | ⚠️ |
| walk_2 | N/A | 🔲 | Could add more fluidity |
| walk_3 | N/A | 🔲 | " |
| flee_0 | prey_fly_flee_0.png | ✅ | ⚠️ |
| flee_1 | prey_fly_flee_1.png | ✅ | ⚠️ |
| dead | prey_fly_dead_0.png | ✅ | ⚠️ |

### Cricket

Located: `assets/prey_cricket_*.png`

| Frame | Current | Status | Notes |
|-------|---------|--------|-------|
| walk_0 | prey_cricket_walk_0.png | ✅ | ⚠️ |
| walk_1 | prey_cricket_walk_1.png | ✅ | ⚠️ |
| flee_0 | prey_cricket_flee_0.png | ✅ | ⚠️ |
| flee_1 | prey_cricket_flee_1.png | ✅ | ⚠️ |
| dead | prey_cricket_dead_0.png | ✅ | ⚠️ |

### Moth

Located: `assets/prey_moth_*.png`

| Frame | Current | Status | Notes |
|-------|---------|--------|-------|
| walk_0 | prey_moth_walk_0.png | ✅ | ⚠️ |
| walk_1 | prey_moth_walk_1.png | ✅ | ⚠️ |
| walk_2 | N/A | 🔲 | Could improve walk cycle |
| walk_3 | N/A | 🔲 | " |
| flee_0 | prey_moth_flee_0.png | ✅ | ⚠️ |
| flee_1 | prey_moth_flee_1.png | ✅ | ⚠️ |
| dead | prey_moth_dead_0.png | ✅ | ⚠️ |

### Beetle

Located: `assets/prey_beetle_*.png`

**Note**: Beetle sprites don't currently exist; using placeholder placeholder data

| Frame | Current | Status | Notes |
|-------|---------|--------|-------|
| walk_0 | N/A | ❌ | Missing entirely |
| walk_1 | N/A | ❌ | " |
| flee_0 | N/A | ❌ | " |
| flee_1 | N/A | ❌ | " |
| dead | N/A | ❌ | " |

---

## Spider Sprites (Multi-frame per state, PNG with transparency)

Located: `assets/spider_[gender]_[state]_[frame].png`

**Status**: ✅ Complete (DO NOT CHANGE per requirements)

### Sprite Organization

Sprites stored by gender × state × animation frame:
- **Gender**: e (empty/default), n (normal), s (small)
- **State**: idle, walk, eat, sleep, pounce, molt
- **Frame**: 0, 1, 2, 3 (animation frames)

Example: `spider_n_walk_2.png` = normal-gender spider, walking, frame 2

| State | Frames | Genders | Total Files |
|-------|--------|---------|-------------|
| idle | 2 | 3 | 6 |
| walk | 4 | 3 | 12 |
| eat | 2 | 3 | 6 |
| sleep | 1 | 3 | 3 |
| pounce | 2 | 3 | 6 |
| molt | 2 | 3 | 6 |

**Total**: ~40 spider sprite files (do not modify)

---

## Gift Icons (32×32px, PNG with transparency)

Located: `assets/gift_*.png` (to be created)

| Name | Color | Current | Status | Notes |
|------|-------|---------|--------|-------|
| pebble | #58d68d | N/A | 🔲 | Green stone |
| leaf | #7c9cff | N/A | 🔲 | Blue leaf |
| twig | #f5b342 | N/A | 🔲 | Orange branch |
| acorn | #c9a876 | N/A | 🔲 | Brown acorn (new) |
| moss | #6b8e23 | N/A | 🔲 | Olive moss (new) |
| sap | #d4a574 | N/A | 🔲 | Amber drip (new) |
| flower | #ff7eb6 | N/A | 🔲 | Pink flower |
| crystal | #4fd1c5 | N/A | 🔲 | Cyan crystal |
| amber | #ffb347 | N/A | 🔲 | Orange amber (new) |
| coral | #ff6b9d | N/A | 🔲 | Coral red (new) |
| silk | #e6e6fa | N/A | 🔲 | Lavender silk (new) |
| bone | #f5f5dc | N/A | 🔲 | Beige bone (new) |
| pearl | #fdeef4 | N/A | 🔲 | Light pink pearl (new) |
| ruby | #e0115f | N/A | 🔲 | Deep red ruby (new) |
| sapphire | #0f52ba | N/A | 🔲 | Deep blue sapphire (new) |
| emerald | #50c878 | N/A | 🔲 | Green emerald (new) |
| diamond | #b9f2ff | N/A | 🔲 | Light blue diamond (new) |
| crown | #ffd700 | N/A | 🔲 | Gold crown (new) |

**Total**: 18 gift icons (5 existing, 13 new)

**Batch creation**: All should be created together to ensure consistent art style

---

## Terrarium/Environment Assets (Variable size, PNG with transparency)

Located: `assets/terrarium_*.png` or `assets/bg_*.png`

| Name | Size | Current | Status | Notes |
|------|------|---------|--------|-------|
| background_day | 480×480 | N/A | ⚠️ | Exists but placeholder |
| background_night | 480×480 | N/A | 🔲 | Not yet implemented |
| decor_rock_0 | 48×48 | N/A | 🔲 | Environmental objects |
| decor_rock_1 | 48×48 | N/A | 🔲 | " |
| decor_plant_0 | 64×64 | N/A | 🔲 | " |
| decor_plant_1 | 64×64 | N/A | 🔲 | " |
| decor_wood_log | 96×32 | N/A | 🔲 | " |

**Note**: Terrarium rendering is procedural; new assets should enhance background

---

## Particle/Effect Assets (Variable, PNG with transparency)

Located: `assets/effect_*.png`

| Name | Size | Current | Status | Notes |
|------|------|---------|--------|-------|
| dust_mote | 8×8 | N/A | 🔲 | Dust particles (procedural ok) |
| sparkle | 16×16 | N/A | 🔲 | Gift glow, sparkles (procedural ok) |
| web_strand | 2×256 | N/A | 🔲 | Web rendering (procedural ok) |

**Note**: Most effects are procedural; pre-rendered assets only needed for quality

---

## Summary by Status

### ✅ Complete (No Action)
- Spider sprites (all 40 files)
- UI action buttons (feed, play, sleep, clean)
- Stat ring icons (hunger, mood, energy, hygiene)
- Prey sprites (fly, cricket, moth) — but quality is poor

### ⚠️ Placeholder (Need Replacement)
- All UI action buttons (4 files) — low quality, need refresh
- All prey sprites (fly, cricket, moth) — 14 files need artistic refresh
- `ui_lock.png` (reuse, but may need retouching)

### ❌ Missing (Need Creation)
- Beetle prey sprites (5 frames × 1 = 5 files)
- Menu section icons (settings, wifi, achievements, species, about)
- Status icons (online, offline, error, unlocked)
- All 18 gift icons
- Terrarium/environment assets (rocks, plants, wood, etc.)
- Particle/effect assets (dust, sparkle, web)

### 🔲 Not Started (Planned)
- Night-time background
- Additional prey animation frames (for fluid motion)
- Environmental decorations

---

## Asset Creation Priority

### Phase 1: Critical Path (Blocks other work)
**Timeline**: Week 1

1. **Menu section icons** (5 files)
   - settings, wifi, achievements, species, about
   - 32×32px PNG, outline style
   - Needed for menu overhaul

2. **Gift icons** (18 files)
   - All 18 gifts in catalog
   - 32×32px PNG, consistent style
   - Needed for gift system UI display

### Phase 2: High Quality Refresh (Improves UX)
**Timeline**: Week 2-3

3. **UI action button icons** (4 files)
   - feed, play, sleep, clean (existing but better)
   - 48×48px PNG, higher quality
   - Improves visual polish

4. **Status icons** (3 files)
   - online, offline, error
   - 32×32px PNG
   - Needed for WiFi menu feedback

5. **Prey sprite refresh** (14 files)
   - fly, cricket, moth walk/flee/dead
   - 48×48px PNG, improved quality
   - Low priority but high impact

### Phase 3: Polish & Polish (Nice to Have)
**Timeline**: Week 4+

6. **Terrarium decorations** (6-8 files)
   - rocks, plants, wood elements
   - Variable size, semi-transparent
   - Improves background immersion

7. **Beetle sprites** (5 files)
   - Complete missing creature
   - 48×48px PNG, match prey style
   - Game feature completion

8. **Particle effects** (3 files)
   - dust, sparkle, web (mostly procedural)
   - Small size, semi-transparent
   - Visual polish

---

## Asset Dependencies

### Compile Time
- `CMakeLists.txt` must list all PNG files in `assets/` directory
- `assets.c` sprite registry must index each asset
- Build system converts PNG → QOI (compressed flash format)

### Runtime
- `sprite_icon_index(name)` maps icon names to asset indices
- `assets_image(idx)` returns `lv_image_dsc_t*` for rendering
- If asset is missing, game displays **fallback text** (no crash)

### UI Referencing
- Menu items reference icons by name: `"feed"`, `"play"`, etc.
- Gift definitions reference color + icon name
- Status display uses icon indices

---

## Testing Assets

### Visual Inspection
- [ ] Each asset displays at target size (32×32, 48×48, etc.)
- [ ] Transparency works (no white background showing through)
- [ ] Colors match specified palette (or documented custom)
- [ ] No pixelation or anti-aliasing artifacts
- [ ] Works on both light and dark UI backgrounds

### Functional Testing
```powershell
# Load game and navigate menus
python cmd.py COM7 "open main"

# Verify icons appear (no missing texture artifacts)
python cmd.py COM7 "open settings"
python cmd.py COM7 "open wifi"

# Spawn gifts and verify icons
python cmd.py COM7 "set gift_kind 0"   # Should show pebble icon

# Check memory impact
python cmd.py COM7 "mem"               # Assets should fit in PSRAM
```

### Performance
- [ ] Menu open doesn't stall (< 16ms render time)
- [ ] Icon rendering is smooth (30 FPS maintained)
- [ ] No memory leaks: `scanloop 5` stable

---

## Asset Handoff Checklist

Before turning over new assets:

- [ ] All files named per convention: `category_name_variant.png`
- [ ] All files 32×32 or 48×48 (or specified size)
- [ ] All files PNG with transparency (no white background)
- [ ] Color palette verified against `docs/ART_STYLE.md`
- [ ] Icons tested at actual game size (no artifacts)
- [ ] High-res versions (2x) included if possible
- [ ] Sprite registry updated in `assets.c`
- [ ] CMakeLists.txt updated to include new files
- [ ] Build succeeds: `build.ps1` exits 0
- [ ] Game boots and renders correctly
- [ ] Assets appear in correct menus/screens
- [ ] Memory stable: `scanloop 5` shows no drift
- [ ] Documented in this inventory file

---

## Future Asset System Improvements

1. **Dynamic Sprite Loading**: Load sprites on demand instead of all at boot
2. **Asset Compression**: Further compress QOI or use JPEG for large backgrounds
3. **Vector Rendering**: Use SVG for scalable icons, rasterize at runtime
4. **Async Loading**: Load assets while game plays (background task)
5. **Asset Variants**: Light mode / dark mode asset sets
6. **Procedural Generation**: Generate some assets (backgrounds, particles) at runtime
