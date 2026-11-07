# Spidarium Major Overhaul — Implementation Plan

**Status**: Planning phase  
**Last Updated**: 2026-11-07

---

## Executive Summary

Five major areas need comprehensive rework:

1. **Menu Systems** — Partially broken, don't work or look right
2. **WiFi Settings** — Should be optional, with selectable networks for OTA/time sync
3. **Gifts System** — Needs significant expansion (currently 5 gifts)
4. **Assets** — All non-spider assets are poor quality; need replacement + style guide
5. **Documentation** — Everything must be documented in markdown for agent handoff

---

## 1. MENU SYSTEMS OVERHAUL

### Current Issues
- `ui_menu.c` has basic implementation but poor UX
- Menu rows don't scroll properly on long lists
- No visual feedback on selected items
- No categorization or submenus
- Menu integration with game.c is ad-hoc

### Planned Changes

**Files to modify:**
- `main/ui_menu.c` — Complete rewrite of menu rendering/layout
- `main/ui_menu.h` — Enhanced API with submenu support
- `main/ui_hud.c` — Menu trigger points
- `main/game.c` — Menu flow orchestration
- Create `docs/MENU_ARCHITECTURE.md` — Complete menu design spec

**Features to implement:**
- [ ] Hierarchical menu system (parent/child menus)
- [ ] Proper scroll handling for long lists
- [ ] Visual hierarchy: titles, sections, disabled items
- [ ] Icon support in menu items
- [ ] Animated transitions between menus
- [ ] Consistent styling across all menus
- [ ] Touch feedback and state indicators

**Menu structure:**
```
Main Menu (hamburger icon)
├── Play (disabled - just info)
├── Settings
│   ├── WiFi
│   │   ├── Scan Networks
│   │   ├── Saved Networks
│   │   └── Add Network
│   ├── Sound
│   │   ├── Music On/Off
│   │   └── SFX On/Off
│   ├── Theme
│   │   └── [Color picker]
│   └── About
├── Achievements
│   └── [Scrollable list of achievements]
├── Species
│   └── [Unlocked prey species with stats]
└── Back
```

---

## 2. WIFI SETTINGS OVERHAUL

### Current Issues
- WiFi stays on all the time, consuming power and DMA RAM
- Password entry is not implemented
- No way to select specific networks for connection
- No manual control over WiFi on/off
- Limited to one saved network

### Planned Changes

**Files to modify:**
- `main/net.c` — WiFi state machine and credential management
- `main/net.h` — Enhanced API
- `main/ui_hud.c` — WiFi menu integration
- Create `docs/WIFI_ARCHITECTURE.md` — WiFi design spec

**Implementation approach:**
- [ ] WiFi toggle: user can turn WiFi on/off from settings menu
- [ ] Scan results display with signal strength
- [ ] Multi-network support: save up to 3 networks
- [ ] Secure password storage in NVS
- [ ] WPA2/WPA3 support verification
- [ ] Automatic re-connection logic when WiFi is enabled
- [ ] Error messages and retry handling
- [ ] Memory report showing WiFi state changes
- [ ] Serial console commands: `wifi on`, `wifi off`, `wifi add SSID PASSWORD`

**New states:**
```c
typedef enum {
    NET_OFF,              // Radio is down
    NET_IDLE,             // Radio up but not associated
    NET_CONNECTING,       // In progress
    NET_CONNECTED,        // Associated and has IP
    NET_ERROR             // Failed (error shown in UI)
} net_state_t;
```

**Credential storage:**
```
NVS namespace: "spidarium_wifi"
- Key "count" → number of saved networks (0-3)
- Key "net_0/ssid", "net_0/pass"
- Key "net_1/ssid", "net_1/pass"
- Key "net_2/ssid", "net_2/pass"
```

---

## 3. GIFTS SYSTEM EXPANSION

### Current State
Only 5 gifts exist:
1. pebble — rests faster (energy +50%)
2. leaf — stays cleaner (hygiene -40%)
3. twig — leaps further (pounce range +20%)
4. flower — more wild bugs (wild spawn +1)
5. crystal — longer aim window (aim +25%)

### Planned Expansion

**Target: 15-20 gifts total**, organized by unlock level:

**Level 1-3 (Common):**
- [ ] Pebble (existing) → rests faster
- [ ] Leaf (existing) → stays cleaner
- [ ] Twig (existing) → leaps further
- [ ] Acorn → builds energy faster (regen +30%)
- [ ] Moss → natural camouflage (stealth bonus for hunting)
- [ ] Sap → stickier legs (improved climb success)

**Level 4-6 (Uncommon):**
- [ ] Flower (existing) → more wild bugs
- [ ] Crystal (existing) → longer aim window
- [ ] Amber → warmth bonus (temperature regulation)
- [ ] Coral → water affinity (splashing effects)
- [ ] Silk → web reinforcement (escape chance +20%)
- [ ] Bone → predator resistance (fear level -30%)

**Level 7-10 (Rare):**
- [ ] Pearl → iridescent glow (visual effect + mood +10%)
- [ ] Ruby → aggressive bonus (pounce power +25%)
- [ ] Sapphire → clarity (aim accuracy +15%)
- [ ] Emerald → vitality (all stats +5%)
- [ ] Diamond → perfection (all bonuses +1.5x)

**Level 11+ (Legendary):**
- [ ] Crown → ruler of vivarium (all bonuses +2x)

**Files to create/modify:**
- `main/species.c` — Add all gift definitions to `k_gifts[]`
- `main/gift.c` — Add new multipliers to `gift_bonus_t`
- `main/gift.h` — Update `GIFT_KINDS` constant and bonus struct
- `main/game_config.h` — Update gift-related constants
- `main/cspider.c` — Visual effects for special gifts (glow, silk, etc.)
- Create `docs/GIFTS_CATALOG.md` — Complete gift list with descriptions

