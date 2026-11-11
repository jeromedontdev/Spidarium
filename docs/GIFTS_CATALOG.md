# Gifts: implemented catalogue

There are **36** habitat gifts. Stable IDs still match `main/species.c` and the
asset filenames, but presentation names, prices and progression live in
`main/gift.c`. Only one is placed at a time. Each has clearer furniture art,
drawn at 64x64 on the tank using one reused 12,288-byte PSRAM buffer.

## Shared SNES art direction

All thirty-six gifts are decorative furnishings and share the background/decor
style, not a separate flat icon style. Their deterministic 16x16 logical art
uses 8-12 visible tones, ink rims, top-left lighting and discrete shaded planes.
Wood has grain and cut edges, shelters have dark openings, stone has chips,
crystal accents have facets, leaves have midribs and silk has folded strands.
Every gift has a distinct silhouette/composition, including the three lookouts
and royal perch. Filenames, IDs, 32x32 PNG sizes, 64x64 displayed size, price,
bonuses and saved ownership remain unchanged.

The generator and validator cover every `gift_*.png`; focused art tests reject
flat palettes and repeated gift images. See [the full style contract](ART_STYLE.md).

## Earn, buy, place

Silk is a separate wallet, never spent XP. New pets and migrated old saves get
20 starter Silk. Completed eight-round games pay 2 + 2 per correct answer
(2-18); perfect/good hunts pay 5/3. A bonded spider without an equipped gift
can contribute 2 Silk every 2-4 active minutes; it no longer creates free random
items that bypass locks.

Price = 12 + 4 times the unlock level. Both level and sufficient Silk are
required for a first purchase. The picker shows the balance, price, purpose,
locked level or ownership. Selecting an affordable item starts placement.
Payment happens on the actual placement tap, not selection: cancelling costs
nothing. The purchased bit is stored; owned items can be selected/repositioned
for free, including replacing another item. There are no duplicate purchases,
resale, refunds or multiple active gifts. Existing placed gifts are grandfathered
as owned, even if their new unlock level is higher.

| ID | Gift | Level | Silk | Passive gameplay benefit |
|---|---|---:|---:|---|
| 0 | Resting Stone | 1 | 16 | Sleeping energy recovery x1.50 |
| 1 | Leaf Shelter | 2 | 20 | Dirty-tank hygiene decay x0.60 |
| 2 | Jumping Branch | 3 | 24 | Maximum pounce distance x1.20 |
| 3 | Acorn Hide | 4 | 28 | Sleeping recovery x1.30; need decay x0.95 |
| 4 | Moss Patch | 5 | 32 | Probabilistic catch x1.15; prey flee speed x0.85 |
| 5 | Sap Trail | 6 | 36 | Energy efficiency x1.10 |
| 6 | Flower Lure | 8 | 44 | Shorter wild-spawn wait |
| 7 | Crystal Lookout | 10 | 52 | Aim window x1.25 |
| 8 | Amber Hearth | 12 | 60 | Mood target x1.10; need decay x0.95 |
| 9 | Coral Scrubber | 14 | 68 | Dirty-tank hygiene decay x0.80 |
| 10 | Silk Hammock | 16 | 76 | Probabilistic catch x1.20 |
| 11 | Bone Tunnel | 18 | 84 | Prey flee radius x0.70 |
| 12 | Pearl Toy | 20 | 92 | Mood target x1.10 |
| 13 | Ruby Launchpad | 23 | 104 | Pounce distance x1.25; probabilistic catch x1.15 |
| 14 | Sapphire Lookout | 26 | 116 | Aim window x1.15; detection radius x1.20 |
| 15 | Emerald Garden | 30 | 132 | Sleeping recovery x1.05; need decay x0.95 |
| 16 | Diamond Retreat | 35 | 152 | Balanced synergy factor x1.20 |
| 17 | Royal Perch | 40 | 172 | Balanced synergy factor x1.30 |
| 18 | Fairy Cottage | 4 | 28 | Sleeping recovery x1.40 |
| 19 | Moon Bed | 6 | 36 | Mood target x1.15 |
| 20 | Reading Nook | 8 | 44 | Aim window x1.20 |
| 21 | Teapot Hide | 3 | 24 | Hygiene decay x0.70 |
| 22 | Witch Hat | 10 | 52 | Prey flee radius x0.75 |
| 23 | Fern Nest | 7 | 40 | Sleeping recovery x1.35 |
| 24 | Neon Den | 12 | 60 | Movement energy efficiency x1.15 |
| 25 | Star Capsule | 14 | 68 | Hygiene decay x0.75 |
| 26 | Dragon Roost | 16 | 76 | Pounce distance x1.20 |
| 27 | Sun Temple | 18 | 84 | Detection radius x1.25 |
| 28 | Zen Pavilion | 5 | 32 | Mood target x1.15 |
| 29 | Petal Swing | 9 | 48 | Mood target x1.20 |
| 30 | Velvet Throne | 13 | 64 | Sleeping recovery x1.30 |
| 31 | Scholar Perch | 11 | 56 | Aim window x1.20 |
| 32 | Potion Hide | 15 | 72 | Prey flee speed x0.85 |
| 33 | Fossil Cradle | 17 | 80 | Sleeping recovery x1.40 |
| 34 | Holo Garden | 20 | 92 | All needs decay x0.95 |
| 35 | Torii Retreat | 22 | 100 | Balanced synergy factor x1.15 |

