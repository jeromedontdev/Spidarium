# Spidarium agent handoff

## Start here

Read the [operating manual](../.github/copilot-instructions.md), then
[whole-game PSRAM/threading policy](PSRAM_THREADING_ARCHITECTURE.md).
For task-shaped recipes — changing a menu, adding a save field, adding a UI
layer, tuning constants, capturing screenshots, verifying and publishing — see
the [agent playbook](AGENT_PLAYBOOK.md).
Historical claims that WiFi cannot work on this board are superseded: real
hardware scans now succeed. Final acceptance evidence belongs in the latest
[progress round](../PROGRESS.md), not older round summaries.

## Implemented surfaces

- Ambient life (`ambient.c`) is the "living ecosystem" layer, and it costs no
  new art. Two effects, both drawn from primitives and palette the renderer
  already had:
  - **Dust motes.** Eight 2-4 px squares drifting upward with a sideways sway,
    wrapping at the top of the playfield. Tinted from `theme->dust`, which was
    defined in the theme table and never read by anything until now -- so the
    motes are theme-aware for free. Created once in `ambient_init()` and moved,
    never created or deleted per frame. Hidden while the tank is hidden, because
    moving invisible objects still costs a redraw.
  - **Idle life.** `ambient_breath()`, `ambient_blink()` and `ambient_glance()`
    are sampled by `cspider_render()`, so the spider is never perfectly still
    even in IDLE. Breathing scales **only the abdomen** -- scaling the whole body
    makes the legs slide. A blink is drawn as a closed lid, not a shrunken
    circle: a shrinking circle reads as the eye receding. A glance shifts the
    pupils, not the head, because turning the head sweeps the whole leg fan and
    reads as the legs moving on a stopped spider. All three are frozen while
    asleep; a breathing sleeper reads as a rendering bug.
  - **Amplitude matters.** The breath was 3% at first, which on a 13 px abdomen
    is a third of a pixel and rounds away entirely -- the effect was invisible
    until it was raised to 8%, the smallest value that actually moves a pixel.
    `ambient_test()` asserts the range, that the motes move, and that the
    enclosure light drifts without flickering.
  - `ambient_light_opa()` drifts the dimmed-enclosure overlay a few percent
    around its historical 110. A fixed value reads as a filter laid over a
    photograph; a slow drift reads as a room whose light is coming from
    somewhere. The swing is capped at 20/255 so it never looks like a flicker.
  - Browser parity lives in `site/lib/ambient.js`. The device uses `esp_random()`
    for its intervals, which cannot be reproduced, so the browser derives the
    same shape from a hash of the time bucket -- the result reads the same and is
    stable under test, which matters because a test that asserts "the spider
    blinks" must not be flaky.
  - **Web strands run all the way around the frame.** `k_web_zones[]` holds
    twelve anchor zones -- four corners, four edge midpoints, four edge
    thirds -- because a web is spun where the spider can anchor it, which is the
    enclosure frame, not only the top. Each zone also carries a `base` fan
    direction: a strand anchored to the bottom of the tank has to point UP or it
    is drawn off the panel, and one anchored to a side has to point inward. The
    fan half-width is `WEB_FAN` (0.6 rad); a wider fan let a bottom-corner strand
    point straight down. `AMBIENT_STRANDS` is 18.
  - **Strands are presented on their own, slower cadence.** Each strand is a
    ROTATED object, so LVGL classifies it as `LV_LAYER_TYPE_TRANSFORM` and
    allocates an intermediate layer for it on every refresh -- far more expensive
    per object than a plain rectangle. Raising the count from 6 to 18 tripled the
    ambient cost (443 -> 2026 us/tick) and halved the frame rate when they shared
    the 10 Hz present. `AMBIENT_STRAND_PRESENT_MS` (250 ms) gives them a 4 Hz
    cadence instead: a strand sways at 0.05-0.21 rad/s, so a whole degree of
    rotation takes 80-350 ms and nothing visible is lost. Measured back at
    baseline afterwards (385-523 us/tick, 34-42 ticks per 12 s).
  - **Decor particles are parented to their own layer, above the decor.**
    `terrarium_particle_layer()` is created last in `terrarium_init()` and raised
    with the canopy. They used to be parented to the background's parent, which
    put them BELOW the decor band -- so a particle spawned at an item's own
    position was drawn behind that item's artwork and was never visible. That was
    the whole reason the effect could not be seen despite the emitter running.
  - **Particles are brightened and fully opaque.** The theme's `dust` is a
    mid-tone that sits close to the cork and soil colours it is drawn over, so a
    3-5 px dot tinted with it had no contrast. `brighten(dust, 55)` mixes toward
    white, and the peak opacity is 255 rather than 200.
  - **Two sentinel bugs made particles invisible even once layered correctly.**
    `last_opa` was initialised to `0xFF`, which equals the real peak of 255, so
    the first opacity comparison was a no-op and a particle spent its whole life
    at its creation value of 60/255. And `ambient_set_visible()` reused
    `last_opa == 0` as "hidden", so a particle that activated afterwards was
    never unhidden. `particle_t.hidden` now tracks visibility separately and
    `last_opa` starts at 0.
- **Draw order is proximity-based.** `decor_layer_count_at()` counts only items
  whose ARTWORK OVERLAPS the one being ordered, because draw order is only
  visible where two sprites cover the same pixels. Ordering a fern against a rock
  at the far end of the tank changed nothing the player could see, and offering
  the control there implied a relationship that did not exist. The gift uses the
  same rule (`decor_gift_layer_count()`). `decorlayers` on the console reports
  the per-item counts.
- **Climb is retired.** The 2.5D lift never read convincingly on the panel: the
  spider slid up a flat sprite with no perspective change, and the shadow and
  hitbox both had to be special-cased to hide how wrong it looked. The option is
  gone; `DECOR_CLIMB` survives in the enum only so old saves parse, and
  `decor_migrate_paths()` rewrites it to Walk Behind on load. `decor_set_path()`
  refuses it, so no code path can reintroduce it. The lift maths is still
  exercised directly by `decor_test()` so it is not silently broken if Climb is
  ever reinstated.
- **`art_scale` decouples drawn size from footprint.** `radius` used to drive
  both the collision footprint and the drawn size, so a bigger-looking prop was
  necessarily a bigger obstacle -- which is why the early items all read at
  roughly the same size. `art_scale` is a percentage of the footprint-derived
  size (100 = historical, 0 = unspecified = 100), so a fern can look lush without
  becoming harder to walk around and a flower can look delicate without becoming
  easier to walk through.
- **Pounces never aim into solid decor.** `clamp_out_of_solid()` in
  `behaviour.c` pushes the TARGET out of any solid item before the flight starts,
  so the arc is correct as well as the destination. The landing re-runs the same
  clamp as a belt-and-braces pass, because decor can be moved mid-flight by a
  restore and a landing inside an item is the one state the spider cannot walk
  out of.
- **Prey graze.** A bug that is not fleeing stops to feed for a moment, then
  moves on (`PREY_GRAZE_*`). Without it, prey either drift constantly or stand
  perfectly still, and both read as mechanical. A graze is cancelled the moment
  the spider gets close, because the flee branch takes over -- so a pause is
  never a way to get caught standing still. The flutter offset is suppressed
  while grazing, or a "resting" moth would still slide sideways.
- **The day/night tint is visible.** The old version was a single dark rectangle
  at 12-24/255 opacity -- a 5-9% wash, invisible on a 480x480 AMOLED in daylight,
  and the reason the "day and night" setting appeared to do nothing. It also only
  ever darkened, so every period looked the same, just slightly dimmer.
  `k_light_tints[]` in `game.c` now changes COLOUR as well as opacity: morning is
  warm gold at 16/255, afternoon is untinted, evening is amber at 30, night is
  deep blue at 52. The tank reads as a different time of day rather than as a
  slightly dirty screen. These were halved from 26/54/96 when the light shafts
  came back: stacked, the two together greyed the whole tank out.
- **Light shafts were removed, not fixed.** They were tried three ways. Rotated
  LVGL rectangles were software rendered at roughly 111,000 blended pixels each
  per frame and made the whole tank choppy. Unrotated bands were cheap but read
  as blocky vertical stripes going one way. Baking them into the background
  fixed both, but a baked beam cannot move, and static light does not read as
  light -- it reads as a stain on the art. Making it move means invalidating a
  large alpha region every frame, which is the choppiness again.
  The lesson is that a full-screen lighting effect is the wrong shape for this
  budget. Aliveness comes from many small things that move independently.
