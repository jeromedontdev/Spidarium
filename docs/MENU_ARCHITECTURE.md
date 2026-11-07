# Menu System Architecture

**Author**: Copilot  
**Last Updated**: 2026-11-07  
**Status**: Specification (Ready for Implementation)

---

## Overview

The Spidarium menu system uses a hierarchical, stack-based model where menus can have submenus. All menu operations are non-blocking and marshalled through the LVGL task.

### Design Principles

1. **Non-blocking**: Menu input never waits for network or I/O
2. **Single-responsibility**: Each menu component owns its data
3. **Reusable**: Generic menu widgets work for any content
4. **Responsive**: Touch feedback is immediate
5. **Memory-efficient**: Menus are created on demand, destroyed after use

---

## Data Structures

### Current (Legacy)

```c
typedef struct {
    uint32_t id;        // Unique identifier
    const char *text;   // Display text
    bool selected;      // Visual indicator
} ui_menu_item_t;
```

**Issues**: No icon support, no disabled state, no submenu pointer

### Proposed

```c
// Menu item type
typedef enum {
    MENU_ITEM_ACTION,       // Clickable; calls callback
    MENU_ITEM_TOGGLE,       // On/off switch
    MENU_ITEM_SLIDER,       // Value adjustment
    MENU_ITEM_SUBMENU,      // Opens another menu
    MENU_ITEM_LABEL,        // Read-only (for section headers)
    MENU_ITEM_SEPARATOR     // Visual divider
} menu_item_type_t;

// Menu item definition
typedef struct menu_item_t {
    menu_item_type_t type;
    uint32_t id;
    
    // Display
    const char *text;
    const char *icon;       // Icon name from sprite sheet
    const char *detail;     // Secondary text (e.g., "20°C" or "Off")
    
    // State
    bool enabled;
    bool selected;
    union {
        bool toggle_state;
        int slider_value;
    } state;
    
    // Behavior
    void (*on_select)(uint32_t id);
    
    // For submenus
    const struct menu_t *submenu;
} menu_item_t;

// Menu definition
typedef struct menu_t {
    const char *title;
    const char *back_icon;      // Back button icon name
    int item_count;
    const menu_item_t *items;
    void (*on_back)(void);      // Callback when back is pressed
} menu_t;

// Menu stack entry
typedef struct {
    const menu_t *menu;
    uint32_t generation;        // For detecting updates
} menu_stack_t;
```

---

## Menu Hierarchy

### Root Menu Structure

```
MAIN_MENU (hamburger icon tap)
│
├─ PLAY (info only)
│  └─ [Game info: time played, spiders caught, XP]
│
├─ SETTINGS
│  │
│  ├─ WIFI
│  │  ├─ WiFi Status (toggle on/off)
│  │  ├─ Scan Networks (action → network list)
│  │  ├─ Saved Networks (submenu → edit/delete/connect)
│  │  └─ Add Manual Network (keyboard input)
│  │
│  ├─ SOUND
│  │  ├─ Music (toggle)
│  │  └─ Sound Effects (toggle)
│  │
│  ├─ THEME
│  │  ├─ Pupil Color (submenu → color picker)
│  │  ├─ Fang Color (submenu → color picker)
│  │  └─ Leg Color (submenu → color picker)
│  │
│  └─ ABOUT
│     ├─ Version
│     ├─ Hardware Info
│     └─ Open Source Licenses
│
├─ ACHIEVEMENTS
│  └─ [Scrollable list of all achievements]
│
├─ SPECIES
│  └─ [Scrollable list of unlocked prey species]
│
└─ BACK (closes menu)
```

---

## Rendering

### Layout

All menus follow a consistent layout:

```
┌─────────────────────────────────────┐
│ Title                         [BACK] │  (30px, always visible)
├─────────────────────────────────────┤
│                                     │
│  • Menu Item 1          [icon]     │
│  • Menu Item 2          [icon]     │  (Scrollable container)
│  • Menu Item 3          [icon]     │
│  ✓ Toggle Item          [ON ]     │
│                                     │
│  Section Header                    │  (52px per row, 8px gap)
│  • Submenu Item   ▶ Detail         │
│                                     │
└─────────────────────────────────────┘
```

**Dimensions:**
- Menu width: `ui.W - 2*UI_CORNER` (420px on 480px screen)
- Menu max height: `ui.H - 2*UI_CORNER` (420px on 480px screen)
- Row height: 52px
- Row gap: 8px
- Title area: 30px
- Scrollable area: auto-sizing within max height

### Styling

**Colors** (theme-aware, currently hardcoded):
```c
#define COL_PANEL    0x15181f  // Background
#define COL_BORDER   0x2a2f3a  // Borders
#define COL_TEXT     0xdfe7ff  // Primary text
#define COL_DIM      0x8b93a7  // Secondary text
#define COL_ACCENT   0xf5b342  // Highlights
#define COL_SELECTED 0x2a5f5f  // Selection highlight
```

**Typography:**
- Title: Montserrat 14px, COL_TEXT
- Items: Montserrat 12px, COL_TEXT
- Detail text: Montserrat 10px, COL_DIM
- Disabled: COL_DIM with reduced opacity

