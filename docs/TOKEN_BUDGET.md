# Token budget — how to stop mindless spend

This project has one expensive resource that is not on the board: **image
generation**. A single nano-banana call costs more than a whole day of agent
coding, and a careless "regenerate all the art" instruction can burn more in ten
minutes than the entire firmware rewrite.

This page is the checklist to run before any task that could spend money. It is
deliberately short, because a long policy gets skipped.

---

## The one rule

**Never regenerate art to fix a code problem.**

Almost every art complaint in this project's history turned out to be a code
bug: a sprite drawn at the wrong scale, a mask sampled from the wrong row, a
layer parented into the wrong band, a hitbox measured against the box instead of
the artwork. Regenerating the image would have cost money and fixed nothing.

Before any generation, answer: *is the problem in the pixels, or in the code that
places them?* If you cannot point at the specific pixels that are wrong, it is
the code.

---

## Before you generate anything

1. **Check whether the asset already exists.** `assets/` holds 74 decor items,
   36 gifts, 48 backgrounds, 48 borders and the full UI set. Most "we need art
   for X" requests are already covered. Run:

   ```powershell
   Get-ChildItem assets -Filter "*<keyword>*"
   ```

2. **Check whether the existing art can be reused with a transform.** A tint, a
   flip, a scale or a palette swap is free. `theme.c` already tints the whole
   tank from one table; `art_scale` in `decor.c` resizes props without new
   pixels.

3. **Check whether the effect can be drawn from primitives.** The dust motes,
   the flies, the spider's breath and blink, and the whole spider itself are
   drawn from circles and lines. None of them needed a sprite. If the thing you
   want is small, moving, or tinted, it probably does not need art.

4. **Write down the exact prompt and the exact count before calling.** "Generate
   some decor" is unbounded. "Generate 3 variants of a 64x64 mossy stone, flat
   lighting, 2px pixel grid, transparent background" is bounded and reviewable.

---

## The generation budget

| What | Cost | When it is justified |
|---|---|---|
| A new decor item | 1 image | A genuinely new theme or a new shop tier |
| A new gift | 1 image | A new gift family, not a variant of an existing one |
| A background | 1 image | A new enclosure theme |
| A border | 1 image | A new enclosure theme |
| A UI icon | 1 image | Only if no existing icon reads correctly |
| **A re-roll of existing art** | 1 image | **Almost never.** See the one rule. |

**Hard ceiling per task: 6 images.** If a task needs more, it is a batch job that
should be planned, reviewed and run deliberately — not something to discover
halfway through.

---

## The expensive mistakes, and what they cost

These are real, from this project's history.

**Regenerating a whole family to fix one item.** The large climb structures were
regenerated because their pixels looked smaller than everything else. The actual
cause was a scale mismatch in `terrarium.c`: they were drawn 1:1 from a 192x192
source while everything else was drawn at 2x. One line of code, six wasted
images.

**Generating art for something that is drawn in code.** The spider is a
direct-pixel canvas renderer. Asking for "a spider sprite" would have produced
something that could not be animated, could not be tinted per theme, and would
have replaced a working system with a worse one.

**Re-rolling to fix a placement bug.** "The hitbox is way off" was a mask-sampling
bug — the code sampled the square box instead of the artwork's opaque rows. The
art was correct the whole time.

**Generating variants nobody asked for.** Four variants of a gift means four
images, four asset entries, four save-compatibility paths and four chances to
break the catalogue. One good image is usually enough.

---

## Cheap alternatives, in order of preference

1. **Draw it from primitives.** Circles, lines and rectangles. Free, animatable,
   theme-tintable, no asset pipeline.
2. **Reuse an existing asset with a transform.** Tint, flip, scale, rotate.
3. **Reuse an existing asset as-is.** The catalogue is large; check first.
4. **Derive it from an existing asset.** Crop a region, composite two sprites.
5. **Generate one image.** Only after 1–4 are ruled out.

---

## If you are an agent reading this

- **Do not call an image generation tool unless the user explicitly asked for
  new art in this turn.** "Make the tank feel alive" is not a request for
  images. "The fern looks flat" is not a request for images.
- **State the count before generating.** "This needs 2 images" lets the user stop
  you. Generating first and reporting after does not.
- **Prefer the code fix.** If you can make the existing art look right by
  changing how it is drawn, do that and say so.
- **Never regenerate to "refresh" or "improve" art that the user has not
  complained about.** The user has approved the current art. Changing it is a
  regression they did not ask for.

---

## What actually costs tokens in coding

Separate from image generation, the agent's own context is the other budget.
These are the habits that keep it small:

- **Read the file, not the repo.** `view` with a `view_range`, or `grep` for the
  symbol. Dumping a 4,000-line file to find one function costs more than the
  edit.
- **Build once, not after every line.** Batch related edits, then build.
- **Do not re-run a passing suite to "be sure".** If nothing changed, it still
  passes.
- **Redirect build output to a file and grep it.** `build.ps1 *> build_out.txt`
  then `Select-String 'error:'` — a full ESP-IDF build log is thousands of lines
  and almost all of it is noise.
- **Stop when the evidence is in.** A soak PASS plus a clean build plus the
  targeted tests is enough. Running the whole matrix again proves nothing new.
