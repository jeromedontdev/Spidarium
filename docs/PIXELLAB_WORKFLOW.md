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
center bounds are16 tones/luma64, with stricter Minimal and hash-pinned
moss/Crystal exceptions; follow the [style contract](ART_STYLE.md).

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

## Budget and MCP workflow

Use the authenticated PixelLab MCP server; no key belongs in source, prompts,
logs, shell arguments or documentation. Token-free `get_upload_url` provides a
temporary upload URL; upload only the intended art reference with `curl.exe`,
then pass its `upload:<id>` to image-reference parameters. Do not record the
temporary signed URL in permanent documentation.

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
