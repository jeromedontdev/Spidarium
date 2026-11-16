# Getting good PixelLab results for Spidarium

## What actually worked

The successful Crystal floor took three deliberately reviewed requests, not a
large batch. "SNES style" alone was insufficient:

1. **Original crop plus old game background as a layout reference:** the output
   copied the old background's primitive shapes too strongly. Better prompting
   did not overcome the wrong composition guide.
2. **Original crop only:** much better chipped stone, crystal facets and
   clustered shading, but oversized perimeter boulders/crystals. The user liked
   the material texture, not the enclosing scenery.
3. **Successful second output as a material reference:** ask for only its
   violet mineral floor texture, extended across the whole square. Explicitly
   exclude large crystals, rocks, rim and frame. This produced the approved
   [Crystal floor](../assets/sources/crystal_floor.png).

Keep backgrounds, optional borders and movable decor as separate layers.
Do not force the approved floor back to a three-color center: its richer
texture was explicitly approved. The later offline moss trial was also approved, and its approach is now used
for all remaining theme pairs without PixelLab spending. Current generated
center bounds are 16 tones/luma64, with stricter Minimal and hash-pinned
moss/Crystal exceptions; follow the [style contract](ART_STYLE.md).

## Backgrounds are ground texture, not scenes

Borders already carry the edge decoration: stones, roots, foliage banks and
accent materials live in the separate `border_*` layer, which is composited
over the floor at runtime. A background that draws its own chunky rim therefore
duplicates that work and fights the border. Generate **pure material, edge to
edge**, and let the border supply the framing.

Organic ground must also not repeat. Brick, floorboards, tiling and paving may
tile visibly because they are manufactured; soil, moss, leaf litter, sand and
gravel must not. A regular cobblestone-like field is a defect on an organic
theme even when its palette and shading are otherwise correct.

## Prompts are generated, not hand-written

Every theme has a recipe in `tools/background_prompts.py`, so the prompt that
produced the approved woodland floor is reused with the material swapped:

```powershell
python tools/background_prompts.py --list
python tools/background_prompts.py --theme snow
python tools/background_prompts.py --all --out scratch\prompts
```

Each recipe declares whether the material is organic or manufactured. Organic
themes get the long non-repetition rule; manufactured themes get a named repeat
unit instead, because forcing irregularity into brickwork produces mush. Two
themes (`minimal`, `neon_alley`) are deliberately near-uniform and get a
narrower tonal paragraph.

Never name a commercial game in a prompt. The provider refuses with
`IMAGE_RECITATION` and the request is billed as a failure.

## Converting a generated texture for the device

Image generators return 1024x1024 or similar, which is not a multiple of the
240x240 the device draws. They also return far more contrast and saturation
than the approved crystal floor, which makes the spider hard to see. Use the
import tool rather than resizing by hand:

```powershell
python tools/import_background.py SOURCE.png --theme woodland --out scratch\review
```

It crops to the largest centred multiple, box-averages to 240x240, fits
luminance contrast to the approved reference, retargets the mean, applies the
shared top-left light, desaturates toward the reference, and quantizes to
64 colors without dithering. It prints the measured profile and the pixel hash,
and writes nothing into `assets/` unless `--install` is passed.

Contrast is **fitted, not fixed**. A brick floor comes back far contrastier than
a snowfield, so a single multiplier either flattens the snow or leaves the brick
too busy to see the spider against. The tool searches for the factor that lands
the central play area near the reference standard deviation of 8.1, and reports
the factor it chose. `--contrast` overrides the search when a theme genuinely
wants more or less.

Brightness is **per theme, not global**. Forcing every floor to crystal's mean of
65 would turn snow, sand and kitchen into mud. `THEME_MEAN` holds the mean each
procedural floor already shipped with, so an imported floor keeps the brightness
players associate with the theme. `--mean` overrides it.

Review the result at native scale with the border and spider composited before
adopting it. To adopt, re-run with `--install`, then add the theme and the
printed hash to `APPROVED_FLOORS` in `tools/approved_sources.py`. That registry
is what `gen_sprites.py` imports from and what `asset_build.py` verifies
against, so an approved floor can never drift from the reviewed pixels.

## Batch generation

`tools/background_batch.py` drives generation and conversion together:

```powershell
python tools/background_batch.py --themes snow gothic --out scratch\batch
python tools/background_batch.py --remaining --out scratch\batch --submit
```

Without `--submit` it is a dry run, so the whole plan can be inspected for free.
It writes a `report.json` recording which source produced which converted
candidate. It never installs anything.

## Reference selection matters more than a style label

- Crop the relevant panel from the actual user reference, not a collage of
  unrelated environments. Keep the original crop available for visual review.
- Explain **what to copy and what not to copy** from each image. Label material
  shading, palette and pixel technique separately from layout and object size.
- Avoid using disappointing existing art as a composition reference for a
  quality upgrade. It can anchor the model to precisely what needs replacing.
- Once a material treatment succeeds, reuse that output as the style reference.
  Say "copy only the floor material" rather than "make something like this".
