"""Convert a generated pixel-art frame into a device-ready 240x240 border.

Borders are authored at 120x120 logical pixels and enlarged 2x with
nearest-neighbour, so the published 240x240 frame is exactly the source with
every pixel doubled. The art contract requires the play opening to be
transparent across [16,224) at 240x240, which is [8,112) at 120x120, so the
frame's rim is exactly 8 logical pixels thick.

Image generators draw a frame with a much thicker rim than that, and they fill
the middle with a picture. This tool therefore takes the outer band of the
generated frame, nine-slices it down to the contract's 8px rim, and forces
everything inside the opening to alpha zero. That is the same geometry the
approved crystal border uses, so a generated frame lands on the same grid.

Usage:
    python tools/import_border.py SOURCE.png --theme snow --out DIR
    python tools/import_border.py SOURCE.png --theme snow --out DIR --install

Without --install nothing is written into assets/; the tool prints the measured
profile and the pixel hash so the result can be reviewed before it is adopted.
"""

import argparse
import hashlib
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ROOT / "assets" / "sources"

TARGET = 120
SCALE = 2
RIM = 8
SOURCE_RIM = 18
MAX_COLORS = 16
LUMA = (299, 587, 114)

# The approved crystal border, measured over its opaque pixels.
REFERENCE_MEAN = 69.7
REFERENCE_SD = 47.2

# A frame is decorative, so it is allowed far more contrast than a floor: the
# approved crystal border already runs at sd 47 and reads as carved stone. The
# ceiling only exists to stop a generated frame arriving as a black-and-white
# silhouette, so it sits well above crystal rather than at it. Fitting to
# crystal's exact sd instead flattens the shading that makes the frame read as
# three-dimensional.
SD_CEILING = 80.0


def luma(pixel):
    return (LUMA[0] * pixel[0] + LUMA[1] * pixel[1] + LUMA[2] * pixel[2]) / 1000.0


def content_box(image, threshold=14):
    """The bounding box of everything that is not near-black.

    Generators usually leave a black margin around the frame. Cropping to the
    content first means the nine-slice measures the band the generator actually
    drew, instead of including that margin as part of the rim.
    """
    pixels = image.load()
    width, height = image.size
    xs, ys = [], []
    for y in range(height):
        for x in range(width):
            if luma(pixels[x, y]) > threshold:
                xs.append(x)
                ys.append(y)
    if not xs:
        raise ValueError("generated frame is entirely black")
    return (min(xs), min(ys), max(xs) + 1, max(ys) + 1)