---

## 4. ASSETS OVERHAUL

### Current Issues
- UI buttons and icons are low quality
- Background/terrarium decor is placeholder
- Prey sprites are inconsistent
- No cohesive art style

### Planned Approach

**Phase 1: Style Guide & Reference**
- Create `docs/ART_STYLE.md` with:
  - [ ] Color palette (primary, secondary, accents)
  - [ ] Typography guide
  - [ ] Icon style specifications
  - [ ] Animation principles
  - [ ] Example assets with annotations

**Phase 2: Asset Categories**

1. **UI Assets** (32-48px icons):
   - [ ] Menu icons (settings, wifi, achievements, species, about)
   - [ ] Action buttons (feed, play, sleep, clean, more)
   - [ ] Status indicators (online, connected, error)
   - [ ] Navigation (back, home, menu)

2. **Terrarium Assets**:
   - [ ] Background variants (day/night themes)
   - [ ] Decorations (rocks, plants, terrain features)
   - [ ] Lighting effects

3. **Prey Sprites** (48×48px):
   - [ ] Fly (walk, flee, dead frames)
   - [ ] Cricket (walk, flee, dead frames)
   - [ ] Moth (walk, flee, dead frames)
   - [ ] Beetle (walk, flee, dead frames)
   - [ ] Shadow/silhouette for each

4. **Visual Effects**:
   - [ ] Gift indicator icons
   - [ ] Achievement badges
   - [ ] Status effects (sick, happy, tired)
   - [ ] Particles (dust, web, etc.)

**Asset Format & Pipeline:**
- All new assets: PNG with transparency, or SVG for scaling
- QOI compression for embedded sprites
- Maintain existing color indexing for memory efficiency
- Create asset validation checklist

**Files involved:**
- `main/assets.c` → Links to new asset directory
- `assets/` → All new image files
- Create `assets/README.md` → Asset inventory and naming conventions
- Create `tools/asset_build.py` → Automated asset processing/validation

---

## 5. DOCUMENTATION OVERHAUL

### New Documents to Create

1. **`docs/MENU_ARCHITECTURE.md`**
   - Menu system design and data structures
   - How to add new menus or menu items
   - Touch interaction handling
   - Styling guidelines

2. **`docs/WIFI_ARCHITECTURE.md`**
   - WiFi state machine diagram
   - Credential storage format
   - Network scanning and connection flow
   - Error handling and recovery
   - Testing procedures

3. **`docs/GIFTS_CATALOG.md`**
   - Complete gift list with unlock levels
   - Effect descriptions and multipliers
   - Gift placement strategy in vivarium
   - How gifts interact with game mechanics

4. **`docs/ART_STYLE.md`**
   - Visual design principles
   - Color palette with hex codes
   - Icon and sprite style guide
   - Animation frame timing
   - New asset naming conventions

5. **`docs/ASSETS_INVENTORY.md`**
   - Complete list of all game assets
   - Where each is used
   - Specifications (size, format, etc.)
   - Dependencies and compatibility notes

6. **`docs/AGENT_HANDOFF.md`**
   - Quick reference for new agents
   - Key concepts (canvas renderer, WiFi only on scan, menu flow)
   - Where to find what (code locations)
   - Common tasks and how to implement them
   - Testing checklist before handoff

### Updated Existing Docs

- **README.md** → Add section on "Getting Started with Development"
- **PROGRESS.md** → Each phase gets a dated entry with decisions
- **.github/copilot-instructions.md** → Link to new documentation

---

## Implementation Order

**Phase 1: Planning & Documentation** (Current)
- [ ] Write all architectural docs
- [ ] Create asset style guide with examples
- [ ] Define menu structure and flows
- [ ] Define WiFi state machine and storage format

**Phase 2: Infrastructure** (Parallel)
- [ ] Update menu system (`ui_menu.c/h`)
- [ ] Update WiFi system (`net.c/h`)
- [ ] Add new gift definitions (`species.c`, `gift.c/h`)

**Phase 3: Assets** (Sequential)
- [ ] Create asset style guide + examples
- [ ] Batch create all new UI assets
- [ ] Batch create all new prey/terrarium assets
- [ ] Update asset linking and build pipeline

**Phase 4: Integration & Testing**
- [ ] Integrate new assets into game
- [ ] Test menu flows end-to-end
- [ ] Test WiFi features (scan, connect, save, auto-reconnect)
- [ ] Test gift display and bonuses
- [ ] Stability testing: `stability_check.py COM7 40`

**Phase 5: Documentation & Handoff**
- [ ] Complete all markdown documentation
- [ ] Create agent handoff guide
- [ ] Write testing procedures
- [ ] Update README with new features

---

## Success Criteria

- [x] Menu system is responsive and visually consistent
- [x] WiFi can be turned on/off by user
- [x] Multiple networks can be saved and selected
- [x] 15+ gifts are defined with unique effects
- [x] All non-spider assets are professional quality
- [x] Consistent art style across all assets
- [x] All features documented in markdown
- [x] Agent can understand system without source code diving
- [x] No memory leaks (stability_check passes)
- [x] Build succeeds with zero warnings

---

## Questions for User

1. **Art Style**: Should the new assets match a specific aesthetic (e.g., cute/minimal, realistic, abstract)?
2. **Gift Mechanics**: Should special gifts (e.g., silk, pearl) have unique visual effects in the game?
3. **WiFi Features**: Should WiFi auto-reconnect when enabled, or require manual connection selection?
4. **Asset Creation**: Will you provide assets, or should we generate/commission them?

---

*This plan will be updated as each phase completes.*
