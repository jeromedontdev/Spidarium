# Spidarium v1.1.10 — Petting, a Real Calendar Day, and a Menu That Looks Like One

## You can pet the spider again

The "Pet 4 Times" daily task was impossible: petting had been removed, and
`g.pets` was never incremented anywhere. Tapping the spider now pets it.

The mechanic was removed once because it competed with the slingshot for the same
gesture — a tap near the spider was ambiguous between "pet" and "start a pull".
That ambiguity is resolved by the gesture itself rather than by a mode: a click
only arrives when the finger did **not** drag, so a pull that starts on the
spider still fires, and a tap that lands on the body pets.

- Hit radius is `PET_RADIUS_PX` (56 px), the existing generous value, so petting
  never feels like a precision test.
- A pet raises mood by 6, bond by 1, and pays 2 XP plus the `first_pet`
  achievement the first time.
- `PET_COOLDOWN_MS` (1.2 s) refuses a repeat. Without it a held finger could farm
  mood, XP and the daily task.

**A bug found while testing:** the cooldown guard was `last_pet != 0`, but the
clock legitimately reads 0 early in a session, so the guard never engaged and a
rapid double-tap petted twice. It now uses a separate `petted_once` flag.

## The daily loop uses the real calendar day

The check-in was keyed to the in-game day counter, which meant it reset after 24
hours of *play* rather than at midnight. It now prefers the RTC, so the day is a
real calendar day. The RTC is optional — no backup battery, no sync without WiFi
— so when it is not valid the loop falls back to the in-game counter exactly as
before. The browser uses its own wall clock.

The console override (`daily day N`) now wins over both clocks, because the RTC
refuses dates before 2000 and a small day number therefore cannot be written to
it. `daily clock` drops the override.

## Menu overhaul

The menus were flat rectangles. They now have:

- A vertical gradient panel with a drop shadow, so the surface reads as a lit card.
- A corner **arrow** Back button instead of a wide "Back" label, with a gold
  border that marks it as chrome rather than content.
- An accent rule under the header that fades out to the right.
- Gradient row cards, with a lit accent bar down the left edge of the selected
  row so the current choice is legible at a glance.
- A flatter pager, so Prev/Next do not compete with the rows above them.

Two layout fixes came with it:

- **Settings is always the last row** of the main menu. It is the least-used
  entry, so it belongs at the bottom rather than in the middle.
- **The redundant full-width Back rows are gone** from Daily and Treats. The
  corner Back button already exists, and the extra row pushed the real content up
  on a list that only has two or three items.

## Verification

- Build exit 0, zero `main/` warnings.
- Flash exit 0.
- Device suite **47/47**.
- Soak `VERDICT: PASS`, 0 faults, `largest=65536` (floor 51,200).
- `scanloop 5` and `render 500`: `largest_drift=0`.
- Browser suite **88/88** (was 84; four new tests cover petting, the hit radius,
  the cooldown, the daily task it advances, and the wall-clock day).
- Menus verified by screen capture on hardware: gradient panel, arrow Back,
  accent rule, Settings last, no redundant Back rows.
- Petting verified on hardware (`OK pet mood=48 bond=1 pets=1 xp=167`, then
  `ERR pet refused` on an immediate repeat) and live in the browser
  ("Spider Petted.", with `pets` 3 → 4 on the first tap and unchanged on an
  immediate second).

## Honest boundaries

The RTC-first day was verified with the board's RTC reporting a real epoch day
(`day=20768`), but a genuine midnight rollover has not been observed — that needs
a day of wall-clock time. The fallback path is exercised by the console override,
not by removing the RTC.
