# Spidarium v1.1.11 — a petting budget, a petting bounce, and a quieter pager

Four refinements to the v1.1.10 petting and menu work.

## Petting pays XP, but only for the first few pets of the day

Petting was uncapped. A tap is the cheapest gesture in the game, so a bored
thumb was an XP faucet: hold a finger on the spider and the level counter climbs
for free.

The reward is now a **per-in-game-day budget** of `PET_XP_DAILY_MAX` (10 XP, or
five pets at `PET_XP` = 2). The cap is on the currency, not on the affection:

- Mood still rises by `PET_MOOD_GAIN` on **every** pet.
- Bond still rises on every pet.
- The spider still reacts to every pet.
- The daily "Pet 4 Times" task still counts every pet.

So the cap is invisible unless the player is deliberately farming. The budget
resets when the in-game day changes, using the same RTC-first day the daily loop
uses, so it follows the calendar rather than the session.

`stat` reports it as `pet_xp=N/10`, and `pet` reports it per call.

## Petting has an animation

The old handler called `spider_play_once(ANIM_IDLE, true)`, which is a no-op:
`ANIM_IDLE` has a zero duration, so `spider_play_once()` returned immediately
and the pet produced no visual reaction at all.

The reaction is now a **timed overlay** in the canvas renderer: a gentle
`PET_REACT_PX` (9 px) rise and fall over `PET_REACT_MS` (520 ms), with the front
legs forced into the happy wave for the duration.

It is deliberately *not* a state change. `ui_hud_tick()` recomputes the emote
from the game state on every frame, so an emote set from the pet handler would
be overwritten before it was ever drawn. Holding the reaction in the renderer
also means a pet never interrupts a hunt, a sleep or a molt.

## The pager is two arrows

`Prev` / `Next` became bare `LV_SYMBOL_LEFT` / `LV_SYMBOL_RIGHT` chevrons, 96×56
like the corner Back button. The words made the pager read as content rather
than chrome, and they competed with the row labels above them. The page counter
stays centred between them.

## The header accent rule fades the other way

The rule under the menu heading faded from bright green on the left to the panel
colour on the right, which put its brightest end directly beneath the Back
button and read as a highlight on the wrong control. It now fades from dark on
the left to bright on the right, so it runs away from Back and toward the title.

## Save format v7

`game_state_t` grew from 152 to 160 bytes for the petting budget
(`pet_xp_day`, `pet_xp_today`). A v6 blob is a valid prefix of the v7 struct and
is migrated in place: the budget opens fresh rather than inheriting garbage. v5
and v2 blobs are still accepted.

## Verification

| Check | Result |
|---|---|
| Build | exit 0, zero `main/` warnings |
| Flash | exit 0 |
| Device tests | 47 / 47 |
| Soak (40 s) | `VERDICT: PASS`, 0 faults |
| `largest` internal | 65,536 bytes (floor 51,200) |
| `scanloop 5` | `largest_drift=0` |
| `render 500` | `largest_drift=0` |
| Browser suite | 90 / 90 |
| Pet cap on hardware | 5 pets → 10 XP, then 0 XP with mood/bond still rising |
| Menu capture | arrows confirmed, rule bright end at x≈420 (right) |
