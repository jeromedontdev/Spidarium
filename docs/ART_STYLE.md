# Art Style Guide

**Author**: Copilot  
**Last Updated**: 2026-11-07  
**Status**: Specification (Ready for Implementation)

---

## Overview

This guide establishes a consistent visual language for all Spidarium assets. The game uses a **stylized, slightly whimsical** aesthetic with emphasis on clarity at small sizes (24-48px).

**Tone**: Cute but not childish. Detailed but readable. Natural but fantastical.

---

## Color Palette

### Primary Colors (Game Theme)

```
Color Name      Hex         RGB           Usage
──────────────────────────────────────────────────────
Deep Navy       #15181f     (21, 24, 31)  Background
Dark Gray       #2a2f3a     (42, 47, 58)  Borders, shadows
Light Gray      #8b93a7     (139, 147, 167)  Muted text, disabled
Off-White       #dfe7ff     (223, 231, 255)  Primary text
Accent Gold     #f5b342     (245, 179, 66)  Highlights, warnings
```

### Stat Colors (UI Rings)

```
Hunger          #f5b342     (245, 179, 66)  Orange/Gold
Mood            #58d68d     (88, 214, 141)  Green
Energy          #7c9cff     (124, 156, 255) Blue
Hygiene         #4fd1c5     (79, 209, 197)  Cyan
```

### Gift Palette (18 Gift Colors)

```
Pebble  #58d68d  (88, 214, 141)   Green
Leaf    #7c9cff  (124, 156, 255)  Blue
Twig    #f5b342  (245, 179, 66)   Orange
Acorn   #c9a876  (201, 168, 118)  Brown
Moss    #6b8e23  (107, 142, 35)   Olive
Sap     #d4a574  (212, 165, 116)  Amber
Flower  #ff7eb6  (255, 126, 182)  Pink
Crystal #4fd1c5  (79, 209, 197)   Cyan
Amber   #ffb347  (255, 179, 71)   Orange
Coral   #ff6b9d  (255, 107, 157)  Coral
Silk    #e6e6fa  (230, 230, 250)  Lavender
Bone    #f5f5dc  (245, 245, 220)  Beige
Pearl   #fdeef4  (253, 238, 244)  Light Pink
Ruby    #e0115f  (224, 17, 95)    Deep Pink/Red
Sapphire #0f52ba (15, 82, 186)    Deep Blue
Emerald #50c878  (80, 200, 120)   Green
Diamond #b9f2ff  (185, 242, 255)  Light Blue
Crown   #ffd700  (255, 215, 0)    Gold
```

### Neutral Palette

```
Shadows         #04050a  (4, 5, 10)      Nearly black
Highlights      #ffffff  (255, 255, 255) Pure white
Success         #2ecc71  (46, 204, 113)  Bright green
Warning         #e74c3c  (231, 76, 60)   Bright red
Info            #3498db  (52, 152, 219)  Bright blue
```

---

## Typography

### Font

**Primary**: Montserrat (sans-serif)
- **Fallback**: Arial, Helvetica
- **Weight**: Regular (400) for body, Bold (700) for headers
- **Reason**: Clean, modern, readable at small sizes

### Sizes & Usage

```
Size    Usage                   Example
─────────────────────────────────────────────────────
16px    Page titles             "Settings"
14px    Section headers         "WiFi Network"
12px    UI button labels        "Feed", "Play"
12px    Menu items              "Sound", "Theme"
10px    Detail text             Signal strength, timestamps
10px    Disabled text           Locked achievements
8px     Small info labels       "MB free"
```

### Line Height

- Headers (16px): 1.4x (22px)
- Body text (12px): 1.5x (18px)
- Labels (10px): 1.3x (13px)

---

## Icon Style

### Design Principles

1. **Simplicity**: Remove unnecessary details; clarity over realism
2. **Consistency**: Same line weight and proportions across all icons
3. **Negative Space**: Use whitespace to create the silhouette
4. **Grid**: Design on 48×48px grid, scale down to 24×24px for use
5. **Stroke**: 2-2.5px strokes for 48px icons

### Icon Categories

#### UI Control Icons (32-48px)

**Design approach**: Outline style with consistent stroke weight

- **Feed**: Bowl with food/legs
- **Play**: Toy/ball shape
- **Sleep**: Closed eyes or bed
- **Clean**: Water droplet or brush
- **Menu/Hamburger**: Three horizontal lines
- **Back**: Left arrow (< shape)
- **Settings**: Gear with 8-10 teeth
- **WiFi**: Signal strength arcs (3-4 arcs)
- **Audio**: Speaker with sound waves
- **Status**: Checkmark, X, or warning triangle