Synergy boosts beneficial multipliers and divides harmful decay/flee
multipliers. Catch uses both catch multipliers; their combined value is x1.44
for Diamond and x1.69 for Royal, not just x1.20/x1.30. Probability remains
clamped and guaranteed-perfect-hit rules remain. This replaces the overpowering
old x1.50/x2.00 synergy.

## Spider interaction

A healthy, awake idle/wandering spider with fullness >=60 can visit its item
after 15 active seconds. It walks toward the item and stops within 18px, on
the furniture rather than permanently avoiding it. Resting, Hiding, Climbing,
Watching, Grooming, Exploring or Playing feedback appears with a happy emote.
After three seconds it receives +4 mood/+4 energy and a 60-second visit cooldown.
Travel times out after 20 seconds. Hunger, sickness, low energy and hunting/
sleeping states take priority; held aiming still freezes AI. These are short
visit animations and care gains, not a new physics climbing system.

`gift_compute()` resets to neutral each logic tick and applies the selected
definition before needs/behaviour/prey updates. Removal immediately stops the
bonus. `needs.c` owns mood and drain/recovery; `behaviour.c` owns energy cost,
aim window/detection, and leap distance; `prey.c` owns flee/catch effects.

Passive bonuses remain active while equipped; visits add visible care events.
There are no predators, temperature/water simulation or stacked gift bonuses.

## Save compatibility and tests

The version-2 pet payload gains appended Silk, owned-bitmask and brightness
fields. The previous payload length is recognized through `offsetof(..., silk)`;
old bytes are copied intact, new fields initialized and the placed item retained.
Older SD backups also remain accepted and migrate when loaded. No reset or NVS
erase is needed. New saves and SD backups include the wallet/ownership.

`gifttest` checks all 18 bonuses and removal, exact-price purchase, insufficient
funds, locked items, free owned placement and a real three-second visit reward.
It restores the pet/wallet/ownership and does not persist synthetic purchases.
`uitest` compares the equipped 64x64 buffer against exact nearest-neighbor
source pixels.

## Adding a gift

Keep table indices stable: they are persisted. Add a definition to `k_gifts`,
update `GIFT_KINDS`, add deterministic art/asset mapping, and wire every new
multiplier to its actual consumer. Update this table and test neutral, equipped,
replaced, removed, and level-locked behavior. See [art guide](ART_STYLE.md).