- **Aliveness is four cheap layers, not one expensive effect.** Dust motes
  (16 small squares rising with a sway), spores (6 larger, slower specks crossing
  the tank on both axes), web strands (18 thin lines radiating from shared anchors
  all the way around the frame, pivoting rather than translating), and
  decor/gift particles. Each is a handful of LVGL objects created once and moved;
  nothing is allocated per frame.
  The two air layers use different palette columns (`dust` and `web`) so they
  read as depth rather than as one noisy field.
  The counts are deliberately generous. The first pass used 8 motes, 3 spores,
  2 strands and a 16-particle pool, and the effect was invisible on the panel --
  two strands read as two decorations rather than as a web, and with a dozen
  items placed each one got a particle roughly once every four seconds.
- **The cost of an ambient object is its DIRTY RECTANGLE, not its CPU time.**
  This is the single most important thing to know before adding another layer.
  `ambient_present()` used to push every object to LVGL on every render tick.
  Simulating a mote is a few float writes, but moving it adds a dirty rectangle,
  and these objects are scattered across the whole tank -- so the flush had to
  cover most of the screen every frame. Measured with the `ambient off` console
  toggle, the layer was costing about 4.5 Hz of frame rate.
  Three rules follow, and all three are load-bearing:
  1. **Throttle the present, not the simulation.** `AMBIENT_PRESENT_MS` (100)
     pushes the layer at 10 Hz while `ambient_simulate()` still runs every tick.
     Dust drifts at 4-12 px/s, so at 10 Hz it still advances less than a pixel
     per update -- identical to the eye, a third of the invalidations.
     The strands get their own, slower cadence (`AMBIENT_STRAND_PRESENT_MS`,
     250 ms) because each one is a rotated object and therefore a transform
     layer; see the strand note above.
  2. **Dirty-check every setter.** Motes, spores, strands and particles cache
     their last integer position, opacity and size and only call LVGL when the
     value changed. A mote moves a fraction of a pixel per tick, so most ticks
     are now free. Inactive particles are hidden once, not every tick.
  3. **Never re-set a full-screen overlay per frame.** `tick_enclosure_lights()`
     called `lv_obj_set_style_bg_opa()` on a 480x480 overlay unconditionally;
     that is the most expensive invalidation in the app. It now only pushes when
     the opacity step changes. The weather specks got the same treatment.
  `ambient off` / `ambient on` exists so this can be A/B measured rather than
  guessed at. Use it before and after any change to the layer.
- **Web strands are dynamic, and the anchors are not.** Each strand has a life of
  20-70 s, fades in over 1.5 s, hangs, and fades out over 2.5 s before respawning.
  `web_anchors_init()` runs ONCE and the per-zone anchors are fixed for the boot;
  a respawn re-randomises the angle, length and opacity but keeps the anchor.
  Re-randomising the anchor would dissolve the clusters back into scattered
  lines, which is exactly the bug that made the web read as "too robotic".
  `strand_spawn()` is therefore pure (no `lv_*`) -- it is called from
  `ambient_simulate()`, which the console task runs during the self-test. The
  object is updated by `ambient_present_strands()` when it sees the `dirty` flag.
- **A zero rotation must not be expressed as a transform.** `apply_rotation()` in
  `power.c` wraps every UI layer in a full-screen `s_rotor` object and used to
  set `transform_rotation` to `rotation * 900` unconditionally -- including 0.
  LVGL classifies any object with a non-zero `transform_rotation` as
  `LV_LAYER_TYPE_TRANSFORM` and allocates a full 480x480 intermediate layer plus
  a transformed blend of every pixel, every frame, even when the transform is the
  identity. At rotation 0 the property is now REMOVED
  (`lv_obj_remove_local_style_prop(s_rotor, LV_STYLE_TRANSFORM_ROTATION, 0)`)
  instead of set to zero, which keeps the common upright case on the plain draw
  path. Note that `lv_obj_remove_style_transform_*` helpers do not exist; the
  generic `lv_obj_remove_local_style_prop()` is the working API.
- **Decor and gift particles are a FIXED POOL, not one emitter per item.** The
  tank can hold twelve decor slots plus a gift, and per-item emitters would make
  the cost scale with how much the player has bought -- the worst possible
  property for a frame budget. `AMBIENT_PARTICLES` (48) objects are created once
  and reassigned to whichever items are on screen, so the cost is constant
  whether the tank is empty or full. The emitter style is derived from the item's
  own kind (`ambient_emit_for_decor`), so nothing has to be authored per decor
  entry and no artwork is needed: a particle is a small tinted dot. That is what
  makes it cheap in both compute and tokens.
  The pool size is the wrong place to economise -- the objects are created once
  and only moved. The spawn cadence is the lever that controls how alive the tank
  feels, and it is a few float writes per spawn.
- **The render tick runs at ~15 Hz, and that is PRE-EXISTING.** Do not chase it
  as an ambient regression. Measured on commit `1073241` with the ambient work
  stashed: `rendercost` -> `ticks=377/381` over 25 s, the same ~15 Hz. The render
  callback is only ~3 ms of a 66 ms cycle; the other 63 ms is LVGL's own flush.
  A full 480x480 RGB565 refresh over QSPI is in that ballpark, which suggests the
  screen is being largely redrawn each frame. That is a display-pipeline question
  and needs its own round.
  `rendercost` now also prints `RENDERSECT`, a per-section breakdown
  (`terrarium` / `ambient` / `spider` / `hud` / `other`), so a regression can be
  attributed instead of guessed at. The spider render dominates at ~2050 us.
- **Both blocking paths block against the ARTWORK, not the collision footprint.**
  `radius` is the steering footprint and is deliberately smaller than the sprite:
  a crystal has radius 12 but is drawn 57 px wide, so blocking only 12 px from
  the centre let the spider walk straight over the visible crystal. `art_scale`
  makes the gap worse, not better, because it scales the sprite without touching
  the footprint. `decor_art_extent()` is now the single measure used by
  `decor_blocks_point()`, `decor_block_radius()` and the steering berth.
- **`ambient_simulate()` is pure and `ambient_present()` touches LVGL.** The
  split is not tidiness. The self-test runs on the console task, and calling
  `lv_*` from there corrupts LVGL's state -- that was a real crash, not a
  theoretical one. The simulation half is also what makes the effects testable
  without a display.
- **The particle self-test suspends emission.** The render task keeps running
  during a console command, so without `s_emit_suspended` the live tick spawns
  particles between the test's reset and its count. That race made the test pass
  once and then fail on every subsequent run.
- **The placement layer row refreshes as the item is dragged.**
  `placement_position()` calls `ui_hud_placement_changed()`, because the layer
  count depends on which items the item currently overlaps and that changes under
  the player's finger. Without it the row only updated on a button press, so the
  control appeared dead until Done was pressed and the item picked up again.
- **The top arrows select the ITEM, the bottom row selects the LAYER.** The top
  arrows used to be a second copy of the layer control, which was redundant: the
  bottom row already does that, and there was no way to switch to a different
  placed item without cancelling out to the menu and starting again.
  `game_on_placement_item()` steps `g_decor_move_slot` to the next occupied slot
  (wrapping, and a full lap with nothing else placed leaves the selection alone)
  and re-stages through the same path `decor_begin_move()` uses.
  `game_placement_item_label()` supplies the "name / n of m" caption and returns
  false when there is nothing to cycle, which is what hides the row for a new
  item or a gift.
  The stepping lives in `placement_step_item(int step)` rather than in the event
  handler, because `lv_event_get_user_data(NULL)` returns NULL -- calling the
  handler with a NULL event always steps by zero, which is exactly what made the
  first version of the test fail. `test_placement_item_selector()` drives the
  helper directly and reports its own `selector=` flag in `uitest`, so a failure
  there is not masked by the pre-existing `pointer=0` baseline.
- **`life_clock_now()` is the single clock read.** `life_visual_tick()` used to
  call `time(NULL)` directly while the mind used `life_period_now()`, so `rtcset`
  changed what the brain saw but not what the screen showed. Everything that
  reacts to the time of day now goes through `life_clock_now()`, and
  `life_clock_override()` lets the console (`dayperiod EPOCH`) drive the tint at
  a chosen time.
- **Screen dimming is a courtesy for a screen about to switch off, not a separate
  power feature.** `brightness_policy()` used to fall back to a flat 60 s dim
  whenever the timeout was "Never", so choosing Never still dimmed the panel after
  a minute -- the exact opposite of what the setting says. With no timeout there
  is nothing to dim towards, so there is no dim.
- **The Battery menu is two groups.** What the battery is doing now (read-only),
  then how the screen behaves. Deep sleep moved to its own submenu because it is a
  separate mode that stops the pet, not another display preference, and the
  confusing "Automatic Sleep" row (which only ever described RTC mode) is gone --
  the mode row already says "RTC Sleep".