#### Status Icons (24-32px)

- **Online**: Check mark (✓)
- **Offline**: X mark (✗)
- **Loading**: Spinning circle or dots
- **Error**: Exclamation or warning triangle
- **Locked**: Padlock (closed)
- **Unlocked**: Padlock (open)

#### Gift Icons (32px)

Each gift gets a distinctive icon representing its essence:
- **Pebble**: Round, smooth stone
- **Leaf**: Single leaf with veins
- **Twig**: Small branch
- **Acorn**: Seed with cap
- **Moss**: Bumpy moss patch
- **Sap**: Dripping resin
- **Flower**: Stylized blossom (5 petals)
- **Crystal**: Geometric crystal shard
- **Amber**: Glowing chunk
- **Coral**: Branching coral
- **Silk**: Flowing strand
- **Bone**: Single bone
- **Pearl**: Perfect sphere with shine
- **Ruby**: Faceted gem
- **Sapphire**: Faceted gem (different cut)
- **Emerald**: Faceted gem (different cut)
- **Diamond**: Brilliant gem with sparkles
- **Crown**: Royal crown with multiple points

### Export

All icons exported as:
- **PNG**: 48×48px with transparency
- **SVG**: Vector (for scaling)
- **Naming**: `icon_[name].png`, e.g., `icon_feed.png`

---

## Sprite Style

### Prey Creatures (48×48px, animation frames)

**Design approach**: Top-down view, chunky but expressive

#### Fly
- 4 frames walking (2 wing states × 2 leg positions)
- 2 frames fleeing (panic)
- 1 frame dead
- Colors: Brown body, dark wings, transparent background

#### Cricket
- 4 frames walking (leg cycle)
- 2 frames fleeing (jumping)
- 1 frame dead
- Colors: Brown/tan body, darker markings, limbs clearly separated

#### Moth
- 4 frames walking (smooth walk)
- 2 frames fleeing (flying up/down)
- 1 frame dead
- Colors: Dusty brown or gray, wing patterns, antennae visible

#### Beetle
- 4 frames walking (heavy, deliberate)
- 2 frames fleeing (scurrying)
- 1 frame dead
- Colors: Dark iridescent shell, legs distinct, segmented

### Spider (Canvas, 96×96px)

**Already implemented; NOT changing.** Note: Future renditions should follow this style:
- Expressive eyes/pupils (user-customizable colors)
- Fangs (user-customizable)
- 8 legs with natural bend (walk cycle)
- Fuzzy/hairy texture (procedural or fine detail)
- Able to squash/stretch and show emotion
- Stage-based morphology (spiderling → adult → elder)

### Visual Effects

**Particles and effects should be subtle**:
- Dust motes: Small circles, semi-transparent
- Web: Fine lines, semi-transparent
- Glow: Soft halo, low opacity
- Shine: Bright spot with subtle rays

---

## Component Styling

### Buttons (UI)

```
Size: 48×32px (large action buttons) or 64×30px (menu buttons)

States:
  Default:
    - Background: #1e232b
    - Border: 1px #2a2f3a
    - Text: #dfe7ff (12px Montserrat)
    - Radius: 8px
    - Padding: 4px

  Pressed/Active:
    - Background: #2a2f3a (darkened)
    - Border: 2px #f5b342 (highlighted)
    - Text: #f5b342 (if highlighted)

  Disabled:
    - Background: #1e232b (unchanged)
    - Border: 1px #8b93a7 (dim)
    - Text: #8b93a7 (dim)
    - Opacity: 0.5
```

### Rings (Stat Display)

```
Arc width: 4px
Ring diameter: 48px (or scale proportionally)
Background arc: #2a2f3a (dim)
Foreground arc: Color per stat (hunger/mood/energy/hygiene)
Label: 10px Montserrat, centered below

Animation: Smooth transition when values change (0.3s ease-in-out)
```

### Panels (Menus, Popups)

```
Background: #15181f
Border: 1px #2a2f3a
Corner radius: 14px
Padding: 12px
Backdrop: Full screen, opaque #04050a (prevents UI crawl)
Shadow: None (opaque backdrop replaces this)
```

### Text Input

```
Background: #1e232b
Border: 1px #2a2f3a (unfocused) or 2px #f5b342 (focused)
Text: #dfe7ff (12px Montserrat)
Padding: 6px
Radius: 6px
Cursor: #f5b342
```

---

## Animation

