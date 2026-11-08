# Spidarium v1.0.0 verification

Verified on 2026-11-07 using the Waveshare ESP32-S3-Touch-AMOLED-2.16 on COM7,
ESP-IDF 5.5.6, Waveshare BSP 2.0.1 and LVGL 9.6.0~1. Component versions are
pinned and `dependencies.lock` is committed. The existing pet and WiFi
credentials were retained; normal gameplay may advance the pet during tests.
All 56 explicit defaults match the verified active `sdkconfig`. A final wrapper
rebuild after aligning those defaults changed none of the five image hashes.

## Real hardware

| Check | Result |
|---|---|
| Wrapper build / full flash | Exit 0; no compiler warnings in `main/` |
| Pet-preserving gameplay regression | 39/39 PASS |
| Render stress | 500 ticks; largest-block drift 0 |
| Temporary radio stress | Five successful scans; largest-block drift 0 |
| Stability | 40-second clean boot window; PASS, zero faults |
| Final internal / DMA largest block | 63,488 bytes; baseline requirement 51,200 |
| SD backup | 144 bytes written and verified by readback |
| Real browser-device protocol | Schema/export, four invalid restores, exact valid restore, overwrite guard and explicit reboot PASS |
| Native save-format diagnostic | Current 144-byte and legacy 132-byte blobs accepted; truncated legacy blob rejected |
| Final console stack margin | 4,060 bytes after restore tests; requirement at least 2,048 |
| Actual screenshots | Complete lossless HUD, menu and achievements captures |

The UI checks include repeated Feed rejection with three live prey, re-enabling
after a catch, coherent battery telemetry, layering, row icon placement, text
geometry and disabled/inert pagination boundaries. Tests restore synthetic
state and use mock minigame rewards rather than persisting test outcomes.

[Device screenshots](README.md) are native 480x480 LVGL snapshots, not browser
mockups. `site/native-spider.png` is an actual transparent 96x96 canvas capture.

## Artwork and browser

- All 134 PNGs and packed entries pass deterministic generation, alpha,
  palette and 2x2 pixel-grid validation.
- Eleven unique achievement badges and all eight species-specific splatters
  pass content checks. All 115 other previously accepted images are preserved,
  including the 39 original spider setup PNGs.
- The website uses the current native canvas anatomy, not those obsolete
  spider gameplay PNGs. Default name is generic **Spider**, not Pip.
- Browser behavioral suite: **32 PASS**; static production build PASS.
- Browser checks load actual badges, prey splatters and current enclosure
  artwork; corrected desktop, simulator and mobile screenshots are included.
- Host protocol/capture/release tests: **19 PASS**, including complete-image
  rejection, both save layouts, safe flash ranges, application descriptor,
  SHA-256 hashes and nested third-party notices.

## Release contract

The ZIP and website use the same five verified images. Packaging checks the
built project/version/chip, application descriptor, exact offsets, erase-block
boundaries and SHA-256 hashes. The generated manifest matches the actual
`dio`, `80m`, `16MB` build settings. NVS at 0x9000-0xEFFF is excluded.
Third-party SDK/component license and notice files accompany the images.

The Pages workflow tests/builds the website and validates the official
v1.0.0 ZIP before deployment. Publication success is verified separately
against the live release and HTTPS website; a local preview is not evidence
of a successful deployment.

### Published result

- [Official v1.0.0](https://github.com/jeromedontdev/Spidarium/releases/tag/v1.0.0):
  published, not draft/prerelease, targeting code commit `0f16802`.
- [Pages deployment](https://github.com/jeromedontdev/Spidarium/actions/runs/42435991497):
  success; [live website](https://jeromedontdev.github.io/Spidarium/) verified.
- Unauthenticated ZIP download matches SHA-256
  `ea8d5aa183f3e42c6515ff9cf41a1497f290bb066a5de33a17a0d2023880688d`.
  Standalone application, manifest and checksum file are also published.
- Live HTTPS checks match all five firmware images, all 134 PNGs and the
  compiled schema byte/data-for-data. Browser has no obsolete live spider
  PNGs, no Pip text and no broken images; the native-style canvas is active.

## Boundaries

Physical Web Serial flashing through a browser was **not** exercised: remote
browser tests have no USB-device access. Actual wrapper flashing and the
real USB console save/restore backend were tested; browser transport tests
use an explicit mock. Test results do not imply an ESP32/LVGL browser emulator.

The real USB-powered PMIC was checked, and the earlier uninterrupted
315-second dim/off/wake test passed. Unplugged runtime, physical low-battery
behavior and charging transitions are not certified. SHA-256 detects
corruption, not publisher authenticity. Application-only OTA does not replace
artwork; use the complete initial release installation.

Local detailed evidence is intentionally excluded from publication:
`build/release_final_build.txt`, `build/release_final_flash.txt`,
`build/release_final_results.txt`, `build/release_final_stability.txt`,
`build/release_final_web_device.txt`, `build/release_final_sd.txt`,
`build/release_final_mem.txt`, `build/release_host_tests.txt` and
`build/release_art_validation.txt`.
