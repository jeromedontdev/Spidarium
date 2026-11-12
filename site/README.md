# Spidarium website

A static, vanilla ES-module website for the Waveshare
ESP32-S3-Touch-AMOLED-2.16. No accounts, telemetry, external font/CDN requests,
or cloud credential services.

## Visual style

The whole set uses SNES-style pixel artwork: quiet habitat centers, all decor
and gift furnishings, and matching flat menu chrome. Panels use `#16283c`, rows
`#24405a`, selected rows `#2b6a5f`, mint `#76d6b1` and gold `#b99b67` over deep
ink `#0a1220`. Opaque two-pixel borders, square/two-pixel corners and hard inset
highlights replace gradients, blur and soft shadows. The physical viewport
shell remains rounded; spider and terrain pixels have no CSS filter effects.
The existing local Montserrat font is shared with the landing page.
v1.1.18 adds framed row icons, pixel rivets and a gold header ornament clear
of Back. Daily actions use care glyphs; backgrounds use real thumbnails.
Treats, Backgrounds, Borders, Decor, Gifts, Achievements and Prey Species use
six-item 3x2 grids; navigation/settings use four-item 2x2 grids. Gifts open a full-effect preview
before placement, with Back restoring the same shop page without a purchase.
The care HUD opens from a small paper/gold fold with a transparent 56px target
inside the top-left corner; M is the keyboard equivalent.
The landing-page circle shows the unchanged native spider with one small
beetle against a quiet moss floor, without pink furnishings or floating motion.

The website's Help Guide and the browser/device in-game help cover seventeen
topics, from first-run controls through care, molting, achievements, placement,
power, radio, backups and recovery. Help appears only in the main game Menu,
not in Settings or About. The browser data in `lib/help.js` is checked
against native `main/help_content.h` to prevent drift. About contains Firmware
Update; WiFi no longer mixes update actions with network management.

Decor/gift placement and hold-to-edit use a visible artwork preview. Hold and slide
to adjust it; release keeps editing. Done commits once; Cancel keeps the
previous position. Purchased ownership is retained. Browser pointer capture
keeps drags working outside the canvas; arrows move the preview, Enter confirms
and Escape cancels. No placement release can also pet, pounce or dim the tank.
Placed-item controls remain over the enclosure: Put Away, Blocking for decor,
Done and Cancel. No submenu/Back is shown. Done applies position and blocking;
Cancel discards both; Put Away retains ownership and stays in the enclosure.

Twenty reachable achievements show goals and pay 15 XP plus 20 Silk once.
First Pet is visible; the existing molt animation now returns to normal care
after 800ms. Old earned badges are not repaid. Shops disable unaffordable items,
keep owned reusable items free, and preserve catalogue pages after purchases.
Centered icons and inset, enlarged slider hit boxes retain the single-tile layout.

Silk Garden retains its soft pads and bell line; Moonlit Hollow uses slower
low-register plucks and Rainy Canopy uses quick mallet arpeggios. Integer PCM
matches firmware exactly. Tests enforce rhythmic/register separation as well
as peak, loop-seam and gain limits. Navigation taps are quiet pitched ticks,
not bursts of white noise.

Disabled controls have dim text and dashed borders; destructive actions use
red, selection uses mint with an inset marker, and keyboard focus uses a light
outline. Slider, checkbox and keyboard geometry and interactions are retained.
These source changes do not imply that a build has been deployed or published.

The flasher shows a concise version/integrity/save-preservation status, not
filenames, addresses or hashes. All manifest, binary hash, partition and NVS
verification remains active internally.

Focused visual CSS regressions (no build, browser or hardware required):

```powershell
node --test tests/visual-style.test.mjs
```

## Run and build

Requires Node.js 22+ and npm. In `site`:

```powershell
npm ci
npm test
npm run build
npm run serve
```

Source: `http://127.0.0.1:8080/site/`. Production:
`http://127.0.0.1:8080/site/dist/site/` after release assets are copied.

