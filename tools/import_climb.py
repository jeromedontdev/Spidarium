"""Turn a generated climb-structure image into a 192x192 RGBA sprite.

The generator returns a flat JPEG with a light background, not a transparent
PNG, so the background has to be keyed out here. The structures are drawn 1:1
at 192x192 on the device, so this is the only decor importer that does not
downsample to 64x64.

Pipeline:

1. Key the background to alpha zero. The generator is asked for transparency
   but returns JPEG, so the corner colour is sampled and everything within a
   tolerance of it becomes transparent. A flood fill from the border is used
   rather than a global colour match, so a pale highlight INSIDE the object is
   not punched out along with the background.
2. Crop to the remaining content and pad back to a square, so the object is
   centred and fills the canvas without being distorted.
3. Resize to 192x192 with NEAREST, because the source is already pixel art and
   any smooth filter would destroy the hard pixel edges.
4. Quantize to a finite palette and restore binary alpha, matching the contract
   in docs/ART_STYLE.md.

Usage:
    python tools/import_climb.py SOURCE --name branch
    python tools/import_climb.py SOURCE --name branch --preview scratch/out.png
"""

import argparse
from collections import deque
from pathlib import Path

from PIL import Image

TARGET = 192
MAX_COLORS = 12
# How far a pixel may differ from the sampled background colour and still be
# treated as background. Generous, because JPEG ringing around the silhouette
# leaves a halo of near-background pixels.
KEY_TOLERANCE = 42


def _distance(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]), abs(a[2] - b[2]))