- The neglect cycle (`terrarium.c`) makes hygiene visible instead of numeric. A
  dropping left alone for `FLY_ATTRACT_MS` (45 s) attracts a fly that orbits it;
  the fly is drawn from the same primitives as the droppings, so it costs no
  art. One fly per dropping, capped at `MAX_FLIES` (4), and `terrarium_clear_poop()`
  frees them, so cleaning visibly settles the tank. `terrarium_tick()` reaps
  before it attracts -- a fly whose dropping just expired must not be counted as
  occupying a slot, or the tank would slowly stop attracting. The browser
  derives the same cycle from the poop list (`fliesAt()`) rather than keeping
  separate state, so it cannot drift out of sync with the mess it describes.
  `pests` on the console reports `poop`/`dirt`/`flies`.
- The spider's MIND (`mind.c`) is a rules table, not a state machine. It
  publishes an intent (`mind_want()`) that `behaviour.c` consults through
  `act_on_intent()`. **The bridge is the part that matters**: `mind_tick()` ran
  for months while nothing ever read `mind_want()`, so the brain computed an
  intent every four seconds and threw it away. If you add a want, add its branch
  to `act_on_intent()` or it will be inert. The bridge is consulted from IDLE
  and from a WANDER with no destination -- the second is essential, because a
  spider that had started wandering never reconsidered.
  Rules carry three environment columns beyond the four stats: `period` (day
  period from `life_period_now()`, 255 when there is no clock, which matches no
  specific period so time-of-day behaviour is absent rather than wrong),
  `climbable` (a climbable item is placed) and `gift_placed`. A rule whose
  condition cannot be satisfied simply never fires, so the table degrades
  cleanly on a fresh tank. `mind_test()` asserts the table is well formed, that
  survival outranks every comfort rule, and that the dwell timer does not
  restart when the intent is unchanged.
  Two details keep it from feeling twitchy: a `MIND_DWELL_MS` (2.5 s) dwell
  before acting, and the dwell timer restarting only when the INTENT changes,
  not when the rule that produced it changes -- two rules can both want
  `explore`, and resetting as they swap would keep the spider from ever acting.
  `mind_wake()` lets a state change (being fed) be reflected immediately.
- Five minigames share one round/score/reward framework in `ui_menu.c`:
  Bug Hunt, Color Match, Quick Tap, Web Weave and Pounce. `PLAY_KINDS` and
  `k_play_names` are the single source of truth; `play_open()` lays out the
  tiles and the two spider-themed games hide them. Web Weave lights four
  anchors in a growing order and the player taps the order back (it never
  repeats an anchor back to back, because a doubled tap reads as one tap).
  Pounce places a single tappable target at a random spot that shrinks each
  round. Both are mirrored in `site/lib/minigames.js`.
  `companion_state_t.play_counts` is five entries so a favourite game can be
  discovered for any of them; the two extra bytes came from the former
  `reserved` tail, which is why the save version had to move to v23.
- Development v1.1.31/save v18 adds optional training, subtle RTC rhythms and
  weather, visitors/season rewards, real photos and safe generations. See
  [the life-system guide](COMPANION_LIFE.md) for gates, limits and verification
  boundaries. `life.c` owns bounded model state, `album.c` uses the existing
  persistence worker, and only LVGL owns screenshots/viewer objects. Keep
  three archived pets with explicit confirmed swaps; habitat/currency remain
  shared. Never overwrite an archive or photo automatically. Keep
  firmware/browser/schema paired.
  Large diagnostic state backups must use explicit PSRAM, not the8KB LVGL stack.
  Photo deletion is confirmed and worker-owned, including damaged slots;
  success needs verified absence or a committed browser IDB transaction.
  Gameplay precedes memories/customization; Help and Settings are last.
  Display saves Double-Tap Lights (default enabled) plus independent manual
  Dim Enclosure. No Settings screenshot row; keep diagnostic console captures.
  v17 reuses life byte15 for light_gesture_disabled (0/1), with no size growth.
  Preserve the global preference across generation swaps; v2-v16 migrate0.
  Audio hardware gain is fixed; existing PCM gets logarithmic master gain
  and20ms ramps. Refill telemetry is not proof of crackle-free audible output.
  Photo Album contains Take Photo, Storage Status and Photo Storage.
  Photo Storage lists only saved images; Generations is a separate main-menu
  feature with no album shortcut. Album entry queues a scan for current status.
  Both UIs retain session menu pages across Back/rebuild. Native page capture
  occurs before ui_menu_close resets its page; browser list clamps deleted pages.
  Older short effects taper their last5ms to zero; music/pet/USB hashes stay intact.

- Silk is capped at `SILK_MAX` (99,999), not `UINT32_MAX`. Saturating at the
  type limit let a legitimate balance reach 305,213, at which point every price
  is meaningless and a greyed shop row is indistinguishable from a Silk
  problem. `game_add_silk()` clamps, and `save_load()` clamps an over-cap
  balance on load rather than rejecting the save, so an existing bloated save
  is repaired instead of destroyed. The cap is far above the total cost of
  everything purchasable, so it never blocks legitimate play.
- A level-locked shop row says "Level N Needed", not "Level N / M Silk". The
  old subtext named the Silk cost, so a player with plenty of Silk could not
  tell why the row was grey.
- Save v23 grows the companion's `play_counts` from three entries to five, taking
  the two bytes that were `reserved`. The struct size (592) and every following
  offset are unchanged, but the MEANING of bytes 76..77 changed, so this had to
  be a new version: two firmwares both claiming v22 with different layouts would
  each read the other's save as valid and silently corrupt it. That is exactly
  what happened during development -- flashing the older firmware over a v22 save
  reset the pet. v23 accepts v22 and clears the two new counters, which were
  always written as zero.
- Save v21 adds `climb_owned` at byte3099, the last v18 reserved byte, so the  payload prefix stays 3,100 bytes and the schema stays 57 fields. It is the
  ownership bitmap for the six large climb structures (decor kinds 68..73);
  `catalogue_owned_high` is full because its high nibble already holds decor
  64..67. v21 accepts decor kinds 0..73, v12..v20 accept 0..67. A large
  structure in an older save is rejected rather than silently reinterpreted.
- Save v20 adds `gift_path` at byte3098, reusing a v18 reserved byte, so the
  payload prefix stays 3,100 bytes. The gift's path control has since been
  REMOVED: it is always walked over, and the migration folds any stored value
  back to Walk Over. The byte is kept so the payload layout does not change.
  v20 accepts gift paths 0..3, v13..v19 accept 0..2, older 0..1.
- Save v19 adds the Climb decor path (value 3 in the existing blocking byte).
  No new fields: the payload prefix stays 3,100 bytes, bound 23,308, 55 schema
  fields. v19 accepts paths 0..3, v13..v18 accept 0..2, older 0..1. A Climb
  record in an older save is rejected rather than silently reinterpreted.
- Save v18 appends `decor_locked` and `auto_clean` at bytes3096/3097 plus two
  reserved bytes, keeping the payload 4-byte aligned. Current fixed prefix is
  3,100 bytes, payload bound23,308 and the compiled schema has55 fields.
  v17 and earlier default both settings to off. Read all v2-v17 backups using
  their original tail offsets.
- Save v16 appends the pointer-free 2,236-byte optional life state at byte860:
  three training tracks, an active-time cooldown, real-calendar reward
  highwaters, cosmetic toggles and three complete 740-byte retired-pet records.
  Current fixed prefix is3,096 bytes; payload bound23,304 retains all3,368
  extra decor slots. Read all v2-v15 backups using their original tail offsets.
  Retirement/resume swap pet-only state and enqueue one complete snapshot;
  capacity never evicts pets and queue rejection rolls back the full state.
  No RTC means unavailable calendar events, not invented seasonal rewards.
  `life_test()` is non-destructive and never submits a real save.