For GitHub Actions, use `actions/setup-node` with Node 22, npm caching and
`cache-dependency-path: site/package-lock.json`. Set the run-step working
directory to `site`; execute `npm ci`, `npm test`, and `npm run build`.
Upload **`site/dist`** as the Pages artifact only after release tooling copies
artwork into `site/dist/assets` and adds an optional root `index.html` redirect
to `./site/`. `npm test` needs no browser or serial hardware.

Commit `site/package.json` and `site/package-lock.json`. Do **not** commit
`site/node.exe`, `site/npm-tool/`, `site/node_modules/`, `site/.npm-cache/`,
`site/*.tgz`, or generated `site/dist/`; these are ignored locally.
Portable Windows equivalents, with the repository root as the working directory:

```powershell
$env:PATH = "$pwd\site;$env:PATH"
Set-Location site
.\node.exe npm-tool\package\bin\npm-cli.js ci --cache .npm-cache
.\node.exe npm-tool\package\bin\npm-cli.js test
.\node.exe npm-tool\package\bin\npm-cli.js run build
.\node.exe serve.mjs
```

The portable files are only a local convenience; CI uses ordinary Node/npm.
`native-spider.png` is the published promotional preview, generated directly from
`lib/spider-renderer.js` so it matches the spider the device draws.
Gameplay and the sleeping preview render the native spider directly through
`lib/spider-renderer.js`; the hero uses it too, with one small beetle beside it.
The source simulator works without an npm install; device flashing requires
the production bundle. `node serve.mjs` alone serves the source.

The v1.1.25 device OTA implementation validates latest-release metadata and
partial-download ranges, but a complete hosted OTA reboot has not passed on the
current hardware connection. Use USB for a verified installation; browser
transport mocks are not proof of physical flashing. See the
[release verification boundary](../docs/RELEASE_v1.1.25.md).

Build output is **`site/dist/site/`**, including a local
`vendor/esptool.js` bundle. Deploy `site/dist` as the public root after copying
the project's PNG artwork into **`site/dist/assets/`**. The website is then at
`https://HOST/REPOSITORY/site/`; the assets are at `../assets/`.
An optional deployment-root redirect can send `/REPOSITORY/` to `./site/`.
All release/schema/module links are relative and support GitHub Pages subpaths.

Release tooling must supply before building:

- `site/save-schema.json`: the JSON returned by the real compiled `save schema`.
- `site/firmware/manifest.json` and the five binary files it describes.

Absent resources produce explicit errors; there is no simulated flash success.
The build intentionally permits missing release inputs so the playable website
can be built before a firmware release. Publish device functionality only after
these files have been provided.

### Existing-device update

The device-operation selector defaults to **Update existing device**. After
verifying release hashes, it compares the installed partition-table bytes with
the release table. A mismatch stops before writes. An update writes only the
application, matching artwork and finally OTA selection; it leaves the
bootloader/table/NVS untouched, then reads all 24 KB of NVS back and compares
every byte. NVS snapshots stay in memory, are cleared, and are not logged or
uploaded. Full installation remains available for new boards/layout migration,
also without a whole-chip erase. Export a pet backup before either operation.
Interrupted updates still require USB recovery; save preservation is not an
atomic firmware/artwork transaction.

On-device **WiFi > Firmware Update > Update From GitHub** resolves the latest
stable release, verifies its application hash and installed artwork compatibility,
and writes only an inactive application slot. It does not update artwork or
emulate radio access in the browser. See [OTA policy](../docs/WIFI_ARCHITECTURE.md).

Pinned dependencies: `esptool-js@0.7.1`, `js-md5@0.8.3`, and build-only
`esbuild@0.25.5`; `npm ci` downloads their locked dependencies. No TypeScript.
No dependency downloads occur in the browser.

## Browser habitat

The browser ports the device's 480×480 coordinate system, native pixel spider,
background/prey/gift art, care HUD, three-row menus, first-run color picker and
eight-round Bug Hunt, Color Match and Quick Tap games. The square world scales
to a larger responsive screen without shrinking the logical touch targets.
Fullscreen preserves its aspect ratio and has an explicit exit button.
It is a JavaScript port, **not an ESP32 instruction-set or LVGL emulator**.