def _is_checkerboard(image, sample=96):
    """True when the border is a drawn transparency checkerboard.

    The image model sometimes renders "transparent background" literally, as a
    grey checkerboard, instead of leaving the canvas blank. Keying a single
    colour then leaves the pattern behind as visible squares. Detect it by
    looking for two alternating near-neutral tones in the border band.
    """
    rgb = image.convert("RGB")
    width, height = rgb.size
    pixels = rgb.load()
    tones = set()
    for y in range(0, min(sample, height), 2):
        for x in range(0, min(sample, width), 2):
            r, g, b = pixels[x, y]
            # Near-neutral only: a coloured border is real artwork.
            if max(r, g, b) - min(r, g, b) > 12:
                return False
            tones.add((r // 8, g // 8, b // 8))
    # A checkerboard has at least two distinct neutral tones in the corner.
    return len(tones) >= 2


def key_background(image, tolerance=KEY_TOLERANCE):
    """Flood-fill the border colour to transparent.

    A global colour match would also erase pale highlights inside the object,
    which the branch and driftwood both have. Filling from the border only
    removes background that is actually connected to the edge.

    When the border is a drawn checkerboard, both tones are keyed, so the
    pattern does not survive as visible squares.
    """
    image = image.convert("RGB")
    width, height = image.size
    pixels = image.load()

    checker = _is_checkerboard(image)

    # Sample the four corners and take the most common, so one stray corner
    # pixel cannot pick the wrong key colour.
    corners = [pixels[0, 0], pixels[width - 1, 0],
               pixels[0, height - 1], pixels[width - 1, height - 1]]
    keys = [max(set(corners), key=corners.count)]
    if checker:
        # The second tone is whatever neutral the first is not.
        for y in range(0, min(96, height), 2):
            for x in range(0, min(96, width), 2):
                candidate = pixels[x, y]
                if _distance(candidate, keys[0]) > tolerance:
                    keys.append(candidate)
                    break
            if len(keys) > 1:
                break

    def is_background(pixel):
        return any(_distance(pixel, key) <= tolerance for key in keys)

    alpha = Image.new("L", (width, height), 255)
    ap = alpha.load()
    seen = bytearray(width * height)
    queue = deque()

    for x in range(width):
        queue.append((x, 0))
        queue.append((x, height - 1))
    for y in range(height):
        queue.append((0, y))
        queue.append((width - 1, y))

    while queue:
        x, y = queue.popleft()
        if x < 0 or y < 0 or x >= width or y >= height:
            continue
        index = y * width + x
        if seen[index]:
            continue
        seen[index] = 1
        if not is_background(pixels[x, y]):
            continue
        ap[x, y] = 0
        queue.append((x + 1, y))
        queue.append((x - 1, y))
        queue.append((x, y + 1))
        queue.append((x, y - 1))

    out = image.convert("RGBA")
    out.putalpha(alpha)
    return out


def content_box(image, threshold=8):
    """Bounding box of the non-transparent pixels."""
    alpha = image.getchannel("A")
    width, height = image.size
    ap = alpha.load()
    left, top, right, bottom = width, height, -1, -1
    for y in range(height):
        for x in range(width):
            if ap[x, y] > threshold:
                if x < left:
                    left = x
                if x > right:
                    right = x
                if y < top:
                    top = y
                if y > bottom:
                    bottom = y
    if right < left or bottom < top:
        raise ValueError("image is fully transparent after keying")
    return left, top, right + 1, bottom + 1


def square_pad(image):
    """Crop to content, then pad to a centred square without distortion."""
    box = content_box(image)
    cropped = image.crop(box)
    width, height = cropped.size
    side = max(width, height)
    canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    canvas.paste(cropped, ((side - width) // 2, (side - height) // 2))
    return canvas


def quantize(image, colors=MAX_COLORS):
    """Median-cut the OPAQUE pixels only, then restore binary alpha.

    Keying sets alpha but leaves each transparent pixel's original RGB in
    place, so the background colour is still present in the data. Quantizing
    the cropped image therefore spends palette entries on colours nobody can
    see: a white-background source burned 8 of 12 entries on near-white and
    left the root with 4 visible tones. Compacting the opaque pixels into a
    strip first means every entry is spent on a colour that is actually drawn.
    """
    alpha = image.getchannel("A")
    mask = alpha.point(lambda v: 255 if v > 8 else 0)
    box = content_box(image)
    cropped = image.crop(box).convert("RGB")
    crop_alpha = mask.crop(box)

    visible = [pixel for pixel, a in zip(cropped.get_flattened_data(),
                                         crop_alpha.get_flattened_data())
               if a > 8]
    if not visible:
        raise ValueError("image has no opaque pixels to quantize")

    strip = Image.new("RGB", (len(visible), 1))
    strip.putdata(visible)
    palette = strip.quantize(colors=colors, method=Image.MEDIANCUT,
                             dither=Image.NONE).getpalette()
    entries = [tuple(palette[i * 3:i * 3 + 3]) for i in range(colors)]

    def nearest(pixel):
        return min(entries,
                   key=lambda e: (pixel[0] - e[0]) ** 2
                   + (pixel[1] - e[1]) ** 2
                   + (pixel[2] - e[2]) ** 2)

    mapped = Image.new("RGB", cropped.size)
    mapped.putdata([nearest(p) for p in cropped.get_flattened_data()])

    out = Image.new("RGBA", image.size, (0, 0, 0, 0))
    out.paste(mapped.convert("RGBA"), (box[0], box[1]))
    out.putalpha(mask)
    return out


def convert(source, colors=MAX_COLORS, tolerance=KEY_TOLERANCE):
    """Full pipeline: key, centre, resize, quantize."""
    image = Image.open(source)
    image = key_background(image, tolerance)
    image = square_pad(image)
    image = image.resize((TARGET, TARGET), Image.NEAREST)
    image = quantize(image, colors)
    return image


def describe(image):
    """A short factual summary for the review line."""
    alpha = image.getchannel("A")
    values = set(alpha.get_flattened_data())
    opaque = sum(1 for v in alpha.get_flattened_data() if v > 8)
    colors = len({p for p in image.convert("RGB").get_flattened_data()})
    return (f"{image.size[0]}x{image.size[1]} alpha={sorted(values)} "
            f"opaque={opaque} colors={colors}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--name", required=True,
                        help="structure name, e.g. branch")
    parser.add_argument("--out", type=Path,
                        help="output PNG (default assets/decoritem_climb_NAME.png)")
    parser.add_argument("--colors", type=int, default=MAX_COLORS)
    parser.add_argument("--tolerance", type=int, default=KEY_TOLERANCE)
    parser.add_argument("--preview", type=Path,
                        help="also write an enlarged review copy")
    args = parser.parse_args(argv)

    image = convert(args.source, args.colors, args.tolerance)
    out = args.out or Path("assets") / f"decoritem_climb_{args.name}.png"
    out.parent.mkdir(parents=True, exist_ok=True)
    image.save(out)
    print(f"saved   : {out}")
    print(f"describe: {describe(image)}")
    if args.preview:
        args.preview.parent.mkdir(parents=True, exist_ok=True)
        image.resize((TARGET * 3, TARGET * 3), Image.NEAREST).save(args.preview)
        print(f"preview : {args.preview}")


if __name__ == "__main__":
    main()