- Icon policy: distinct features/actions need dedicated recognizable artwork;
  no shared generic placeholders, recolors or renamed duplicates. Repeating
  the same action (Back/Next) may retain its icon. Apply across device/browser/
  website; see [the style contract](ART_STYLE.md#dedicated-icons-no-reuse-for-different-meanings).
  Companion and Sound submenu entries now have dedicated deterministic icons;
  native UITEST checks distinct loaded icon indices, not just filenames.
- Development v1.1.31 adds companion preferences, optional Today requests and
  a pinnable Diary. The existing temperament/bond/art are preserved. Main-menu
  Your Spider/Today/Diary expose the new state. Today combines Check In, the
  original daily task and both optional requests; old Daily routes alias it.
  Preserve once-only claim guards, task rewards and check-in streaks.
  Treat/game/gift favorites discover after three actual successful events,
  remain stable and add two mood. Manual catches only, completed minigames,
  completed gift visits, accepted petting and needed cleaning feed requests.
  Two requests/day pay five Silk automatically once; Not Today has no penalty.
  **Today is now all claimable.** The dismissible request row is gone: Today is
  the check-in plus `DAILY_TASK_SLOTS` (3) rotating tasks, each independently
  claimable. Slot 0 is the day's own task and the others step forward from it, so
  the set changes daily and every kind still comes around. Requests still pay out
  passively when their event happens; they are simply no longer a chore on this
  screen. Save v24 appends the two extra task baselines and the done bitmask
  (3104 -> 3116); slot 0 reuses `daily_task_base`/`daily_task_done` so an existing
  save keeps its progress.
  UTC is used internally when reliable, without visible UTC labels; otherwise
  use explicit play days. Backward time
  cannot reroll; acquiring calendar time retains the current pair/progress.
  Sixty-four memories/eight pins use bounded copied records, never screenshots.
  Save v15 appends 592 bytes at 268 (860-byte prefix), then six-byte decor tail.
  Historical v12-v14 tails stay at 268 and v10-v11 at 260. Old backups migrate
  without fabricated historical memories.
- Sound > USB Chimes controls both attach/detach cues independently of effects
  and music; master mute/zero volume still win. The saved usb_sounds flag uses
  former padding byte267; it defaults on for new pets and all pre-v15 backups.
  Save v15 retains an 860-byte prefix/592-byte companion state; schema has 53 fields.
  Its 21,072-byte payload bound preserves the old maximum decor capacity.
  Console `set usb_sounds 0|1` is strict. Browser mirrors the saved checkbox but
  does not pretend to monitor USB; actual device PMIC boot checks verify output.

- USB chimes: the battery worker confirms two one-second PMIC samples for
  attach/detach and atomically posts the rising/falling event, including USB boot.
  Read errors/contact bounce/charging-full changes must not re-arm it.
  LVGL queues SFX_CHARGE (10) / SFX_UNPLUG (11); only the audio worker writes
  320ms rising/falling two-note PCM in the existing PSRAM buffer. Respect master
  mute/zero volume, not the independent music setting. No USB deep-sleep wake.
  power_test covers edge sequences; audiotest checks PCM bounds/endpoints.
- Ten original soundtracks use the same integer oscillator/PSRAM stream.
  IDs0..2 are byte-identical;3..9 are Crystal Echoes, Mosslight Waltz, Ember
  Hearth, Starlit Web, Bamboo Rain, Clockwork Fern and Dewdrop Lullaby. Native
  and browser score tables/hashes/loop seams match. Save v14 allows IDs0..9
  in the existing track byte; v11-v13 allow only0..2. Read old v2-v13 backups;
  old firmware cannot read v14 saves. Test all tracks and last-page selection.

- The completed decor overhaul starts with the original sixteen natural props
  and their five derived legacy PNGs. tools/core_decor_art.py authors explicit
  32px material geometry, a transparent one-pixel gutter and exact2x outputs.
  Keep the existing IDs/dimensions/radii/blocking defaults and pet. Remaining
  52 props/all36 gifts/12 treats now use tools/furnishing_art.py, with explicit
  item details and connected material planes instead of diagonal speckles.
  tools/small_art.py handles all40 prey frames/30 icons/11 badges at their
  original grid sizes. These181 final sprites preserve the approved96 terrain/
  16 core/five legacy images and39 spider exports/both renderers. Read the
  current asset/style guide for anatomy reuse and finite-palette composition.
  Latest progress records v1.1.28 build/device/pet/publication evidence.

- All47 non-Crystal background/border pairs now use the approved offline moss
  direction. Exact moss floor/frame are hash-pinned imports;46 other pairs use
  explicit theme recipes in tools/terrain_art.py with fixed seeds and discrete
  material planes. Generated centers allow16 tones/luma64; Minimal keeps3/24.
  Moss allows17/80 but is hash-pinned. Preserve the thin16px rim and transparent
  208x208 opening. That terrain pass preserved every non-terrain PNG and the
  Crystal pair. No new buffers/save fields or PixelLab spend. Round90 records
  its device evidence; Round92 records inclusion in the v1.1.28 publication.

- Crystal background14 now imports the exact user-approved PixelLab floor from
  `assets/sources/crystal_floor.png` at240x240. Generation stays offline and
  deterministic. Its pinned decoded-pixel hash replaces the three-tone-center
  check for Crystal only. Crystal border14 imports
  a separate hash-pinned120x120 PixelLab source, explicitly nine-slices its18px
  rim to8px, then doubles to240x240. Binary alpha/16 colors/transparent208x208
  opening remain strict. Its pixels are unchanged by the offline catalogue redraw.
  Preserve the spider, current pet and other artwork. See latest progress for
  actual flash/activation evidence and v1.1.28 publication.
  Read [the PixelLab workflow](PIXELLAB_WORKFLOW.md) for successful reference
  roles/prompts and cost bands. The final10 trial generations returned four
  border candidates in one request; the trial is now exhausted.

- The light-gesture fix uses independent 450ms/48px tap pairs instead of
  LVGL's single/double/triple streak. Allow 24px tap jitter outside hunting;
  preserve hunting's 18px shot threshold. Exclude holds, out-and-back drags,
  placement and HUD/menu interactions. Petting waits 475ms. Keep native/browser
  behavior aligned and preserve the actual-pointer regressions.
  `silk add N` now grants testing currency on LVGL, with overflow rejection and
  console acknowledgement only after the save worker finishes. See the serial
  guide; it adds to the current balance, not a replacement save or set-total.

- Help now appears only in the root Menu on native/browser. Decor/gifts and
  their hold-to-edit action use draggable artwork previews: release does
  not commit, Done saves once, Cancel keeps the previous placement. Preserve
  existing purchase timing and the original gift variant when moving. Native
  preview artwork/ring/Done/Cancel controls are retained objects, not recreated
  while dragging. See the latest progress round for pointer/memory evidence.
  Holding furniture opens inline Put Away/Done/Cancel controls, not a submenu.
  New/moved decor add Solid, Walk Over, Walk Behind, Climb and Back/Front layer
  arrows. Climb (2.5D) raises the spider's draw position only, leaving its
  logical position on the floor so steering/hunting/hit-testing are untouched;
  a contact shadow follows the body under its feet. Offered only for kinds with
  a climb height.
- **Root menu page order is deliberate.** Page 0 is the four things a player
  reaches for while looking at the tank -- Decor, Treats, Backgrounds, Borders --
  and the last page is the reference material: Photo Album, Help Guide, Settings.
  The middle pages hold the progress screens, whose order does not matter. With a
  page size of four and fifteen rows the last page holds three, so indices 12..14
  must be the reference rows. `ui_menu_set_item_icon()` indices must be updated
  with the array, and `site/lib/device-ui.js` mirrors the same order.
  Two climb families exist. The 53 small props (64x64, drawn at radius*16)
  snap to their full climb height anywhere inside the footprint. The six large
  structures (decor kinds 68..73, authored 96x96, drawn at LVGL scale 512 which
  is 2x) are a SLOPE that runs up the screen: the spider enters at the bottom
  edge of the footprint and rises as it walks toward the top edge, so height is
  a function of vertical position only. Ramping radially from the centre made
  the spider rise from whichever side it approached, which read as being lifted
  onto a platform rather than climbing a branch. Their ownership lives in
  `climb_owned`, not `catalogue_owned_high`.
  The lift alone does not sell the climb, and three attempts at a pose did not
  either. Forward-axis compression, a body tilt from the surface gradient, and a
  full dorsal projection into a side view were all tried and all looked wrong on
  the panel: a 96x96 sprite authored top-down cannot be re-projected into a
  convincing profile by moving its parts. The perspective system was deleted
  rather than left dormant so it cannot be switched back on by accident. What
  remains is the lift plus a contact shadow UNDER THE FEET. The shadow must
  follow the body: pinning it to the logical floor point put it up to 84 px
  below the spider at full lift, nearly a whole sprite height, which read as
  floating above the structure. A contact shadow belongs where the object
  touches its surface, and while climbing that surface is the top of the
  structure. The height cue is the spider's screen position over the artwork,
  not a gap to a shadow left behind. `decor_climb_slope()` still reports the
  ramp gradient, but only the debug console uses it.
  The hit test must follow the ARTWORK, not a circle. A large structure's sprite
  is `DECOR_CLIMB_SPRITE` square drawn at LVGL scale 512 (2x), so its screen
  half-extent is `DECOR_CLIMB_EXTENT` (96); its `radius` is only 13..18.
  Measuring the ramp against `radius` confined the whole climb to a 36 px circle
  in the middle of a 192 px sprite, so the spider rose and fell while still well
  inside the artwork. Measuring against the square box instead over-corrected:
  the sprites are diagonal logs and arches, so the box corners are empty and the
  spider counted as standing on the item while visibly above it, with the
  contact shadow far away. `main/climb_mask.h` is a per-row opaque span table
  generated from the installed PNGs by `tools/climb_mask.py`, and
  `climb_on_art()` asks the real question: is this point on the artwork? Rows
  with small gaps (between two branches of a vine) are recorded as one span,
  because a spider in a gap is still on the structure.
  Two details of that mapping are easy to get wrong. LVGL scales about the image
  centre, so screen maps to source as `sx = (x - item->x) / 2 + 48`, NOT
  `(x - item->x + 48) / 2`. And there is no circular pre-check for structures:
  the box corners sit at `r*sqrt(2)`, so a circle of radius `r` clipped the ends
  of the diagonal logs and the top of the driftwood reported no lift at all. The
  ramp `t` is measured against the artwork's own opaque row range
  (`top_row`..`bot_row`), and the horizontal fade uses each row's own span
  midpoint and half-width, so both follow the diagonal rather than the square.
  `tools/climb_mask.py --check` fails when the table is stale; the guard runs
  from `tools/test_art_style.py`. Regenerate after changing the climb art.
  The same circle-versus-artwork mistake was in `decor_blocks_point()`: it
  tested the circle first and only then the mask, so the ends of the logs (which
  lie outside a radius-96 circle) were walkable. The mask now replaces the
  circular test for a structure on Walk Behind. `blockat X Y` samples the
  blocking test directly; `climbat X Y` samples the lift. They are different
  questions, because Walk Behind blocks the whole artwork while Climb lifts only
  along the ramp.
  Their artwork is generated by nano banana through `tools/climb_prompts.py`
  and installed by `tools/import_climb.py`; `gen_sprites.py` preserves the
  installed PNGs and `climb_art.py` is only a fallback. See ART_STYLE.md. The
  prompts carry `SIMPLIFY_RULE` and `SURFACE_RULE` because the first set came
  back photographic; keep materials to three or four flat tones and the
  walkable top a calm band of flat colour.
  A small prop is PERCHED on, not walked across: `SPIDER_PERCH` holds the
  item's centre and turns the spider to face out from it, snapped to 45-degree
  steps. The pose is what sells the height, so the renderer also compresses the
  body's forward axis (`PERCH_FORESHORTEN` 0.72) while perched. The centre is
  derived from the item every tick via `decor_perch_center()`, never cached by
  the caller: a forced state change or a save load that restores SPIDER_PERCH
  never runs the idle branch that would have cached it, and a stale centre
  makes the spider drift to a point that is not on the prop. Hunger below
  `PERCH_HUNGER_LEAVE` (40), exhaustion and sickness all end the perch, because
  a cosmetic pose must never compete with feeding.
  The perch trigger runs in BOTH `SPIDER_IDLE` and `SPIDER_WANDER`. An
  IDLE-only check meant a wandering spider crossed a prop in well under a
  second and the player never saw a perch. The footprint also has a floor:
  `PERCH_MIN_RADIUS` (20) because a prop's `radius` is its collision disc,
  which for small items is smaller than the spider's own body (the Book Stack
  is radius 12 against a ~26px body), so the raw radius gave a window the
  spider crossed in a fraction of a tick.
  Both triggers go through `perch_wanted()`, which is the only place the
  guards live. The IDLE trigger was originally unguarded, so the moment PERCH
  left because hunger dropped below `PERCH_HUNGER_LEAVE`, IDLE re-perched it on
  the same tick and the two states alternated at the 20 Hz logic rate; the
  visible symptom was a spider sitting on the prop and vibrating, because the
  foreshortening eased in and out on every flip. `PERCH_COOLDOWN_MS` (4000)
  additionally holds the spider off a prop after it leaves, so leaving means
  leaving for a moment instead of re-perching on the next tick. A forced
  `SPIDER_PERCH` (console `state 10`, or a save load) starts a fresh
  `s_perch_until` window, because it never runs the trigger that sets it.
  The spider canvas is 96x160, not 96x96: `s_draw_dy` raises the body inside
  the buffer, so a 62px lift on a 96px canvas clipped the body off the top.
  `CANVAS_CX`/`CANVAS_CY` keep the body and its contact shadow inside the
  buffer with 62px of transparent headroom. Do not shrink it back.
  Stage position/path/layer until Done; Cancel discards all. Floor is beneath
  the spider; Canopy (Walk Behind) is above the creature layer. Arrows reorder
  within a band. The gift carries the layer control but NOT the path control:
  it is always walked over (save v20 `gift_path` is kept for layout only and is
  always 0, v22 `gift_layer`). The gift is parented DIRECTLY into the decor
  band it belongs to (`s_gift_obj`), not into a container of its own: a separate
  gift layer made the layer control meaningless, because
  `lv_obj_move_to_index()` only reorders siblings, so a gift in its own layer
  could never be interleaved with the decor it was meant to sit behind. The
  target index must also account for where the gift already is, or the move
  oscillates every tick. The layer row is hidden when the band holds nothing but
  the gift, since "1 / 1" is not a choice. All exits stay in
  the enclosure, with no Back-to-Settings route.
  The gift NEVER blocks or pushes the spider. It used to offer Walk Over and
  Solid; Solid bought nothing visible (a single 64x64 object has no depth, so
  "the spider goes around it" read as an invisible wall around a flat sprite)
  and cost a fragile coupling: Solid pushed the spider out to `GIFT_PUSH_RADIUS`
  while the visit only counted as arrived inside `GIFT_VISIT_ARRIVE`, so the two
  constants had to stay in the right order or a visit would shove the spider out
  every tick and circle forever. Removing the choice removed the coupling --
  there is no push radius, no arrival threshold to keep in sync, and no gift
  avoidance steering at all. `gift_blocks_movement()` is gone from behaviour.c
  and returns false in device-engine.js; `separate_from_gift()` and
  `gift_avoidance()` are deleted. `behaviour_gift_block_test()` now asserts the
  stronger invariant: no path value displaces the spider, dead centre included.
  `save.c` folds any stored `gift_path` to Walk Over, and `gift_canopy_now()` in
    terrarium.c is deleted outright -- `gift_band()` returns the floor layer, so
    the gift is always drawn there and there is no canopy case left to handle.
  Decor keeps Walk Behind, and it blocks the same way. `decor_path_blocks()` is
  true for Solid and Walk Behind; `decor_blocks_point()` and
  `decor_nearest_blocking()` use it, and `behaviour.c`'s `decor_avoidance()`
  measures from `decor_block_radius()` rather than a hardcoded 24. The radius
  differs by path: Solid blocks at `radius`, Walk Behind blocks at the artwork
  extent (`decor_art_extent()` -- `2*radius` for a 64x64 prop drawn at
  `radius*16`, `DECOR_CLIMB_EXTENT` for a structure), because the spider has to
  be clear of the whole sprite before it can legitimately appear in front of it.
  For a structure the extent is additionally gated on `climb_on_art()`, so the
  empty corners of a diagonal log do not block. That gate REPLACES the circular
  test rather than supplementing it: the box corners sit at `r*sqrt(2)`, so a
  circle of radius `DECOR_CLIMB_EXTENT` clipped the ends of the logs and the
  spider could walk through opaque artwork. `blockat X Y` samples the blocking
  test directly, which is how that was found.
  Save v13 keeps the v12 layout, storing path0/1/2 in the existing byte and
  layer order in record order. Only path1 collides. Read old v2-v12 saves,
  reject path2 before v13/path3 always; do not downgrade a v13 save to old
  firmware. Renderer objects are retained/reparented only on explicit edits;
  no per-drag allocation. Pointer self-tests use lightweight decor checks,
  not repeated full terrain decodes that can starve the LVGL watchdog.
  The art pipeline validates eleven dedicated badges plus nine explicitly
  shared UI icons against native/browser mappings; every achievement bit must
  be covered. Use the complete PNG-only pipeline, not just its unit tests.

