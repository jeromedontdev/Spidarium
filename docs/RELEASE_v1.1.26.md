# v1.1.26 - Dragging sliders and cleaner menu tiles

- Sliders remain in single grid tiles.
- Browser sliders explicitly capture held mouse/touch/pen drags, updating live
  even outside the track. Release/cancel, scaled screens, all four rotations
  and native keyboard range controls are covered.
- Removed submenu arrows from device/browser tiles. Selected checkmarks,
  checkboxes, Back and footer paging arrows remain.
- Added real native held/moving input regression and browser capture tests.
- Spider artwork, all337 PNGs, catalogue IDs and save v12 remain unchanged.

Verification before release: browser165/165; real mouse and touch drags;
native UI diagnostics pass (held60, moved84 before release), wrapper build/flash
exit0 with zero warnings, render500/scanloop5 drift0, internal/DMA largest63,488,
and40-second soak PASS with zero faults/errors. Original pet restored.
Final versioned release/publication evidence is in [PROGRESS](../PROGRESS.md).

**Unchanged update boundary:** GitHub latest discovery works, but complete hosted
OTA application download/new-slot boot remains unverified because transfers
stalled on the tested connection. Use USB for installation. Physical browser
flashing is not claimed without a granted serial port. Back up first: USB
app/art updating is not atomic. See [v1.1.25](RELEASE_v1.1.25.md).
