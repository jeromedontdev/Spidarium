# Gifts Catalog

**Author**: Copilot  
**Last Updated**: 2026-11-07  
**Status**: Specification (Ready for Implementation)

---

## Overview

Gifts are power-ups that the spider receives and carries, granting bonuses to specific game mechanics. Currently there are 5; this document proposes expanding to **18 gifts** organized by unlock level.

Each gift has:
- **Name**: Unique identifier
- **Effect**: Short description of what it does
- **Unlock Level**: Player level required to receive this gift
- **Colour**: UI indicator color (hex)
- **Multipliers**: Numeric bonuses applied to game stats

---

## Unlock Tiers

### Level 1-3: Common (The Basics)

These gifts teach the core mechanics and are available early in the game.

#### 1. Pebble ⭐ (EXISTING)
- **Effect**: Rests faster (+50% energy regeneration)
- **Unlock**: Level 1
- **Color**: `0x58d68d` (green)
- **Multipliers**:
  - `energy_regen_mult = 1.50`
- **Placement**: Often found in starting area
- **Strategy**: Early-game meta: encourages napping to recover from hunts

#### 2. Leaf ⭐ (EXISTING)
- **Effect**: Stays cleaner (-40% hygiene decay)
- **Unlock**: Level 2
- **Color**: `0x7c9cff` (blue)
- **Multipliers**:
  - `hygiene_decay_mult = 0.60`
- **Placement**: Near water or vegetation
- **Strategy**: Reduces care requirements; good for casual play

#### 3. Twig ⭐ (EXISTING)
- **Effect**: Leaps further (+20% pounce range)
- **Unlock**: Level 3
- **Color**: `0xf5b342` (orange)
- **Multipliers**:
  - `pounce_range_mult = 1.20`
- **Placement**: In open areas
- **Strategy**: Makes hunting easier; combines with Flower for aggressive build

#### 4. Acorn (NEW)
- **Effect**: Builds energy faster (+30% energy regen, different from pebble)
- **Unlock**: Level 2
- **Color**: `0xc9a876` (brown)
- **Multipliers**:
  - `energy_regen_mult = 1.30`
  - `energy_max_mult = 1.05` (slight max increase)
- **Placement**: Under trees, in woodsy areas
- **Strategy**: Mid-tier energy bonus; less powerful than Pebble but more natural-feeling

#### 5. Moss (NEW)
- **Effect**: Natural camouflage (+15% hunting success rate)
- **Unlock**: Level 1
- **Color**: `0x6b8e23` (olive)
- **Multipliers**:
  - `pounce_success_mult = 1.15`
  - `prey_flee_penalty = 0.85` (prey flee time -15%)
- **Placement**: Mossy rocks, damp corners
- **Strategy**: Stealthy hunting alternative; pairs with behavior mods

#### 6. Sap (NEW)
- **Effect**: Stickier legs (+20% climbing/wall grip success)
- **Unlock**: Level 3
- **Color**: `0xd4a574` (amber)
- **Multipliers**:
  - `movement_efficiency_mult = 1.10` (less slipping on walls)
  - `wall_climb_success_mult = 1.20`
- **Placement**: On tree bark, sticky surfaces
- **Strategy**: Traversal bonus; makes difficult terrain easier

---

### Level 4-6: Uncommon (Specialization)

Mid-game gifts that let players specialize their spider's playstyle.

#### 7. Flower ⭐ (EXISTING)
- **Effect**: More wild bugs (+1 wild spawn bonus)
- **Unlock**: Level 5
- **Color**: `0xff7eb6` (pink)
- **Multipliers**:
  - `wild_spawn_bonus = +1`
- **Placement**: Visible, attractive; planted areas
- **Strategy**: Increases hunt frequency; aggressive spider build

#### 8. Crystal ⭐ (EXISTING)
- **Effect**: Longer aim window (+25% aim accuracy time)
- **Unlock**: Level 7
- **Color**: `0x4fd1c5` (cyan)
- **Multipliers**:
  - `aim_window_mult = 1.25`
- **Placement**: Sparkly/special locations
- **Strategy**: Makes aiming easier for less skilled players

#### 9. Amber (NEW)
- **Effect**: Warmth bonus (+10% all stats, slight mood boost)
- **Unlock**: Level 4
- **Color**: `0xffb347` (amber/orange)
- **Multipliers**:
  - `all_stats_mult = 1.05` (hunger, mood, energy, hygiene all +5%)
  - `mood_bonus = +10` (direct bonus)
  - `temp_regulation = 0.5` (half temperature stress)
- **Placement**: Sunny spots, warm locations
- **Strategy**: Jack-of-all-trades; slight buff to everything