- Current working changes include twenty-three help topics in the native/browser game
  and website, simplify WiFi, move Firmware Update into About, center grid art,
  enlarge/inset slider targets, gate affordability and fix the missing native
  Borders route. Keep Wipe Spider last in Settings. Achievements now show all
  twenty goals, including First Pet, and pay 15 XP +20 Silk exactly once using
  the existing uint32 bits. Molting was reachable but its state never exited;
  it now completes after 800ms. Moon/Rain use distinct plucked/mallet scores.
  Audio's bounded PSRAM producer runs at priority7 above LVGL6; inspect its
  explicit refill counters, not just missing crash logs. Source changes are
  not a new public release; use the newest progress evidence for validation.

- v1.1.26 keeps single-slot sliders, explicitly captures held browser pointer
  drags (including rotation/scale and cancellation), and removes submenu
  chevrons from both UIs. Keep selected checkmarks and footer paging arrows.
  Native UI tests inject actual held/moving pointer input, not only synthetic
  VALUE_CHANGED events. Spider/art/save format and OTA limitations are unchanged.

- v1.1.25 retries transient manifest reads and resumes stalled application
  downloads only after verifying HTTP206 Content-Range start/end/total, with
  at most16 reconnects inside a five-minute deadline. Full size/hash/project/
  version/art checks remain mandatory before boot selection. Parser/range
  diagnostics pass and failed real transfers preserve exact pet bytes, stop/
  deinit WiFi and recover the memory floor. Full hosted OTA boot is not yet
  proven: transfers stalled on the current link. Do not equate manifest fetch,
  a partial download or the ordinary USB flash with OTA success.