Tap the top-left page ear to open care. Only Back navigates menus; empty areas
do not dismiss them. HUD/menu opening pauses the tank, as on the device.
Keyboard: M toggles care, arrows aim, Space releases a pounce. Auto Feed is one
Settings switch, off by default, that drives both wild prey and unattended
hunting. Manual feeding and
aim/release are otherwise required. Gifts cost Silk and unlock by level.
Hold decor/a gift to move it; redundant movement menu entries are removed.
Gift movement is free and preserves variant/bonus/ownership. Backgrounds cost
Silk once; cork and reapplying owned themes are free. There are 48 backgrounds,
68 decor styles, 36 gifts and twelve treats. Borders offers 48 corresponding
frames, bought independently with Silk, mixable with any floor, with free
reapplication/removal. All eleven requested theme families are included.
Placement grows past twelve and save v12 keeps
all extra records, music and lights. Double tap dims just the enclosure.
Gentle ambient music has an independent Sound switch; master mute still
silences everything. Ground prey gait follows travel and plants feet at
rest; eight-heading sprites use 48x48 native size. The spider is unchanged.
Sound page 2 offers Music Volume and Soundtrack: Silk Garden, Moonlit Hollow
and Rainy Canopy. Gain and selection persist independently of master volume.
SD/file and screenshot outcomes appear inside the game, including fullscreen;
browser actions request local downloads/file restore, never device SD access.
Achievement rows use the unique native icons; unavailable pages are disabled.
Promotional spiders are rendered from the same `lib/spider-renderer.js` the game
uses, so they match the device; each player's chosen colors are preserved.
The unboxed battery icon appears only with care, inside the upper-right safe
area below the identity panel. The page ear is flush with the square playfield
corner at all sizes. Settings > Save Screenshot downloads a 480x480 PNG of the
canvas only: no menu or HUD, no SD-card emulation.
The locally bundled Montserrat font retains its OFL notice.

WiFi/RTC/OTA, SD-card access and battery/charging telemetry require actual
hardware. The corresponding browser menus say so: WiFi opens real USB tools,
SD Card offers browser JSON backup/restore, and Battery and Display does not
invent voltage or charge readings. Device binary saves remain separate.
Battery stores profile, screen timeout, idle sleep delay and RTC wake preferences,
explicitly labeled Device Preference. Deep Sleep Now remains device-only;
the browser does not emulate MCU power or physical wake.

Progress uses `spidarium.browser-habitat.v1` in localStorage, including names,
colors, progression and automation preferences. Existing browser saves migrate
instead of being reset. Saves occur after actions, every five seconds and on
page hide. Corrupt/unreadable saves are not silently overwritten; read/write
and quota errors are visible. SD Card → Back Up Save downloads a JSON backup.
Browser storage can be cleared or evicted and private browsing is temporary.
No offline-time decay is applied. Resetting the browser never touches hardware.

## License

Free for personal, non-commercial use under the repository's [LICENSE](../LICENSE).
The deployed site includes a copy at `./LICENSE`. Commercial use needs separate
written permission; third-party libraries/fonts retain their own licenses.

## Real device tools and safety

Use desktop Chromium (Chrome/Edge) on HTTPS or localhost with a USB data cable.
Web Serial is not available in Safari, Firefox or most mobile browsers.
The board's native USB Serial/JTAG port is `303A:1001`.
Opening the port can reset the board. Console connection waits three seconds
and drains the multi-line help frame before allowing commands. Commands are
serialized, have explicit acknowledgements and bounded timeouts. Timeout
disconnects the session to prevent late replies satisfying a new request.

### Flash