- Do not send the spider or a furnished gameplay screenshot as an unqualified
  style image: the model might bake characters/furniture into the background.
  Compose those locally afterward to assess contrast.

## Prompt recipe

Lead with the deliverable and its geometry, then materials, then exclusions:

> One opaque 240x240 top-down Crystal cave floor, not a side-view landscape.
> Copy only the reference's violet/slate mineral floor material and SNES-era
> stepped shading. Extend that material across the entire square. Crisp
> intentional pixel clusters, chipped stone planes, cool shadows and small
> broken highlights; finite hue-shifted ramps, top-left light, at most 64 colors.
> No large crystals, boulders, enclosing rock rim or decorative frame: those
> belong to separate border/decor layers. No spider, insects, furniture, text,
> collage, blur, gradients or antialiasing.

This is a reusable recipe, not a guarantee or a replacement for reference review.
Describe concrete surfaces instead of stacking vague adjectives such as
"beautiful", "high detail" and "retro". More detail should mean readable
planes/facets and intentional clusters, not random bright noise.

### Thin border recipe

Borders have a different native pixel scale than floors. Generate **120x120**
logical art, then enlarge exactly 2x to the runtime **240x240** PNG:

> Only a thin square Crystal enclosure border, not a filled cave scene. All
> four edges reach the canvas boundary. Keep all art within the outer 8 pixels;
> the entire central [8,112) square is transparent. Modest 2-4px mineral edging,
> chipped violet/slate stones and small lavender/cyan faceted crystal tips.
> Corner groups fit inside 8x8 pixels; edge tips are only 3-6px tall. Copy the
> reference's shaded materials, not its huge crystal sizes or enclosing layout.
> At most 16 visible colors, binary alpha, crisp pixels. No floor, backdrop,
> shadow across the opening, giant boulders, pillars, filigree or bright white glare.

The runtime opening must remain `[16,224)` on both axes, fully transparent.
Do not shrink it just because a generated frame is attractive. Inspect every
candidate before deciding whether any meets the contract. Any geometric
extraction must be explicit and reproducible, with raw source retained; never
claim the raw generation passed checks if it required correction.