- v1.1.24 restores the website hero spider with one small beetle on plain moss.
  All browse/navigation menus are grids: six catalogue tiles, including
  Achievements/Prey/Borders, or four wider settings/control tiles. Keep full
  gift effects in the explicit detail preview. Slider callbacks use stable
  item indices, not mutable percentages; anchors must be TOP_LEFT in tiles.
  Web updater defaults to update mode: reject a mismatched installed table,
  write app/art/OTA selection last, compare all NVS bytes afterward and clear
  in-memory snapshots. Full installation remains available for migration.
  Native latest OTA follows GitHub's latest stable descriptor, validates exact
  application hash/size/project/version and current art hash before boot selection.
  Publish `spidarium-ota.json` and `spidarium.bin` alongside every release ZIP.
  Art differences require USB update; no signed firmware/functional rollback.
  See [OTA policy](WIFI_ARCHITECTURE.md) and latest progress evidence.

- v1.1.23 uses six-tile (3x2) grids for Treats/Backgrounds/Borders/Decor/Gifts;
  other menus remain three-card lists. Use explicit `ui_menu_open_grid()`.
  Reuse at most six tile objects per open, never create objects per frame.
  Gifts have individual thumbnails and a full-effect preview; Back restores
  the grid page without spending Silk. The care entry is a 34px folded corner
  in a transparent 56x56 target at12/12, native and browser; browser M still
  works and exposes expanded state. Website hero is a static woodland beetle
  over Fairy Glade; both spider renderers and all 337 PNGs remain unchanged.
  Save v12 and catalogue IDs/prices are unchanged. See
  [release evidence](RELEASE_v1.1.23.md) and the latest progress round.

- v1.1.22 doubles to 68 decor, 12 treats, 36 gifts and 48 backgrounds; all old
  IDs/art remain unchanged. Borders offers 48 independently purchased matching
  frames plus free None; owned frames reapply free with any floor.
  Save v12: 268-byte prefix, 52 compiled fields, six-byte decor tail now at 268.
  v10/v11 tails still read at 260. Former padding 245 packs gift IDs32..35 in
  its low nibble and decor IDs64..67 high; u16 at246 stores background32..47.
  Decor32..63 uses the full existing extra word. Border words at260/264 and
  selection266; 255=None. Earlier saves ignore old padding and start frameless.
  Runtime keeps 48px menu thumbnails, not all 96 full scenes/frames: one reusable
  172,800-byte PSRAM QOI workspace decodes on explicit rebuild/test, composites
  frame into the existing floor canvas. No per-frame allocations/worker LVGL.
  `background 0..47`, `border 0..47|none`, `open borders` use real shop rules.

- v1.1.21 adds three selectable original scores and independent music gain.
  Sound page 2 holds Music Volume and Soundtrack. Master volume/mute still win;
  effect volume is not reduced by music gain. Save v11 uses former padding
  bytes 254/255 for music volume/track; fixed prefix 260 and decor tail unchanged.
  Older saves migrate to volume 100/track 0, with 47 compiled schema fields.
  SD menus retain a third result/status row. Screenshot capture keeps one LVGL
  snapshot alive while the persistence worker reads it, then destroys it on
  LVGL after SD completion; never allocate a second 460,800-byte copy.
  Browser feedback appears inside the game/fullscreen, with honest download
  request wording and browser-file/device-card distinctions.

- v1.1.19 [expanded habitat and music](RELEASE_v1.1.19.md): six treats,
  thirty-four decor kinds and twenty-four
  backgrounds, including a house, dark cave and night scene. Preserve old IDs
  and all approved spider art. Contextual long press replaces redundant move
  entries. Double tap dims just the enclosure, not menus or screen brightness.
  Original ambient music has an independent Sound switch and master mute still
  wins; the pet effect is a soft consonant bell/purr rather than a sweep.
  Save v10 keeps old offsets, appends music/lights/extra ownership and a count,
  then six-byte extra decor records. Fixed prefix 260 bytes, raw 272 plus tail;
  45 compiled fields. Never truncate tails in console/browser/SD backups.
  Extra records, snapshots and console lines are PSRAM-backed; only the internal
  save worker touches flash. Opaque background decoding now uses RGB565 rather
  than RGB565A8, keeping all 24 sources at the previous 16-source memory cost.

- v1.1.18: [habitat controls, prey gait and RTC sleep](RELEASE_v1.1.18.md).
  Automatic ambience is disabled, superseding v1.1.17 scheduling. Keep foreground
  effects and manual sound diagnostics. Decor/gifts have explicit movement
  menus plus long press; closed backdrops hide before deferred deletion so they
  cannot intercept the next pointer event. Menus use framed icons and a short
  gold ornament at x120, clear of Back. Background purchases/ownership and power
  preferences share save v9 (248-byte payload, 260-byte raw, 41 compiled fields);
  v8 migrates the equipped old NVS background plus free cork without charging.
  Update the compiled schema, browser decoder and raw console capacity together.
  RTC sleep snapshots on LVGL, commits on the internal save worker, drains
  workers and pauses the display before sleeping; no worker calls LVGL.
  Timer/right GPIO18 wake only, not touch. Automatic sleep is opt-in and battery
  only. See [battery policy](BATTERY_MANAGEMENT.md) for unmeasured boundaries.
  Living prey now have fixed thoracic leg roots, alternating tripod feet and
  direction-aware image rotation, with ground gait driven by travel rather
  than a clock while stopped. Image rotation works without LV_USE_MATRIX;
  do not change the approved spider or enable that unrelated option.