#### 10. Coral (NEW)
- **Effect**: Water affinity (+15% hunting near water, visual effects)
- **Unlock**: Level 5
- **Color**: `0xff6b9d` (coral/red)
- **Multipliers**:
  - `water_zone_bonus = 1.15` (bonus when in water areas)
  - `swim_efficiency = 1.20`
  - `prey_spawn_near_water = +1`
- **Placement**: Near water features, ponds
- **Visual Effect**: Iridescent shimmer when active
- **Strategy**: Rewards exploring water zones; environmental gameplay

#### 11. Silk (NEW)
- **Effect**: Web reinforcement (+20% escape chance, web durability)
- **Unlock**: Level 6
- **Color**: `0xe6e6fa` (lavender/white)
- **Multipliers**:
  - `web_escape_chance = 1.20`
  - `predator_avoidance_mult = 1.15`
  - `trap_effectiveness = 1.10`
- **Placement**: In corners, web-rich areas
- **Strategy**: Defensive playstyle; helps when cornered

#### 12. Bone (NEW)
- **Effect**: Predator resistance (-30% fear from threats)
- **Unlock**: Level 4
- **Color**: `0xf5f5dc` (beige/bone)
- **Multipliers**:
  - `fear_mult = 0.70` (less fear from predators)
  - `boldness_bonus = +20` (directly affects behavior)
  - `aggression_mult = 1.10`
- **Placement**: Scattered, somewhat hidden
- **Strategy**: Makes spider braver; less fleeing behavior

---

### Level 7-10: Rare (Mastery)

Late-game gifts for experienced players with unique effects.

#### 13. Pearl (NEW)
- **Effect**: Iridescent glow (+10% mood, visual aura, light buff)
- **Unlock**: Level 7
- **Color**: `0xfdeef4` (pearl/light pink)
- **Multipliers**:
  - `mood_mult = 1.10`
  - `mood_base = +15`
  - `visual_appeal = 1.25` (cosmetic only)
- **Visual Effect**: Soft glowing aura around spider
- **Placement**: Rare, special locations
- **Strategy**: Mood-focused build; emotional/psychological gameplay

#### 14. Ruby (NEW)
- **Effect**: Aggressive bonus (+25% pounce power, damage)
- **Unlock**: Level 8
- **Color**: `0xe0115f` (ruby red)
- **Multipliers**:
  - `pounce_range_mult = 1.25`
  - `pounce_force_mult = 1.25` (faster pounce)
  - `catch_success_mult = 1.15`
  - `wildness_mult = 1.20` (spider acts more aggressively)
- **Placement**: Dangerous/exciting areas
- **Strategy**: Aggressive hunt build; pairs with Flower for apex predator

#### 15. Sapphire (NEW)
- **Effect**: Clarity (+15% aim accuracy, perception)
- **Unlock**: Level 9
- **Color**: `0x0f52ba` (sapphire blue)
- **Multipliers**:
  - `aim_window_mult = 1.15`
  - `prey_detection_range = 1.20` (sees prey from farther away)
  - `focus_bonus = +20`
  - `reaction_time_mult = 0.85` (faster to react)
- **Placement**: High vantage points
- **Strategy**: Perception/awareness build; helps with hunting

#### 16. Emerald (NEW)
- **Effect**: Vitality (+5% all four stats uniformly)
- **Unlock**: Level 10
- **Color**: `0x50c878` (emerald green)
- **Multipliers**:
  - `hunger_decay_mult = 0.95` (all stats decay slower)
  - `mood_decay_mult = 0.95`
  - `energy_decay_mult = 0.95`
  - `hygiene_decay_mult = 0.95`
  - `energy_regen_mult = 1.05` (slight boost to regen)
  - `all_needs_recovery = 1.05` (benefits from care)
- **Placement**: Balanced, everywhere equally
- **Strategy**: Universal buff; good for any playstyle

---

### Level 11+: Legendary (Endgame)

Super-rare gifts for completionists and longtime players.

#### 17. Diamond (NEW)
- **Effect**: Perfection (+50% to all bonuses, multiplicative!)
- **Unlock**: Level 12
- **Color**: `0xb9f2ff` (diamond/clear)
- **Multipliers**:
  - `ALL multipliers += 50%` (if another gift has 1.20, becomes 1.80)
  - `bonus_synergy = 2.0` (gifts interact better)
- **Visual Effect**: Bright sparkle and shine
- **Placement**: Hidden, ultra-rare
- **Strategy**: Endgame power fantasy; makes all other gifts much better
- **Note**: Should NOT give base bonuses alone, only multiplier bonuses

#### 18. Crown (NEW)
- **Effect**: Ruler of vivarium (+100% to all bonuses!)
- **Unlock**: Level 15
- **Color**: `0xffd700` (gold)
- **Multipliers**:
  - `ALL bonuses doubled`
  - `master_level = 1`
  - `special_aura = true` (golden glow effect)
