# Optional companion life systems

These features are in v1.1.31/save v18. Preserve the approved spider artwork
and existing pet.

## Expression and training

Healthy, content, idle spiders periodically greet you using the existing
petting reaction. Bond and temperament change the message and frequency.
Nothing interrupts sleeping, illness, hunting or molting.

Menu > Training offers Wave, Spin and Bow. Five practices learn each trick.
Practice requires a healthy, awake spider with at least 20 energy, costs 2
energy and has a 30-active-second shared rest. Menus do not advance that rest.
Perform a learned trick for fun; it does not mint Silk or XP. The bounded
animations reuse the approved bounce, heading and crouch, not replacement art.

## Clock, activities and seasons

Menu > Calendar shows the reliable saved clock. The device uses its RTC and
the browser its clock. Existing internal calendar time remains the basis;
this feature does not add a location/timezone or hemisphere setting.
Morning is 06:00-11:59, afternoon 12:00-17:59, evening 18:00-21:59 and night
22:00-05:59. Stretch, explore and quiet-time prompts are cosmetic; they never
force sleep or impose care penalties.

Day/night shading starts on and stays subtle. Gentle Weather starts off:
six low-opacity particles near the enclosure edges, never an online forecast
or a screen-obscuring storm. Seasons follow northern calendar quarters:
January-March winter, April-June spring, July-September summer,
October-December autumn. This is intentionally a simple offline calendar.

An occasional afternoon visitor offers 3 Silk once that day. A season offers
10 Silk once. Persistent high-water markers prevent replaying an old date
or quarter for rewards. A clock earlier than 2024 is unavailable: no invented
season, visitor or calendar payout. Clock sync still uses only an authorized,
saved WiFi network and tears down the radio afterward.

## Real photo album

Menu > Photo Album > Take Photo captures the actual 480x480 playfield with
menus and HUD hidden. Device photos need supported SD media, and use eight
fixed BMP slots. Work is queued to the existing internal-stack persistence
worker. Wait for its completed storage status; queue acceptance is not a
saved photo. Missing media, malformed files, full storage and write/readback
errors are explicit. Existing files, including damaged slots, are not evicted.

Select an occupied slot for View Photo or Delete Photo. Deletion requires
Confirm Delete; Cancel leaves the image untouched. The worker unlinks only
that slot and verifies absence before reporting success. Damaged slots remain
selectable for deletion. A completed deletion frees one of the eight slots;
no pet, other photo or backup is removed. Settings' old screenshot row is gone;
serial screenshot diagnostics remain available.
The Photo Album root offers Take Photo, Storage Status and Photo Storage.
Only saved images appear inside Photo Storage (or an empty/loading message).
Generations is independent and appears only in the main menu.

The browser stores up to eight actual canvas PNGs in IndexedDB. Storage
rejection is an error, not a localStorage metadata substitute. Export photos
before clearing browser storage. Photos are not embedded in raw/JSON pet
backups; copy the SD album files separately too. Browser images and device
files are independent albums.
Browser viewers offer the same explicit confirmation; deletion reports success
only after the IndexedDB read/write transaction commits. Rejection keeps the
photo and reports the error.

## Menu and lighting preferences

The root is ordered Your Spider, Today, Training, Calendar, Treats, Achievements,
Prey Species, Diary, Photo Album, Generations, Decor, Backgrounds, Borders,
Help Guide, Settings. Gameplay precedes customization and system tools.
Back retains the last page of each menu, including the root, Settings,
catalogues, diary and photo storage. Page history is session-only, not a save
field; changing a catalogue selection does not reset the page.
Settings > Display > Double-Tap Lights starts checked. Turning it off disables
only the enclosure gesture, not normal tapping or petting. Dim Enclosure always
allows manual lighting changes, even when the gesture is disabled. Neither
setting alters screen brightness or the calendar's independent shading.

## Audio transitions

The ES8311 hardware master gain stays fixed. The audio worker applies the
equivalent logarithmic master gain to existing PCM with 20 ms ramps, observing
control changes in chunks of at most 50 ms. This avoids register gain jumps
across already queued nonzero samples without adding a buffer or blocking LVGL.
Older short sweep/noise effects now taper their last5ms to exact zero rather
than switching a residual nonzero sample directly to idle DMA silence.
The approved music, pet bell and USB chime source waveforms remain unchanged.
Host transition tests and device refill telemetry cannot prove the absence of
audible crackle; physical listening is still required.

## Safe optional generations

Menu > Generations keeps
three archived pets. A healthy adult can retire only after a confirmation.
Back returns to the main menu. There is no generations shortcut in Photo Album.
The current pet's identity, care/progression, preferences, diary and learned
tricks are archived before raising a hatchling with inherited existing
colour presets. Full archives reject retirement; no automatic eviction.

Resume also requires confirmation. It swaps the active pet with that archive
slot, keeping both pets. Silk, habitat, inventory and global settings remain
shared, so swapping cannot duplicate money or purchases. Retired pets do not
decay. There is no automatic retirement, forced replacement or death.
Cancel leaves state untouched. Keep a backup before changing generations and
wait for the actual save completion before disconnecting power.

Save v16 appends a 2236-byte life state after the existing 860-byte prefix:
3096 fixed bytes, then the existing six-byte extra-decor records. The 23304-byte
maximum preserves 3368 extra records. Three 740-byte pet records and fixed
training/calendar fields keep snapshots bounded and pointer-free. Legacy
v2-v15 backups migrate with default new features and no fabricated ancestors.
Save v17 uses former life reserved byte15 for the global double-tap lighting
disable setting:0 keeps the gesture enabled (the default),1 disables it.
The prefix stays3096 bytes, life stays2236 bytes, archives still start at16,
and archive reserved bytes remain zero. Generation swaps preserve this setting.
v2-v16 backups migrate with the gesture enabled; malformed v16 nonzero reserved
bytes and invalid new flags are rejected. Legacy browser JSON `life.reserved:0`
normalizes to the new enabled default before validation.
Older firmware cannot read v18 saves. Keep the browser, schema and firmware
paired at v1.1.31.