- v1.1.17: [quiet environmental audio](BROWSER_PARITY.md#v1117-quiet-environmental-audio).
  Replace intermittent pitch sweeps with slow air/leaf textures, not louder
  melodic effects. Native/browser seeded PCM matches `audiotest`; the short pet
  chirp is explicit, never automatically scheduled. Stream through the existing
  audio worker/PSRAM buffer; long ambience must fade promptly for queued
  foreground sound, mute and radio drains. Browser mute/volume applies while
  menus are open. See the latest progress round for measured evidence; speaker
  listening approval is not inferred from waveform or codec-write checks.

- v1.1.16: [complete SNES art/chrome release](RELEASE_v1.1.16.md).
  All 119 non-spider assets now share the [style contract](ART_STYLE.md):
  quiet terrain, shaded decor/gifts, material-lit prey, splatters, icons and
  badges. Both spider renderers and all 39 exports remain unchanged.
  Native/browser/website chrome uses flat navy/mint/gold, two-pixel stepped
  corners, hard borders and highlights, not gradients or blurred shadows.
  Keep touch geometry, keyboard coverage, readable Montserrat and distinct
  disabled/selected/destructive/focus states. The generator is authoritative;
  ordinary validation now protects **all 158 source PNGs**, superseding the
  historical 84-image preservation subset. Generate an approved redesign
  before validation. `asset_build.py --png-only --preview PATH.png` reviews
  every non-spider asset; CI checks art before website publication.

- v1.1.1: [HUD and screenshot update](RELEASE_v1.1.1.md). Battery chrome is
  transparent, HUD-only and safely inset; browser ear has an explicit origin
  and square viewport. Promotional fangs are mint, not player colors.
  Settings > Save Screenshot and `screenshot sd` share playfield capture.
  LVGL temporarily hides/restores HUD and menu objects within one callback,
  copies RGB565 into PSRAM and destroys the LVGL buffer on its own task.
  The serialized save worker owns the copied pixels after successful enqueue,
  writes a numbered RGB565 BMP with exclusive creation, flush/fsync and exact
  readback, then frees pixels. Failure leaves UI restored and reports an error;
  no card is formatted. `screenshot menu` includes the current menu;
  `screenshot playfield` excludes UI, and `capturetest` compares restoration.
  See [serial diagnostics](serial_console.md#screen-capture).

- v1.1.0: [browser parity and measured evidence](BROWSER_PARITY.md).
  Native-coordinate browser HUD/setup/menus/minigames, fullscreen, local
  progress migration with explicit errors, and separate default-off Auto Feed/
  Auto Hunt checkboxes on both platforms. The native numeric FSM maps through
  `STATE_NAMES` for rendering. Never introduce fake PMIC/WiFi/SD responses.
  Save schema v3 retains the 132-byte payload, appending checkboxes in old
  padding at 129/130; v2 padding must migrate OFF. The actual compiled schema
  contains 24 fields and `minVersion:3` for both flags. Future browser/schema
  changes require real capture and exact restore tests, not guessed offsets.
  Root LICENSE permits personal, non-commercial use; retain all third-party
  notices and do not describe it as OSI open source.
- v1.1.2 (Round 46): save schema v4 appends `volume` at offset 131, which still
  fits the existing tail padding, so the payload stays 132 bytes and the legacy
  120-byte payload is unchanged. The compiled schema now has 25 fields with
  `minVersion:4` for `volume`. Settings is WiFi, SD Card, About, Battery,
  Display, Sound, Auto Feed, Save Screenshot, Wipe Spider (destructive, last).
  Auto Feed is one switch driving both `auto_feed` and `auto_hunt`. Sound and
  Display use `ui_menu_set_item_slider()`; Battery shows a heuristic runtime
  estimate and Display holds the Screen Rotation control. Rotation is an LVGL
  transform on a full-size wrapper holding the whole UI, because the adapter
  never re-reads MADCTL after init and `bsp_display_rotation_set()` had no
  visible effect. Every layer — playfield, HUD, sleep overlay, menu panel and
  backdrop — must be parented to `power_rotation_layer()`; anything parented
  straight to `lv_screen_active()` stays upright while the rest turns. The panel
  is square, so taps still map to the same coordinates. Do not claim a measured
  runtime without hardware evidence.
- First release: [v1.0.0 notes](RELEASE_v1.0.0.md),
  [measured verification](RELEASE_VERIFICATION.md) and [actual screenshots](README.md).
  Website source lives in `site/`; Pages consumes the official, hash-verified
  five-image ZIP rather than local firmware or saves. Native browser anatomy
  follows `cspider.c`; generic Spider is not a replacement name for the user's pet.
  Browser simulation is approximate, not an ESP32 emulator.
  Release code is `0f16802`; live publication evidence is in the verification
  guide. The `github-pages` environment permits `main` and the explicitly
  approved `v1.0.0` tag. Approve the next official tag without removing existing
  protections, and update version-specific package/website validation together.
- Round 43 adds three-prey Feed guards, shared silk-bordered cards, readable
  translucent identity panel, corner battery gauge/details, Sound in Settings,
  gray/inert pagination and distinct achievement assets. There are 158 PNGs,
  including eleven badges, eight species-specific splatters, sixteen ground
  textures and sixteen placeable decor sprites. Preserve the 84 accepted
  non-spider images. The 39 spider PNGs are a generated export of
  `main/cspider.c` and regenerate with it; they are not live anatomy and not
  preservation-protected.
- Browser save tooling uses the compiled schema and validated raw export/restore.
  Restore pauses the tank and blocks subsequent saves until explicit reboot;
  never resave the old live state over a restored blob. Screenshot capture is
  marshalled to LVGL, with immutable bytes transferred/freed by acknowledgement.
  Bulk protocol uses the existing USB-JTAG driver with TX drain checks, not
  ordinary large VFS printf writes or a second UART driver.

- [Menus](MENU_ARCHITECTURE.md): three taller cards per page, larger navigation,
  red Wipe Spider (last row) under Settings, top-left page ear/aligned status bars and unobstructed drag hunting,  QWERTY keyboard with Show/Hide WiFi password,
  shared first-run name editor, manual SSID, saved networks, OTA URL,
  About, battery controls and prey selection. No flash writes or SD mounting in menu callbacks.
- Play chooses Bug Hunt, Color Match or Quick Tap without immediate repeats.
  Eight ramped rounds pay 8-32 base XP and 2-18 Silk plus care/achievement effects
  only on completion; Back cancels and deletes its LVGL timer. Score tests use
  mock rewards. Bug sprites shift, Color Match ramps from one to three
  remembered colors in order and Quick
  Tap has orange/red feints. Hunt accuracy XP is 18/10 plus 5/3 Silk.
  Care shows current XP and next gift/prey unlock.
- [Networking](WIFI_ARCHITECTURE.md): three saved networks; one copied job at
  a time; bounded scan/join/SNTP/HTTPS OTA; stop and deinit on every exit.
- RTC: PCF85063A on BSP I2C, UTC restoration at boot and verified calendar
  writes after player-requested SNTP. Boot primes network allocations with
  init/stop/deinit before LCD creation, but never associates with a saved
  network or runs SNTP automatically.
- [Gifts](GIFTS_CATALOG.md): eighteen larger habitat items, earned Silk wallet,
  one-time purchase/owned placement, unlocks through level 40 and spider visits.
  `gifttest` checks bonuses, economy and visit rewards without persisting tests.
  Keep IDs/filenames stable. The v2 payload gains appended Silk/ownership/
  brightness fields; old pet payloads and SD backups migrate rather than reset.
- [Battery](BATTERY_MANAGEMENT.md): AXP2101 worker/atomic telemetry, stored
  brightness, 60s dim/300s screen-off, touch/serial wake and low-battery save.
  Power/network display ownership is serialized. Charger settings are untouched;
  USB telemetry is not proof of unplugged discharge/runtime.
- [Artwork](ART_STYLE.md): deterministic non-spider assets in a **16-bit
  SNES-era** direction — crescent shading, three-to-five tones per material, a
  hard terminator, bounce light, an ink rim on every silhouette, no gradients.
  Eight prey species, eighteen gifts, sixteen placeable decor sprites and
  thirteen HUD icons. [Inventory](ASSETS_INVENTORY.md) and
  [asset commands](../assets/README.md) define the maintenance contract. The
  spider is drawn procedurally by `main/cspider.c` (ported in
  `site/lib/spider-renderer.js`); the 39 `spider_*.png` are a generated export of
  that renderer, not preserved art. Sickness dulls the palette rather than
  applying canvas opacity, so the spider stays opaque against pale floors. The
  terrain/decor redraw replaces smooth noise with crisp edge material clusters
  and quiet three-tone centers; placeable props and all eighteen gift
  furnishings use 8-12 tones. Gift furniture is part of the decor art scope,
  not an unchanged icon set. The spider and
  its 39 exports are byte-identical to their pre-redesign state.
  [ART_HANDOFF_SNES.md](ART_HANDOFF_SNES.md) is the current maintenance brief,
  not the superseded proposal to add hundreds of interpolated colors.
- Sixteen selectable habitats use one reusable 460,800-byte PSRAM canvas, not a
  web/prop object tree. Backgrounds are **ground texture only** — substrate,
  trail, grain, tonal drift — with no painted props, so the spider always has a
  clean floor behind it. Sixteen authored 240x240 packed background assets add
  2,764,800 bytes of explicit decoded PSRAM, scaled 2x into the existing canvas
  at selection. Preference IDs remain stable in a separate NVS key, written by
  the persistence worker.
- [Decor](ART_STYLE.md): sixteen kinds bought with Silk (12-68) and unlocked from
  level 1 to 9, placed by tapping the tank into twelve slots. Each is blocking or
  non-blocking. Blocking is a **steering input**, not a positional shove:
  `decor_avoidance()` in `behaviour.c` measures the berth from the footprint edge
  and only steers when the item is ahead, and `advance()` refuses a step that
  would land inside a blocking footprint. Non-blocking decor is ignored entirely.
  The catalogue order is shared with the browser and checked by a test that
  parses the C table. Save version 8; `decor_owned` at offset 160, `decor` at
  164, `game_state_t` 236 bytes. Placement mode lives in file-scope globals, not
  in the persisted struct.
- Prey IDs 0..3 and levels 1/2/4/6 are preserved. New roach/mealworm/fruitfly/
  waxworm unlock at 10/13/16/20. All explicit/wild/daily spawns obey eligibility;
  never reset the user's level merely to demonstrate locks.
- Drag direction is toward the finger, not the inverted legacy comments.
  Held gestures freeze AI/prey; stationary taps do not shoot. Pounce landing
  synchronizes subpixel position. Canvas gait is distance-driven with bounded
  local joint poses; duplicate renders cannot advance it.
- Care now uses a translucent page ear at (-1,-1), with a transparent
  44px touch target and four status boxes centered as a group at y=8.
  No word label or full-hunger press bubble. Tap the ear again to close;
  X is shown only for gift cancellation, not when the HUD is open.
- Round 30 darkens all enclosure floors (central RGB channels <=96, validated),
  replaces grid webs with small anchored silk sleeping sacs, and restores
  clearly bent/highlighted knee joints while preserving distance-driven gait.
  Build/full flash, 39 console checks, all eight scene diagnostics, packed art
  verification and 40-second stability soak passed. See latest progress.
- Round 31 (superseded by Round 32) replaced arbitrary knees with fixed 18/20-pixel two-link geometry,
  mirrored headward front and abdomenward rear bend branches. Signed direction,
  link-length and outward-order checks caught an inward sad/sleep knee; reduced
  contraction corrects it without weakening assertions. The dot is 2.5x larger
  and moved left along the usable circular edge. Final build/full COM7 flash,
  39/39 checks, zero render/scan drift and 40-second stability passed.
- Round 32 restores the original angular fan legs, radius-16 hips, 12.76/15.84
  segments and original pair phase offsets. Keep distance-based gait timing;
  bound original emote angles to prevent knee inversion. Golden-coordinate
  checks prove the neutral shape matches the original code, not the rejected
  IK design. Remove the circular-safe-corner assumption: dot center (30,30),
  status row y=8, level strip y=60. Build/full COM7 flash, 39/39 checks,
  zero scan/render drift and 40-second stability PASS; physical taste remains
  the user's acceptance check.
- The latest HUD revision supersedes Round 32's dot: a triangular page fold
  facing inward (lower-right half), lighter navy 60%-opacity face/shadow and
  a muted 20%-opacity crease.
  A light X appears only to cancel a gift. Preserve
  centered stat boxes and the original spider anatomy.

## Ownership

Latest care/menu revision: explicit Back only (blank taps are absorbed),
no disabled dashes, icon-bearing achievements, concise WiFi rows and
capitalized built-in text. Creature objects are grouped below the HUD.
Fullness decay is 3/minute, with temperament/gift modifiers; the >=95 gate
remains. Three quiet ambient effects use the existing audio worker and mute.
SD backup requires `CONFIG_FATFS_LFN_HEAP=y` and `CONFIG_FATFS_MAX_LFN=64` in
live config/defaults; the previous short-name configuration caused fopen to
reject `spidarium-save.bin`. Backup snapshots/commits/writes/verifies on the
save worker, with status returned to the UI.

`game.c` owns live gameplay and menu state on the LVGL adapter task. Logic
(20 Hz) and rendering (30 Hz) are timers on that task, not separate tasks.
`net.c` owns a 12 KB internal-stack worker. `save.c` owns a four-entry copied-job
queue and 4 KB internal-stack worker; SD jobs use that same queue and publish
copied restored state. Queue/stack allocation happens at boot before LVGL starts,
not in a save callback. Console save acknowledgements propagate queue rejection.
Flash writes require internal stacks even with XIP
enabled: a real NVS write proved the cache-disabled assertion on PSRAM stacks.
Audio and USB-JTAG console workers have explicit PSRAM stacks; synthesized PCM
uses a reusable 12,800-byte PSRAM buffer. Workers never call `lv_*`.
Modals skip gameplay/tank work while keeping the service timer/polling alive.
HUD labels only invalidate on actual changes at at most 4 Hz.

Networking pauses the whole adapter with acknowledgement and drains active
persistence/audio before starting the radio. Touch and composition temporarily stop;
the last frame stays visible. Pending saves wait until networking finishes.
Pending audio also waits. Main LVGL stack uses `stack_in_psram`; its internal draw workers do not.
The adapter's optional draw-worker patch hook does not match LVGL 9.6 and must
remain disabled. Do not modify managed dependencies to bypass that check.

## Build and migration

Always use wrappers, redirecting build output:

```powershell
powershell -File build.ps1 *> build_out.txt 2>&1
powershell -File flash.ps1 COM7
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' run_tests.py COM7 results.txt --gameplay
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' stability_check.py COM7 40
```

Asset packing precedes the main component build; generated identifiers must
exist before compiling consumers. Do not call `idf.py` directly.
Keep active `sdkconfig` and defaults aligned. See [memory budget](memory_budget.md).
Prefer `--gameplay` for an existing pet: the default suite intentionally
performs care/save/state changes. New hunt/progression diagnostics restore
synthetic fields before the LVGL callback returns, so saves cannot capture them.

The new table retains NVS/PHY offsets and adds OTA metadata, a 3 MB factory app,
two 3 MB OTA slots and 6 MB asset storage. Initial migration requires the full
flash wrapper (bootloader, table, app, assets), not an application-only OTA.
Do not erase NVS: it holds the user's pet and credentials.

## Verification boundaries

Round 42: final wrapper build/full flash exit 0 with zero warnings,
**39/39 non-mutating console checks**, all three games scoring 7/8 with mock
rewards, gift economy/visit tests, battery policy/wake tests and exact gift
text/pixel geometry PASS. `render 500` and `scanloop 5` have zero largest-block
drift; the final 40-second soak has zero faults. Final suite largest is 63,488
bytes, above the 51,200 gate.

Actual uninterrupted inactivity capture verified brightness 15 at one minute,
0/screen sleeping after five minutes, and serial wake restoring 50%. AXP2101
reports 100% and approximately 4,150mV on USB. A fresh SD backup wrote/read-verified
144 bytes including the appended wallet, ownership and brightness fields.
All 123 packed artwork entries match PNG pixels; 97 creature/icon/prop images
are preserved while the 18 furniture gifts are intentionally redesigned.
No unplugged runtime, physical low-battery or charging-transition claim is made.
See [battery limits](BATTERY_MANAGEMENT.md) and [latest progress](../PROGRESS.md).

Round 29 supersedes Round 28's persistent HUD and procedural landscape art.
Final build/full flash exited 0 with zero main warnings. **39/39 non-mutating
console cases** passed, including injected LVGL press/move/release and catch,
aligned icon/status bars and Gift cancellation, gait cadence/geometry, and all
eight prey unlock boundaries. `render 500` and quiescent `scanloop 5` had zero
drift; 40-second soak passed with zero faults. Minimum observed DMA largest:
63,488 bytes. All eight authored scene IDs survived independent resets and
matched packed source pixels; the user's original leaf-litter selection was
restored. All 123 packed entries matched decoded PNG pixels; all 115 earlier
PNGs remained byte-identical. See [latest progress](../PROGRESS.md).
The art preview was inspected, but the user still decides aesthetic acceptance.
Synthetic diagnostics restore pet/prey fields; normal live auto-hunting may
advance the existing pet during the time hardware runs.

Round 28 on COM7: final build/flash exit 0, zero main warnings, **44/44 console
checks**, two successful real-callback `uitest` runs, and 40-second soak
`VERDICT: PASS` with zero faults. Final `render 500` and `scanloop 5` reported
zero largest-block drift. Minimum observed internal/DMA largest and standalone
post-reset memory check: **63,488 bytes**; suite-end/soak snapshots: 81,920.
First scan infrastructure can consume a one-time block; subsequent scans are stable.
All eight background choices saved and survived independent resets; the original
choice was restored. All 95 packed QOI entries match their PNG pixels; the 56
non-spider assets enforce 2x2 blocks, binary alpha and limited palettes, with
deterministic regeneration preserving all 39 spider PNGs.
Earlier Round 27 RTC/SD evidence remains applicable:
Manual host-UTC RTC write/readback and restoration across reset passed.
The actual missing-card SD probe ran on its worker while menu commands remained
responsive; expected driver errors were confined to that explicit probe.

Console tests are actual UI/game code-path tests, not proof of visual quality
or physical touchscreen ergonomics. Gift computations are not proof of every
random catch outcome. See [serial reference](serial_console.md).

Positive saved-network association, fresh SNTP/boot synchronization and OTA
installation require authorized credentials and a hosted, uniquely versioned
Spidarium HTTPS image. Never join an arbitrary discovered open AP. RTC retention
while powered off requires physical backup-power testing; a reset is not proof.

OTA checks HTTPS certificate/hostname, matching project and image completion.
It has no release manifest, semantic downgrade policy, provisioned signing,
automatic reboot or application-functional rollback. Credentials use ordinary
NVS, not encrypted flash, and must never be logged or committed.
Application-only OTA does not replace artwork. Asset/identifier changes require
a matching full flash until a separate asset-update mechanism exists.

## Preserve and extend

- Allocate CPU-only buffers once in PSRAM; no per-frame allocation.
- Publish worker completion only after results are ready and busy is cleared.
- Queue copied state, not pointers to live LVGL objects or mutable game state.
- Report rejection/failure explicitly; test failure cleanup and radio-off.
- Compare internal/DMA `largest` before and after repeated operations.
- Preserve dirty user changes. Commit only when requested; never erase NVS or
  flash-wide storage for an artwork or menu change.
- Append each round to [PROGRESS](../PROGRESS.md), including Decisions made and
  honest build/flash/hardware evidence. Do not label blocked checks as passed.