def detect_opening(image, tolerance=10):
    """Find the hollow middle of a generated frame.

    The opening is not reliably black: generators fill it with white, black, or
    a flat tint. What it always is, is *flat* — one uniform colour with no
    texture — while the band is shaded. It also always contains the centre of
    the frame, so this floods outward from the centre pixel and takes the
    bounding box of everything reachable at a similar colour. That is far more
    reliable than guessing a seed colour, which can land on the band.
    """
    pixels = image.load()
    seed = pixels[TARGET // 2, TARGET // 2]

    def matches(pixel):
        return all(abs(pixel[channel] - seed[channel]) <= tolerance
                   for channel in range(3))

    seen = [[False] * TARGET for _ in range(TARGET)]
    stack = [(TARGET // 2, TARGET // 2)]
    seen[TARGET // 2][TARGET // 2] = True
    area = 0
    left = right = TARGET // 2
    top = bottom = TARGET // 2
    while stack:
        x, y = stack.pop()
        area += 1
        left, right = min(left, x), max(right, x)
        top, bottom = min(top, y), max(bottom, y)
        for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if 0 <= nx < TARGET and 0 <= ny < TARGET and not seen[ny][nx]:
                if matches(pixels[nx, ny]):
                    seen[ny][nx] = True
                    stack.append((nx, ny))

    if area < TARGET * TARGET * 0.25:
        raise ValueError("generated frame has no hollow middle")
    if area > TARGET * TARGET * 0.90:
        raise ValueError("generated frame has no band around its middle")
    return (left, top, right + 1, bottom + 1)


def fit_frame(image, rim=RIM):
    """Crop to the drawn frame, then fit its band to exactly `rim` pixels.

    This works on the source at its own resolution, not on a pre-reduced copy.
    Reducing 1024 to 120 first averages about eight source pixels per output
    pixel, which smears a band that is only eight pixels wide once published.
    Slicing at full resolution and reducing each band piece once keeps the
    generator's detail instead of a double-averaged blur.
    """
    box = content_box(image)
    frame = image.crop(box)
    width, height = frame.size
    side = min(width, height)
    frame = frame.crop((0, 0, side, side))

    probe = frame.resize((TARGET, TARGET), Image.Resampling.BOX)
    left, top, right, bottom = detect_opening(probe)
    measured = min(left, top, TARGET - right, TARGET - bottom)
    if measured < 1:
        raise ValueError("generated frame has no measurable band")

    band = max(1, round(side * measured / TARGET))
    if band * 2 >= side:
        raise ValueError(
            f"generated frame band is {band}px of {side}; the middle is not hollow"
        )
    edges = (0, band, side - band, side)
    target = (0, rim, TARGET - rim, TARGET)
    out = Image.new("RGB", (TARGET, TARGET))
    for y in range(3):
        for x in range(3):
            piece = frame.crop((edges[x], edges[y], edges[x + 1], edges[y + 1]))
            size = (target[x + 1] - target[x], target[y + 1] - target[y])
            # Nearest when the piece is being enlarged or is already at size,
            # so a crisp band stays crisp; box-average only when shrinking.
            shrinking = piece.width > size[0] or piece.height > size[1]
            resample = (Image.Resampling.BOX if shrinking
                        else Image.Resampling.NEAREST)
            out.paste(piece.resize(size, resample), (target[x], target[y]))
    return out, band


def nine_slice(image, source_rim=SOURCE_RIM, target_rim=RIM):
    """Compress the generated rim band down to the contract's 8px rim.

    The corners are kept square and the four edge bands are resized
    independently, so a motif that sits mid-edge stays mid-edge instead of
    being stretched around the corner.
    """
    if not 0 < target_rim < TARGET // 2:
        raise ValueError(f"target rim {target_rim} does not fit a {TARGET}px border")
    if not 0 < source_rim < TARGET // 2:
        raise ValueError(f"source rim {source_rim} does not fit a {TARGET}px border")
    source_edges = (0, source_rim, TARGET - source_rim, TARGET)
    target_edges = (0, target_rim, TARGET - target_rim, TARGET)
    out = Image.new("RGB", (TARGET, TARGET))
    for y in range(3):
        for x in range(3):
            box = (source_edges[x], source_edges[y],
                   source_edges[x + 1], source_edges[y + 1])
            size = (target_edges[x + 1] - target_edges[x],
                    target_edges[y + 1] - target_edges[y])
            out.paste(image.crop(box).resize(size, Image.Resampling.NEAREST),
                      (target_edges[x], target_edges[y]))
    return out


def opening_mask(rim=RIM):
    """Binary alpha: opaque rim, fully transparent play opening."""
    mask = Image.new("L", (TARGET, TARGET), 255)
    mask.paste(0, (rim, rim, TARGET - rim, TARGET - rim))
    return mask


def opaque_luma(image):
    pixels = image.load()
    return [luma(pixels[x, y]) for y in range(TARGET) for x in range(TARGET)
            if pixels[x, y][3]]


def profile(image):
    """Measure the rim the way the art contract does: opaque pixels only."""
    samples = opaque_luma(image)
    if not samples:
        raise ValueError("border has no opaque pixels")
    count = len(samples)
    mean = sum(samples) / count
    sd = (sum((value - mean) ** 2 for value in samples) / count) ** 0.5
    ordered = sorted(samples)
    pixels = image.load()
    colors = len({pixels[x, y] for y in range(TARGET) for x in range(TARGET)
                  if pixels[x, y][3]})
    return {
        "mean": mean,
        "sd": sd,
        "span": ordered[-1] - ordered[0],
        "colors": colors,
        "opaque": count,
    }


def scale_contrast(image, factor):
    """Scale luminance deviation from the rim mean, uniformly."""
    if factor >= 1.0:
        return image
    pixels = image.load()
    samples = opaque_luma(image)
    mean = sum(samples) / len(samples)
    out = image.copy()
    target = out.load()
    for y in range(TARGET):
        for x in range(TARGET):
            r, g, b, a = pixels[x, y]
            if not a:
                continue
            shift = (luma((r, g, b)) - mean) * (factor - 1.0)
            target[x, y] = tuple(
                max(0, min(255, int(round(value + shift)))) for value in (r, g, b)
            ) + (a,)
    return out


def retarget_mean(image, mean_target):
    """Shift the rim so its mean luminance lands on `mean_target`."""
    pixels = image.load()
    samples = opaque_luma(image)
    shift = mean_target - sum(samples) / len(samples)
    if abs(shift) < 0.5:
        return image
    out = image.copy()
    target = out.load()
    for y in range(TARGET):
        for x in range(TARGET):
            r, g, b, a = pixels[x, y]
            if not a:
                continue
            target[x, y] = tuple(
                max(0, min(255, int(round(value + shift)))) for value in (r, g, b)
            ) + (a,)
    return out


def quantize(image, colors=MAX_COLORS):
    """Median-cut the rim to a finite palette, then restore binary alpha.

    Dithering is disabled: the contract forbids isolated bright or dark pixels,
    and a dithered 16-colour ramp is exactly that.
    """
    alpha = image.getchannel("A")
    reduced = image.convert("RGB").quantize(
        colors=colors, method=Image.MEDIANCUT, dither=Image.Dither.NONE
    ).convert("RGB")
    out = reduced.convert("RGBA")
    out.putalpha(alpha)
    return out


def convert(source, mean=REFERENCE_MEAN, contrast=1.0, rim=RIM):
    """Full pipeline: fit the drawn frame, tone-match, quantize, cut the opening."""
    image, _ = fit_frame(source.convert("RGB"), rim)
    image = image.convert("RGBA")
    image.putalpha(opening_mask(rim))
    image = scale_contrast(image, contrast)
    image = retarget_mean(image, mean)
    image = quantize(image)
    image.putalpha(opening_mask(rim))
    return image


def fit_contrast(source, mean=REFERENCE_MEAN, rim=RIM,
                 target_sd=SD_CEILING, tolerance=1.5, steps=12):
    """Reduce contrast only as far as the ceiling requires.

    Contrast can only be reduced, so this searches downward from 1.0 and stops
    at the first factor that brings the rim under `target_sd`. A frame is
    decorative, so it keeps as much of its own shading as the ceiling allows;
    fitting to an exact sd would flatten the carved look the frame needs.
    """
    untouched = convert(source, mean, 1.0, rim)
    if profile(untouched)["sd"] <= target_sd:
        return 1.0, untouched
    best = (float("inf"), 1.0)
    low, high = 0.05, 1.0
    for _ in range(steps):
        middle = (low + high) / 2.0
        result = convert(source, mean, middle, rim)
        sd = profile(result)["sd"]
        error = abs(sd - target_sd)
        if error < best[0]:
            best = (error, middle)
        if error <= tolerance:
            return middle, result
        if sd > target_sd:
            high = middle
        else:
            low = middle
    factor = best[1]
    return factor, convert(source, mean, factor, rim)


def shipped_mean(theme):
    """The brightness the procedural border for `theme` already ships with."""
    try:
        import terrain_art
        image = terrain_art.border(theme).convert("RGBA")
    except Exception:
        return REFERENCE_MEAN
    samples = [luma(pixel) for pixel in image.get_flattened_data() if pixel[3]]
    if not samples:
        return REFERENCE_MEAN
    return sum(samples) / len(samples)


def describe(image):
    stats = profile(image)
    return (
        f"colors={stats['colors']} opaque={stats['opaque']} "
        f"mean={stats['mean']:.1f} sd={stats['sd']:.1f} span={stats['span']:.1f}"
    )


def published(image):
    """The 240x240 frame the device actually draws."""
    return image.resize((TARGET * SCALE, TARGET * SCALE), Image.Resampling.NEAREST)


def pixel_hash(image):
    return hashlib.sha256(published(image).tobytes()).hexdigest()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="generated PNG or JPEG")
    parser.add_argument("--theme", required=True, help="border theme name")
    parser.add_argument("--out", type=Path, required=True, help="scratch output directory")
    parser.add_argument("--mean", type=float, default=None,
                        help="target rim mean luminance (default: the theme's own "
                             "shipped border brightness)")
    parser.add_argument("--contrast", type=float, default=None,
                        help="luminance contrast multiplier (default: fitted to "
                             f"the reference sd of {REFERENCE_SD})")
    parser.add_argument("--rim", type=int, default=RIM,
                        help=f"published rim thickness in logical pixels (default {RIM})")
    parser.add_argument("--install", action="store_true",
                        help="write into assets/sources/ as the approved source")
    args = parser.parse_args(argv)
    mean = shipped_mean(args.theme) if args.mean is None else args.mean

    with Image.open(args.source) as raw:
        raw.load()
        if args.contrast is None:
            contrast, image = fit_contrast(raw, mean, args.rim)
        else:
            contrast = args.contrast
            image = convert(raw, mean, contrast, args.rim)

    print(f"source : {args.source.name} {raw.size}")
    print(f"target : mean={mean:.1f} contrast={contrast:.3f} rim={args.rim}")
    print(f"result : {describe(image)}")
    print(f"sha256 : {pixel_hash(image)}")

    if args.install:
        SOURCES.mkdir(parents=True, exist_ok=True)
        destination = SOURCES / f"{args.theme}_border.png"
        if destination.exists():
            print(f"refusing to overwrite {destination}", file=sys.stderr)
            return 1
        image.save(destination)
        print(f"installed: {destination}")
    else:
        args.out.mkdir(parents=True, exist_ok=True)
        destination = args.out / f"border_{args.theme}_0.png"
        published(image).save(destination)
        print(f"wrote  : {destination}")
        print("Review at native scale before installing; nothing was adopted.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
