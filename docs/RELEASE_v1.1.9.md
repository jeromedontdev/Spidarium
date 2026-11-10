# Spidarium v1.1.9 — A Reason to Come Back Tomorrow

## The daily loop

Spidarium had no reason to be opened on a second day. Everything it offered —
care, hunting, gifts, minigames — was available in one long session, and the
only thing that changed with time was decay. v1.1.9 adds a daily loop.

**Check in.** Once per in-game day the Daily page offers a check-in. The reward
grows with a consecutive-day streak:

| Streak | XP | Silk |
|---|---|---|
| 1 | 6 | 4 |
| 2 | 8 | 6 |
| 3 | 10 | 8 |
| 4 | 12 | 10 |
| 5 | 14 | 12 |
| 6 | 16 | 14 |
| 7 (cap) | 18 | 16 |

Missing a day resets the streak to 1. The cap keeps the economy from running
away: a maximum streak pays 18 XP and 16 Silk, which is roughly one gift every
two days rather than a gift per visit.

**A daily task.** Each in-game day also offers one rotating objective, chosen
deterministically from the day number so a reload cannot reroll it:

| Day mod 4 | Task | Goal |
|---|---|---|
| 0 | Catch 3 Prey | 3 catches |
| 1 | Clean 2 Times | 2 cleans |
| 2 | Play 3 Times | 3 plays |
| 3 | Pet 4 Times | 4 pets |

Completing it pays a flat 12 XP and 8 Silk. Progress is measured against a
baseline snapshotted when the day turns over, so actions taken yesterday never
count toward today's task.

The loop is driven by the **in-game day counter**, not the RTC. The RTC is
optional — no battery, no sync without WiFi — so a wall-clock daily would either
never fire or fire on a bogus 1970 epoch. The in-game day advances with
`AGE_TICK_MS`, which `GAME_TIME_SCALE` compresses for verification builds, so the
whole loop is testable in minutes rather than days.

## Treats: a recurring Silk sink

Gifts are permanent and one-time. Once all eighteen were owned, Silk had nothing
left to buy, so the currency stopped mattering. Treats are consumables: they cost
Silk every time and give a short, visible boost.

| Treat | Cost | Effect |
|---|---|---|
| Juicy Fly | 6 Silk | +25 hunger |
| Silk Ribbon | 8 Silk | +30 mood |
| Warm Pebble | 10 Silk | +35 energy |

A purchase is refused, and logged, when the balance is short. The menu shows
"Need More" on any treat the player cannot afford.

## Save format v6

The daily loop needs five new fields, and `game_state_t` was exactly 136 bytes
with no padding room, so the payload grew to 152 bytes and `SAVE_VERSION` moved
from 5 to 6.

**Existing pets are preserved.** A v5 blob is a valid prefix of the v6 struct, so
`save_load()` and `save_raw_valid()` accept a 136-byte v5 payload and migrate it
in place: the new fields are defaulted rather than inherited from garbage. This
was verified by crafting a v5 blob from a live v6 export, restoring it, rebooting,
and confirming the pet's level, XP, stats and Silk all survived.

The browser backup/restore schema reports the new fields with `minVersion: 6`, and
the browser engine accepts v1–v6 saves.

## Two bugs found while building this

**A brand-new pet could never claim its first check-in.** `daily_day` used 0 as
"never claimed", but 0 is also a real in-game day, so `daily_day == today` was
true from the first boot. Fixed with a `DAILY_NEVER` sentinel, plus a self-healing
check that treats a zero streak as unclaimed.

**The task baseline was captured after the player acted.** Taking the snapshot
lazily on first read meant a player who cleaned twice and *then* opened the menu
had their baseline recorded as 2, so progress read 0/2 forever. The snapshot now
happens on the day rollover, and a first-tick call covers migrated and restored
saves that have no baseline at all.

## Verification

- Build exit 0, zero `main/` warnings.
- Flash exit 0.
- Device suite **47/47**.
- Soak `VERDICT: PASS`, 0 faults, `largest=65536` (floor 51,200).
- `scanloop 5` and `render 500`: `largest_drift=0`.
- Browser suite **84/84** (78 before, 6 new daily/treat tests).
- Menus verified on hardware by screen capture: Daily shows "Check In Ready" and
  the task with its progress; Treats shows all three with costs and "Need More"
  when short.
- Browser menus verified live: Daily claims and disables, Treats deducts Silk.

## Honest boundaries

The daily loop was exercised by forcing the in-game day through the serial
console (`daily day N`), not by waiting real days. The streak arithmetic, the
cap, the reset-on-skip behaviour and the task rotation are all covered by tests,
but a multi-day real-time soak has not been performed.
