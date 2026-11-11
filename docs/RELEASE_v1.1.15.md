# Spidarium v1.1.15 — A spider that stays solid, and art that matches the screen

## The spider was never transparent

A sick spider looked see-through. It was not. `cspider_render()` finished with

```c
lv_obj_set_style_opa(s_canvas, g.sick ? LV_OPA_60 : LV_OPA_COVER, 0);
```

so a sick spider was drawn at 60% widget opacity. Against the pale v1.1.13
ground textures that reads exactly as "the spider has gone transparent" — the
floor shows straight through the body.

Sickness is now expressed in the palette instead. Every colour the renderer uses
is blended 45% toward **its own luma** and then darkened 12%, so the spider stays
fully opaque and simply looks unwell: greyer, flatter, a little dimmer. The
widget opacity is `LV_OPA_COVER` unconditionally, and there is no longer any code
path that can make the spider translucent.

The luma target is the important detail. The first attempt mixed toward a fixed
blue-grey, which *increased* channel spread on near-white pixels — the fangs and
eye whites got more saturated, not less. Mixing toward each pixel's own luma
desaturates every pixel by construction.

## The published spider art is now an export of the renderer

The device draws the spider procedurally in `main/cspider.c`. The PNGs in
`assets/` came from a separate hand-written design in `tools/gen_sprites.py`, and
the two had drifted apart — different body shape, different leg pose, different
palette, and fangs the renderer does not draw at all.

`draw_spider()` is now a port of the C renderer: the same geometry constants, the
same leg-pose maths, the same draw order, the same RGB565 quantisation, the same
highlight ramp. The PNGs are generated output, so they cannot drift again — if
the renderer changes, the export changes with it.

The 39 spider frames also leave the byte-for-byte preservation set. They are
still validated for binary alpha, determinism and the 12-colour cap, and they are
exempt from the 2×2 uniform-block rule because they are single-pixel exports
rather than 2× upscales. `site/native-spider.png` was regenerated from the
browser renderer for the same reason.

## Verification

- `asset_build.py`: 158 MMAP entries verified, 158 PNGs valid, deterministic
  across two passes, 84 preserved PNGs byte-identical.
- `build.ps1` exit 0, zero `main/` warnings.
- `flash.ps1 COM7` exit 0.
- `run_tests.py COM7`: **47 passed, 0 failed**.
- `uitest`: `backgrounds=1`. `decortest`: `placed=12 slots=12`.
- `stability_check.py COM7 40`: `VERDICT: PASS`, 0 faults.
- `scanloop 5`: `largest_drift=0`. `render 500`: `largest_drift=0`.
- Internal DMA largest 63,488 bytes, above the 51,200 floor.
- Browser suite **100/100**. The sickness test asserts alpha `[0,255]` rather
  than `153`, and a new test asserts every drawn pixel stays opaque and more
  than 90% of them desaturate.
- Captured on hardware with the pet healthy: body mean `(80,83,88)`, opaque.
- Captured on hardware with the pet forced sick (`state=9 sick=1`): body mean
  `(82,85,90)` — darker and desaturated, **still opaque**. The floor does not
  show through.