The Crystal request returned four 16-color binary-alpha frames, but all had
17-18px inward detail despite the 8px instruction. Candidate 0 was selected
for quieter violet stone and restrained crystal accents. Its source18px
rim is explicitly nine-sliced to8px with nearest-neighbor sampling, then
doubled to240x240. This retains the complete corner/edge composition rather
than cutting crystal tips off. Raw source and fitted pixels are hash-pinned;
see [the precise transformation](../assets/README.md#pixellab-crystal-border).
PixelLab is good at material rendering, not guaranteed exact frame geometry.

### Border prompts are generated too

Every theme has a frame recipe in `tools/border_prompts.py`, mirroring the
floor builder:

```powershell
python tools/border_prompts.py --list
python tools/border_prompts.py --theme snow
python tools/border_prompts.py --all --out scratch\prompts
```

Each recipe names the band material, the motif that makes the frame
recognisable at 8 logical pixels thick, and the palette. `minimal` is the only
theme marked plain, and it gets a restraint paragraph instead of a motif.

The prompt states the geometry explicitly: a **narrow** band, an **empty and
hollow** middle, and no scene. Generators otherwise draw a picture with a frame
around it, which would hide the spider.

### Converting a generated frame for the device

```powershell
python tools/import_border.py SOURCE.png --theme snow --out scratch\review
```

Generators do not honour the 8px rim. They typically leave a black margin
around the frame and draw a band 10-14px wide. The tool therefore:

1. crops to the drawn content, ignoring that margin;
2. finds the hollow middle by taking the longest run of rows and columns that
   are mostly empty;
3. nine-slices the band down to exactly 8 logical pixels;
4. forces everything inside the opening to alpha zero;
5. quantizes to 16 colors without dithering and restores binary alpha.

Contrast is a **ceiling, not a target**. A frame is decorative, so it is allowed
far more contrast than a floor: the approved crystal frame already runs at a
luminance standard deviation near 47. The tool only reduces contrast until the
rim comes in under `SD_CEILING` (80), so a generated frame keeps the shading
that makes it read as carved. Fitting to crystal's exact sd instead flattens
that shading into a flat strip.

Brightness is per theme, taken from the procedural frame the theme already
shipped with, so a snow frame stays bright and a cave frame stays dark.

To adopt, re-run with `--install`, then add the theme and the printed hash to
`APPROVED_BORDERS` in `tools/approved_sources.py`. `tools/border_batch.py`
drives generation and conversion together, exactly like the floor batch:

```powershell
python tools/border_batch.py --themes snow gothic --out scratch\borders
python tools/border_batch.py --remaining --out scratch\borders --submit
```

## Budget and MCP workflow

Use the authenticated PixelLab MCP server; no key belongs in source, prompts,
logs, shell arguments or documentation. Token-free `get_upload_url` provides a
temporary upload URL; upload only the intended art reference with `curl.exe`,
then pass its `upload:<id>` to image-reference parameters. Do not record the
temporary signed URL in permanent documentation.

For scripted or non-interactive generation, [imagegen.py](../tools/imagegen.py)
is the programmatic path to both providers. They are independent services:
OpenRouter is a model marketplace at `openrouter.ai`, PixelLab is a separate
pixel-art service at `pixellab.ai`. Neither routes through the other, and each
has its own credential. The script is dry-run by default and spends nothing
until `--submit` is passed, reads credentials only from `PIXELLAB_API_KEY` /
`OPENROUTER_API_KEY`, refuses to write into `assets/`, and never overwrites an
existing file. It writes a JSON receipt beside each candidate recording the
prompt, request payload, usage and image hashes.

```powershell
python tools/imagegen.py models --provider openrouter   # public catalogue, no key
python tools/imagegen.py balance --provider pixellab
python tools/imagegen.py generate --provider pixellab --prompt "mossy stone floor" --width 240 --height 240 --out scratch
python tools/imagegen.py generate --provider openrouter --model <slug> --prompt "mossy stone floor" --width 1024 --height 1024 --out scratch
```

Add `--submit` to any `generate` call to actually spend. OpenRouter has no
default model because slugs and availability change, so `--model` is required
there; PixelLab's synchronous Pixen endpoint is the wired path. Output is a
candidate, not installed art: review it, then integrate through
[gen_sprites.py](../tools/gen_sprites.py) and validate with
[asset_build.py](../tools/asset_build.py).

### Comparing several OpenRouter models in one run

Because OpenRouter is a marketplace, one prompt is often worth trying against
several models. Repeat `--model` or comma-separate the slugs; each model gets
its own request, its own receipt, and a filename tagged with the model so
candidates stay distinguishable.

```powershell
python tools/imagegen.py generate --provider openrouter `
    --model google/gemini-3.1-flash-image,openai/gpt-image-2.6-sunburst `
    --prompt "mossy stone floor" --width 1024 --height 1024 --out scratch --submit
```

Duplicate slugs are collapsed so nothing is paid for twice. If one model fails,
the images already generated are kept and the run exits non-zero listing which
models failed — a single bad slug never discards work you paid for.

1. Read `get_balance` and `search_help(tool="create_image_pro")` **before**
   spending. Provider pricing can change; the submission receipt is authoritative.
2. Choose the **native art size**, not the screen's physical resolution.
   Confirm transparency, color count, reference roles, seed and candidate count.
3. Submit once. Save the exact prompt/settings, reference hashes, job ID, seed,
   returned cost and raw candidates outside the runtime asset directory.
   A seed records the request; it does not guarantee identical output across
   future provider/model changes. Retaining the actual source is essential.
4. Use `wait_for_jobs`, then `get_image(job_id=...)`. Reconcile a timeout with
   that same job; do not blindly retry and spend twice.
5. Read balance afterward. No automatic paid corrections or additional batches.

Verified Pro square-canvas examples for this experiment:

| Canvas | Debit per request | Candidates |
|---|---:|---:|
| 120x120 | 10 generations | 4 |
| 240x240 | 10 generations | 1 |
| 480x480 | 25 generations | 1 |

Candidate count and cost are size bands, not a linear price per pixel or a
price per candidate. The three floor requests cost 30 of 40 trial generations.
The matching 120x120 border request uses the final 10 for four candidates in
one request, not four separately billed retries. These are **subscription
generations**, not ten dollars of account credit.

## Review, integrate and verify

- Review raw art at native resolution and nearest-neighbor enlarged resolution.
  Compare a floor-only view and the composed scene with existing border,
  furnishings and spider. Automated palette checks cannot establish beauty.
- Use [pixellab_prepare.py](../tools/pixellab_prepare.py) for offline briefs,
  protected-region masks and strict current runtime-candidate validation. It never
  submits requests or handles credentials. Its edit/inpaint briefs are not
  executable MCP payloads; verify the selected tool's current schema first.
- Do not silently resize an arbitrary wrong-sized image, quantize away a
  palette failure, flatten alpha or erase a noisy center to claim acceptance.
  Planned 120-to-240 nearest-neighbor border scaling is part of the contract,
  not an accidental dimension repair.
- Retain the chosen immutable source under `assets/sources/`, check its decoded
  pixel hash, and integrate it in [gen_sprites.py](../tools/gen_sprites.py).
  Do not merely replace a generated PNG: the next build would overwrite it.
- Validate all packed QOI entries and decoded pixels with
  [asset_build.py](../tools/asset_build.py), including two deterministic runs.
  Compare every other runtime PNG and both spider renderers to pre-edit hashes.
- Build/flash through the wrappers, preserve the freshly captured current pet
  around mutating tests, activate via normal shop/save rules, and verify reboot.
  Check render/scan memory drift, largest DMA block and stability soak.
- A successful local flash is not a public website deployment, and an LVGL
  capture is not evidence of physical touchscreen appearance.