- **Visual Effect**: Golden crown-like icon, special animations
- **Placement**: Never spawned; earned via achievement only
- **Strategy**: Ultimate achievement; caps character growth
- **Rarity**: 1 per playthrough (if earned at all)

---

## Data Structure

```c
typedef struct {
    uint8_t id;
    const char *name;           // "pebble", "leaf", etc.
    const char *effect;         // "rests faster"
    uint32_t unlock_level;      // Level required to receive
    uint32_t color;             // UI color (hex)
    
    // Bonuses (multiplicative unless noted)
    float energy_regen_mult;
    float hygiene_decay_mult;
    float pounce_range_mult;
    float aim_window_mult;
    float pounce_success_mult;
    float mood_mult;
    float fear_mult;
    float aggression_mult;
    
    // Special flags
    bool has_visual_effect;
    const char *visual_effect_name;  // "glow", "shimmer", "sparkle"
    
    // Synergy (if this gift multiplies other gifts' bonuses)
    float bonus_synergy_mult;  // 1.0 = no synergy, 1.5 = 50% bonus to other gifts
} gift_def_t;
```

---

## Placement Strategy

Gifts should **not spawn randomly**. Instead, they should be placed strategically:

1. **Pebble**: Common, always available (green area)
2. **Leaf**: Common, near water or clean spots
3. **Twig**: Common, open spaces (hunting area)
4. **Acorn**: Forests, tree areas
5. **Moss**: Damp corners, under rocks
6. **Sap**: On bark, sticky places
7. **Flower**: Garden areas, bright spots
8. **Crystal**: Shiny/special locations
9. **Amber**: Warm areas (if day/night implemented)
10. **Coral**: Water features
11. **Silk**: Web-rich corners
12. **Bone**: Hidden, somewhat rare
13. **Pearl**: Rare, special locations
14. **Ruby**: Dangerous areas
15. **Sapphire**: High vantage points
16. **Emerald**: Balanced everywhere
17. **Diamond**: Ultra-rare, hidden
18. **Crown**: Achievement unlock only

---

## Implementation Details

### File Changes

**`main/species.c`**:
- Expand `k_gifts[]` array from 5 to 18 entries
- Add new multiplier fields to each entry
- Add visual effect names

**`main/gift.h`**:
- Update `GIFT_KINDS` constant to 18
- Expand `gift_bonus_t` struct with new fields
- Add visual effect enum

**`main/gift.c`**:
- Expand `gift_compute()` to handle all new multipliers
- Add synergy calculation for Diamond/Crown gifts
- Apply visual effect flags

**`main/cspider.c`**:
- Add visual effects: `render_gift_glow()`, `render_gift_shimmer()`, etc.
- Call from main render loop if active gift has effect

**`main/game_config.h`**:
- Update constants: gift counts, unlock levels

---

## Visual Representation

Each gift should have a small icon (24×24 or 32×32) representing it:
- **Pebble**: Round stone
- **Leaf**: Green leaf
- **Twig**: Brown branch
- **Acorn**: Brown acorn with cap
- **Moss**: Green moss patch
- **Sap**: Golden amber drip
- **Flower**: Colorful flower blossom
- **Crystal**: Blue/clear crystal shard
- **Amber**: Warm amber chunk
- **Coral**: Pink coral branch
- **Silk**: White silk strand
- **Bone**: Bone shape
- **Pearl**: Iridescent pearl
- **Ruby**: Red gem
- **Sapphire**: Blue gem
- **Emerald**: Green gem
- **Diamond**: Clear/white gem with sparkle
- **Crown**: Golden crown shape

These should be added to the asset inventory and rendered in:
- Gift pickup animations
- HUD gift indicator
- Menu gift selection (if applicable)

---

## Testing Checklist

- [ ] All 18 gifts are in `k_gifts[]` array
- [ ] Each gift has unique ID and name
- [ ] Unlock levels are 1, 2, 3, 2, 3, 1, 5, 7, 4, 5, 6, 4, 7, 8, 9, 10, 12, 15
- [ ] Bonus multipliers are correctly applied
- [ ] `gift_compute()` produces correct combined bonuses
- [ ] Diamond multiplier stacking works (1.5x all other bonuses)
- [ ] Crown doubles all bonuses (2x)
- [ ] Visual effects render when active gift has them
- [ ] No performance regression from expanded gift system
- [ ] No memory increase (gifts are static data)

---

## Future Ideas

1. **Gift Synergy**: Specific gifts work better together
2. **Gift Crafting**: Combine gifts to create new ones
3. **Gift Trading**: Trade gifts with other spiders (multiplayer)
4. **Seasonal Gifts**: Limited-time gifts that rotate
5. **Quest Rewards**: Specific gifts as quest completions
6. **Gift Rarity Tiers**: Ultra-rare drops with higher bonus multipliers
