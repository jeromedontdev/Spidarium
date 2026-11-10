# Spidarium v1.1.8

## Clean pays for the mess it removes, not for the tap

Tapping Clean on a spotless tank used to pay 3 XP every time — an unlimited
faucet with no cost and no cooldown. The first fix gated the button, but a
grayed-out control looks broken and the gate was bypassable anyway: drop a bug
in, tap Clean repeatedly, and each tap still paid.

Clean is now always enabled and always works. The reward is proportional to how
much the tank actually improved:

```
score = dirt + (100 - hygiene)
xp    = ceil(score * 3 / 24), capped at 3
```

Dirt and hygiene share units because a spider dropping costs 12 hygiene to make
and is worth 12 dirt. Measured payouts:

| Mess | Dirt | XP |
|---|---|---|
| 1 bug speckle | 6 | 1 |
| 2 speckles | 12 | 2 |
| 3 speckles | 18 | 3 |
| 4 or more speckles | 24+ | 3 (cap) |

The bypass is closed structurally rather than by a gate: bugs alone are not
dirt, so parking a bug pays 0 XP, and after one clean the mess is gone so every
later tap scores 0. A tank holding only bugs is still cleaned — the bugs are
shooed out — it simply pays nothing.

## Bugs foul the tank

A bug left in the tank now leaves a pale speckle near itself every 2.5 minutes.
It is frass rather than a meal's worth of waste, so it is smaller and cooler in
tone than a spider dropping and worth half the dirt (6 against 12). Speckles
share the dropping slot pool and the 10-minute decay, so a parked bug is a real
consequence instead of a free meal.

## Two bugs found while building this

1. **Speckles vanished on the tick they were created.** `game_tick_render()`
   captured `now` before servicing the console's UI request, which stamped the
   new object with a later `lv_tick_get()`. `now - stamp` went negative, wrapped
   to about 4.29 billion as an unsigned value, and the object was deleted
   immediately. Fixed by re-reading the clock after servicing requests and by
   comparing with a signed difference in `terrarium_tick()`.
2. **The first guard was wrong.** `needs_clean()` returned early when the reward
   was 0, which left bugs in the tank on a bugs-only clean while reporting
   success. The guard is now "is there anything to do", with the XP computed
   separately.

## Browser parity

The website ports all of it: the same proportional formula, the same dirt
values, the same 2.5-minute bug timer, and the same pale speckle palette. Clean
is never disabled in the browser either. The browser suite grew from 73 to 77
tests covering the scaling table, the anti-bypass property, the bugs-only clean,
and the natural speckle timer.

## Verification

Build exit 0 with zero `main/` warnings, flash exit 0, device suite 47/47,
40-cycle soak `VERDICT: PASS` with 0 faults, `scanloop 5` and `render 500` both
at `largest_drift=0`, `largest=69632` (above the 51,200 floor), browser suite
77/77.

Speckle rendering was confirmed from pixels on both targets: the device capture
contained exactly 48 pixels in the three intended colours forming exactly 4
clusters of 12 pixels each at the 4 reported positions, and the browser canvas
painted 24 pale pixels for 3 speckles while a spider dropping painted none.

See the [progress log](../PROGRESS.md) for exact evidence.