**Spacing:**
- Padding: 12px on all sides
- Gap between rows: 8px
- Icon size: 24px (scaled from 48px source)

---

## Implementation Details

### File Structure

```
main/
  ui_menu.c        - Generic menu rendering engine
  ui_menu.h        - Public API + data structures
  ui_menus_def.c   - Menu definitions (all menus in one file)
  ui_menus_def.h   - Menu extern declarations
  ui_hud.c         - Hamburger button and menu triggers
```

### Key Functions

```c
// Open a menu (pushes onto stack)
void ui_menu_open(const menu_t *menu);

// Close current menu (pops stack)
void ui_menu_back(void);

// Close all menus (clear stack)
void ui_menu_close_all(void);

// Check if any menu is open
bool ui_menu_open_p(void);

// Get current menu depth
int ui_menu_depth(void);

// Refresh current menu (for dynamic content like scan results)
void ui_menu_refresh(void);

// Define a submenu callback that opens another menu
typedef const menu_t* (*menu_submenu_fn)(uint32_t item_id);
```

### Event Handling

**Touch Flow:**
1. User taps menu row
2. `on_row()` captures row index
3. Gets item definition
4. Handles based on type:
   - `MENU_ITEM_ACTION`: Call callback
   - `MENU_ITEM_SUBMENU`: Push submenu onto stack
   - `MENU_ITEM_TOGGLE`: Toggle state + callback
   - etc.

**Async Updates:**
- WiFi scan results: Use `ui_menu_refresh()` when `net_generation()` changes
- Achievement unlock: Rebuild menu if visible
- Level up: Update locked/unlocked items in species list

---

## Building Menus

### Example: Simple Menu

```c
// ui_menus_def.c

static void on_sound_music(uint32_t id) {
    g.muted ^= 1;  // Toggle
    game_save_now();
    ui_menu_refresh();  // Rebuild to show new state
}

static const menu_item_t k_sound_items[] = {
    {
        .type = MENU_ITEM_TOGGLE,
        .id = 1,
        .text = "Music",
        .icon = "speaker",
        .detail = g.muted ? "Off" : "On",
        .enabled = true,
        .selected = false,
        .state.toggle_state = !g.muted,
        .on_select = on_sound_music,
    },
    // More items...
};

const menu_t k_menu_sound = {
    .title = "Sound",
    .back_icon = "back",
    .item_count = COUNT_OF(k_sound_items),
    .items = k_sound_items,
    .on_back = NULL,
};
```

### Example: Dynamic Menu (WiFi Networks)

```c
// Scan results are fetched by net.c and stored in global state
// This menu is rebuilt whenever scan completes

static const menu_t *get_wifi_scan_menu(uint32_t parent_id) {
    // Allocate menu_item_t array dynamically
    // Populate from net_scan_results()
    // Return pointer to allocated menu
    // NOTE: Old menu must be freed when backing out
}

static const menu_item_t k_wifi_main_items[] = {
    {
        .type = MENU_ITEM_TOGGLE,
        .id = WIFI_TOGGLE,
        .text = "WiFi",
        .detail = net_enabled() ? "On" : "Off",
        .on_select = on_wifi_toggle,
    },
    {
        .type = MENU_ITEM_ACTION,
        .id = WIFI_SCAN,
        .text = "Scan Networks",
        .icon = "wifi",
        .on_select = on_wifi_scan,  // Starts scan
    },
    // More items...
};
```

---

## Testing Checklist

- [ ] Menu opens on hamburger tap
- [ ] All menu items are visible without scrolling (if < 7 rows)
- [ ] Long lists scroll smoothly
- [ ] Back button closes menu
- [ ] Clicking outside menu closes it
- [ ] Tapping an item triggers correct callback
- [ ] Selected items are highlighted
- [ ] Disabled items look disabled and don't respond
- [ ] Submenus open on selection
- [ ] Back in submenu returns to parent
- [ ] Dynamic menus (WiFi scan) update when refreshed
- [ ] Menu doesn't block game ticks
- [ ] No memory leaks after menu open/close cycles
- [ ] Icons display correctly
- [ ] Scrollbar appears for long lists
- [ ] Text wrapping works if text is long

---

## Migration Notes

**Current code** in `ui_menu.c`:
- Uses simple `ui_menu_item_t` struct with just `id` and `text`
- Single-level menu only
- No support for icons or disabled items

**New implementation must**:
- Drop-in replace `ui_menu_open()` signature
- Support backward compatibility with simple menus
- All existing menu callsites work without changes (except adding icons)

**Breaking changes** (unavoidable):
- Menu item struct expands significantly
- Menu definitions move to `ui_menus_def.c`
- Callback signature may change if we add more context

---

## Future Extensions

1. **Search/Filter**: For long lists (species, achievements)
2. **Keyboard Navigation**: For accessibility
3. **Drag-to-reorder**: For favorite networks or gifts
4. **Transitions**: Slide/fade animations between menus
5. **Multi-select**: For batch actions
6. **Confirmation Dialogs**: Delete network, factory reset, etc.