Check release first: all five image sizes and SHA-256 hashes are checked, and
the binary partition table must match image offsets/capacities and preserve NVS
at `0x9000..0xf000`. Offsets come from the manifest and partition table, never
from outdated fixed example offsets. The expected current release is:
bootloader `0`, partition table `0x8000`, OTA data `0x10000`,
application `0x20000`, storage `0x920000`.

Back up first and explicitly accept the risk. Console stream locks are released
before bootloader transport opens. `esptool-js` identifies an actual ESP32-S3,
writes actual images with `eraseAll:false`, preserves embedded flash settings,
and verifies device MD5 results. Wrong chips, downloads, hashes or writes fail
explicitly. No full-flash erase or automatic reset is offered.
After success, press RESET manually. For recovery hold BOOT, tap RESET, release
BOOT, then retry. Never unplug while writing.

SHA-256 proves downloaded image integrity against this manifest; it is not
publisher authentication. Use a trusted release/site.

### Save

Export reads `SAVE HEX …` plus `OK save export` and downloads the **complete raw
binary save**, not only its payload. Offline viewer accepts raw `.bin` or
hex `.txt`/`.hex` and uses the bundled compiled schema. It checks the 12-byte
little-endian magic/version/reserved/payload-size header, legacy fixed payloads
v10/v11's 260-byte and v12's 268-byte prefixes plus counted six-byte decor tails,
schema field boundaries and finite numeric values. It shows absent appended
fields in legacy saves without inventing values. It does not edit device blobs.
Firmware remains the authority on field-level compatibility.

Restore requires a live device schema, an explicit checkbox and a second
confirmation. `save restore HEX` respects the schema's 20 KB payload ceiling
and paces serial chunks for the complete variable tail. Only
`OK save restore; reboot required` counts as success.
The device is left paused; **Reboot device** requires separate confirmation.
Keep the original backup. Never close/reconnect as a substitute for a carefully
confirmed reboot: opening the console may itself reset a device.

### WiFi privacy

SSID is 1–32 UTF-8 bytes; passwords are empty for open networks or 8–63 UTF-8
bytes. Pipes, CR/LF, NUL and whitespace that the console would trim are rejected.
SSID/password travel only via the chosen local serial port. Inputs are cleared
on submission; neither credential is saved in localStorage, URLs or activity.
All serial input is suppressed during WiFi setup **and afterward until
disconnect**, including async radio messages. Sensitive errors are generic.
JavaScript strings cannot be cryptographically wiped from process memory.

## Verification

`npm test` runs Node built-in behavioral tests for serial fragmentation/help
draining/serialization/timeouts/privacy, save codecs and invalid headers,
WiFi injection/length boundaries, manifest SHA/size/partition/NVS checks,
flasher transport lifecycle/error propagation, simulator care/prey/catches,
gift ownership, eight-round games and browser save sanitization.

These mocked tests do not claim physical flash verification. No firmware or
hardware serial access is needed to develop the website. Release/device
verification belongs to the firmware workflow.

Browser smoke checks were also run with the integrated Playwright browser:
care, three-prey cap, a mouse-drag catch, all eight memory-game rounds, gift
purchase/visit, sleep/wake, settings and achievement images, an offline raw-save
upload with a mocked schema, disabled restore without hardware, and explicit
missing-release failure. The production bundle imported correctly and its MD5
function matched the known `abc` digest; all normal resource requests were local.
No board was connected or flashed.
The corrected production page was also verified to render the native 96×96
canvas, request no obsolete spider-sheet PNGs, default to “SPIDER” with an
empty name input, and offer Play rather than Pet. Renderer tests compare all
eight angular leg poses to the exact native C regression fixtures.
The current hardware schema is v12 with 52 fields and a 268-byte fixed payload
plus six bytes per extra decor slot. Save-codec tests exercise historical
v2-v9 prefixes, variable tails, high ownership bits and malformed records.

Captured screenshots are in `site/screenshots/`: `desktop.png` (whole website),
`simulator.png` (interactive section), and `mobile.png` (390-CSS-pixel layout).
They were produced with the browser's `page.screenshot`/element screenshot
tools; no additional screenshot dependency is required.
