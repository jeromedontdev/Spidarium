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

Disabled controls have dim text and dashed borders; destructive actions use
red, selection uses mint with an inset marker, and keyboard focus uses a light
outline. Slider, checkbox and keyboard geometry and interactions are retained.
These source changes do not imply that a build has been deployed or published.

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
Hero and gameplay render the native spider directly through `lib/spider-renderer.js`.
The source simulator works without an npm install; device flashing requires
the production bundle. `node serve.mjs` alone serves the source.

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
Hold decor/a gift to move it, or use Arrange Decor / Placed Gift in Menu.
Gift movement is free and preserves variant/bonus/ownership. Backgrounds cost
Silk once; cork and reapplying owned themes are free. Save v9 migrates old
equipment without charging. Automatic ambience is disabled; foreground sound
and volume/mute remain. Ground prey gait follows travel and plants feet at
rest; eight-heading sprites use 48x48 native size. The spider is unchanged.
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
little-endian magic/version/reserved/payload-size header, 120/132-byte payload,
schema field boundaries and finite numeric values. It shows absent appended
fields in legacy saves without inventing values. It does not edit device blobs.
Firmware remains the authority on field-level compatibility.

Restore requires a live device schema, an explicit checkbox and a second
confirmation. `save restore HEX` is capped at the supported 144-byte blob
(288 hex characters). Only `OK save restore; reboot required` counts as success.
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
The current bundled hardware-compiled v9 schema has 41 fields. Save-codec tests
exercise the 120-byte legacy and 248-byte current payloads plus v2-v8 prefixes.

Captured screenshots are in `site/screenshots/`: `desktop.png` (whole website),
`simulator.png` (interactive section), and `mobile.png` (390-CSS-pixel layout).
They were produced with the browser's `page.screenshot`/element screenshot
tools; no additional screenshot dependency is required.