### Principles

1. **Easing**: Use ease-in-out for motion
2. **Duration**: Keep animations under 0.5s (no slow, floaty feel)
3. **Predictability**: Animation should telegraph intent
4. **Performance**: No per-frame allocations during animation

### Animation Types

**Menu Open/Close** (300ms)
- Fade in/out + slight scale (0.95→1.0)
- Easing: ease-out-cubic

**Button Press** (100ms)
- Scale down (1.0→0.95) then back up
- Easing: ease-in-out-cubic

**Value Change** (300ms)
- Arc ring smoothly transitions fill amount
- Easing: ease-in-out-cubic

**Gift Pickup** (500ms)
- Gift icon scales in and moves to HUD position
- Easing: ease-out-cubic
- Then small pulse (1.0→1.1→1.0) for 300ms

**Emote** (1500ms)
- Bubble appears above spider
- Emoji fades in
- Bubble fades out

---

## Accessibility

### High Contrast

- All text has at least 4.5:1 contrast ratio against background
- Icon foreground vs background: at least 3:1
- Disabled text: clearly distinguishable (dimmed)

### Color Blindness

- Do NOT rely on color alone for status (use icons too)
- Red/green pairs avoided (stat colors use orange/blue/green/cyan)
- Colorblind palette check: Use tools like ColorBrewer

### Text Size

- Minimum 10px for any readable text
- Headers at 16px or larger
- Important info (errors, warnings) at 12px+

### Touch Targets

- Buttons: minimum 48×48px (or 32×32px with generous padding)
- Tap tolerance: 8px padding around interactive areas
- Spacing: 8px gap between buttons

---

## Asset File Organization

```
assets/
  ui/
    icon_feed.png
    icon_play.png
    icon_sleep.png
    icon_clean.png
    icon_settings.png
    icon_wifi.png
    icon_status_online.png
    icon_status_offline.png
    ... (48×48px PNG files)
  
  gifts/
    gift_pebble.png
    gift_leaf.png
    gift_twig.png
    ... (32×32px PNG files)
  
  prey/
    prey_fly_walk_0.png
    prey_fly_walk_1.png
    prey_fly_walk_2.png
    prey_fly_walk_3.png
    prey_fly_flee_0.png
    prey_fly_flee_1.png
    prey_fly_dead_0.png
    prey_cricket_*.png
    ... (48×48px PNG files, all animation frames)
  
  effects/
    effect_dust.png
    effect_sparkle.png
    effect_glow.png
    ... (visual effects, variable size)
  
  terrarium/
    bg_day.png
    bg_night.png
    decor_rock_0.png
    decor_plant_0.png
    ... (environment assets)

  README.md (inventory and usage notes)
```

---

## Design Checklist for New Assets

- [ ] Asset follows color palette (or custom + documented)
- [ ] Size matches specification (48px, 32px, etc.)
- [ ] Transparency: PNG with alpha channel
- [ ] Line weight: Consistent across components
- [ ] No anti-aliasing artifacts at target size
- [ ] Readable at both large and small sizes
- [ ] Works on both light and dark backgrounds
- [ ] Named following convention: `category_name_variant.png`
- [ ] Exported at 1x and 2x (for potential future scaling)
- [ ] Tested in-game at actual size and position
- [ ] No sub-pixel details (snap to pixel grid)

---

## Example: How to Create a Gift Icon

**Goal**: Design the Pebble gift icon (32×32px)

1. **Sketch**: Draw a simple rounded stone shape on paper
2. **Vector**: Create in SVG or Adobe Illustrator:
   - Draw a circle (24px diameter)
   - Add irregular bumps to make it stone-like
   - Give it a slight shadow on the bottom (subtle 1px line)
   - Fill with color `#58d68d` (pebble green)
   - Add a thin highlight on top-left (suggest shine)
3. **Padding**: 8px padding on all sides (final 32×32px canvas)
4. **Rasterize**: Export as 32×32px PNG (transparent background)
5. **Test**: View at actual game size; adjust if unreadable
6. **Variants**: Create 16×16px version for small UI
7. **Name**: Save as `gift_pebble.png`
8. **Document**: Add to asset inventory with usage notes

---

## Future Considerations

1. **Dark Mode**: Invert colors but maintain contrast ratios
2. **Animated Sprites**: GIF or APNG for fluid motion
3. **Variable Font**: Montserrat variable weight for responsive sizing
4. **HDR**: Support for future high-color-depth displays
5. **Scalable Vector**: Convert complex assets to SVG + rasterize on demand
