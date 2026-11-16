"""Convert a generated pixel-art texture into a device-ready 240x240 background.

The device draws backgrounds at 240x240 logical pixels and enlarges them 2x with
nearest-neighbour, so 240 is the only size that matters. Image generators return
1024x1024 or similar, which is not an integer multiple of 240, so this crops to
the largest centred multiple and box-averages down. That keeps the pixel grid
aligned instead of smearing it.

Generated textures also come back far more contrasty and saturated than the
approved crystal floor, which makes the spider hard to see. The approved floor
is the tonal reference: its central 160x160 play area has a luminance standard
deviation near 8 and a mean near 65. This tool pulls a generated texture into
that range while keeping the material's hue variation, which is where its
character actually lives.

Usage:
    python tools/import_background.py SOURCE.png --theme woodland --out DIR
    python tools/import_background.py SOURCE.png --theme woodland --out DIR --install

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

TARGET = 240
MAX_COLORS = 64
CENTER = (40, 40, 200, 200)
LUMA = (299, 587, 114)

# The approved crystal floor, measured over its central 160x160 play area.
REFERENCE_MEAN = 65.0
REFERENCE_SD = 8.1

# Contrast is what makes a floor read as flat 8-bit art, so every theme is
# pulled into crystal's calm band. Brightness is not: snow must stay bright and
# a cave must stay dark, or the theme stops being recognisable. These are the
# mean luminances the procedural floors already shipped with, so an imported
# floor keeps the brightness players already associate with the theme.
THEME_MEAN = {
    "cork": 101, "moss": 80, "leaf_litter": 100, "sand": 159, "fern": 84,
    "orchid": 85, "woodland": 66, "minimal": 169, "mushroom": 101,
    "gothic": 74, "seashell": 160, "succulent": 104, "willow": 101,
    "desert": 143, "crystal": 65, "cemetery": 86, "house": 103,
    "dark_cave": 62, "night": 64, "autumn": 107, "snow": 167, "pond": 92,
    "ruins": 117, "marsh": 85, "fairy_glade": 103, "moonflower_grove": 82,
    "cathedral": 100, "rose_courtyard": 105, "library": 90, "observatory": 79,
    "kitchen": 154, "sewing_room": 117, "witch_garden": 70, "moon_altar": 86,
    "primeval_jungle": 81, "amber_basin": 106, "neon_alley": 58,
    "circuit_garden": 69, "lunar_base": 118, "starship": 97,
    "enchanted_court": 104, "dragon_heath": 88, "lost_city": 131,
    "sunken_temple": 90, "zen_garden": 156, "bamboo_grove": 109,
    "sakura_courtyard": 121, "cloud_sanctuary": 156,
}


def theme_mean(theme):
    """The brightness a theme should keep, falling back to the reference."""
    return float(THEME_MEAN.get(theme, REFERENCE_MEAN))


def luma(pixel):
    return (LUMA[0] * pixel[0] + LUMA[1] * pixel[1] + LUMA[2] * pixel[2]) / 1000.0


def reduce_to_target(source):
    """Crop to a centred multiple of TARGET, then box-average down."""
    image = source.convert("RGB")
    width, height = image.size
    side = (min(width, height) // TARGET) * TARGET
    if side < TARGET:
        raise ValueError(
            f"source is {width}x{height}; need at least {TARGET}x{TARGET} to reduce cleanly"
        )
    left, top = (width - side) // 2, (height - side) // 2
    image = image.crop((left, top, left + side, top + side))
    return image.resize((TARGET, TARGET), Image.BOX)


def center_luma(image):
    pixels = image.load()
    x0, y0, x1, y1 = CENTER
    return [luma(pixels[x, y]) for y in range(y0, y1) for x in range(x0, x1)]


def profile(image):
    """Measure the quiet center the way the art contract does."""
    samples = center_luma(image)
    count = len(samples)
    mean = sum(samples) / count
    sd = (sum((value - mean) ** 2 for value in samples) / count) ** 0.5
    ordered = sorted(samples)
    p10 = ordered[int(count * 0.10)]
    p90 = ordered[int(count * 0.90)]
    pixels = image.load()
    x0, y0, x1, y1 = CENTER
    tones = len({pixels[x, y] for y in range(y0, y1) for x in range(x0, x1)})
    return {
        "mean": mean,
        "sd": sd,
        "p10_p90": p90 - p10,
        "span": ordered[-1] - ordered[0],
        "tones": tones,
    }


def scale_contrast(image, factor):
    """Scale luminance deviation from the image mean, uniformly.

    Uniform scaling cannot introduce a seam at the play-area boundary, which a
    region-limited adjustment would. Hue and saturation are untouched.
    """
    if factor >= 1.0:
        return image
    pixels = image.load()
    samples = [luma(pixels[x, y]) for y in range(TARGET) for x in range(TARGET)]
    mean = sum(samples) / len(samples)
    out = image.copy()
    target = out.load()
    for y in range(TARGET):
        for x in range(TARGET):
            r, g, b = pixels[x, y]
            shift = (luma((r, g, b)) - mean) * (factor - 1.0)
            target[x, y] = tuple(
                max(0, min(255, int(round(value + shift)))) for value in (r, g, b)
            )
    return out


def retarget_mean(image, mean_target):
    """Shift the whole image so its mean luminance lands on `mean_target`."""
    pixels = image.load()
    samples = [luma(pixels[x, y]) for y in range(TARGET) for x in range(TARGET)]
    shift = mean_target - sum(samples) / len(samples)
    if abs(shift) < 0.5:
        return image
    out = image.copy()
    target = out.load()
    for y in range(TARGET):
        for x in range(TARGET):
            r, g, b = pixels[x, y]
            target[x, y] = tuple(
                max(0, min(255, int(round(value + shift)))) for value in (r, g, b)
            )
    return out


def scale_saturation(image, factor):
    """Pull saturation toward grey.

    The approved floor averages 0.42 saturation; generated earth comes back
    near 0.64, which reads as flat orange mud. Desaturating lets hue variation
    carry the material instead of raw chroma.
    """
    if factor >= 1.0:
        return image
    pixels = image.load()
    out = image.copy()
    target = out.load()
    for y in range(TARGET):
        for x in range(TARGET):
            r, g, b = pixels[x, y]
            grey = luma((r, g, b))
            target[x, y] = tuple(
                max(0, min(255, int(round(grey + (value - grey) * factor))))
                for value in (r, g, b)
            )
    return out


def apply_lighting(image, strength):
    """Add the contract's single top-left light as a gentle scene-scale falloff.

    A generated flat texture has no scene-scale shading, so the ground reads as
    a swatch rather than a lit surface. This lifts the upper-left slightly and
    deepens the lower-right slightly.
    """
    if strength <= 0:
        return image
    pixels = image.load()
    out = image.copy()
    target = out.load()
    span = float(TARGET - 1)
    for y in range(TARGET):
        for x in range(TARGET):
            # -1 at the top-left corner, +1 at the bottom-right corner.
            t = ((x / span) + (y / span)) - 1.0
            shift = -t * strength
            r, g, b = pixels[x, y]
            target[x, y] = tuple(
                max(0, min(255, int(round(value + shift)))) for value in (r, g, b)
            )
    return out


def quantize(image, colors=MAX_COLORS):
    """Median-cut to a finite palette with no dithering."""
    return image.convert("RGB").quantize(
        colors=colors, method=Image.MEDIANCUT, dither=Image.Dither.NONE
    ).convert("RGB")


def convert(source, contrast=0.50, mean=REFERENCE_MEAN, light=16, saturation=0.70):
    """Full pipeline: reduce, tone-match, light, quantize, make opaque."""
    image = reduce_to_target(source)
    image = scale_contrast(image, contrast)
    image = retarget_mean(image, mean)
    image = apply_lighting(image, light)
    image = scale_saturation(image, saturation)
    image = quantize(image)
    return image.convert("RGBA")


def fit_contrast(source, mean=REFERENCE_MEAN, light=16, saturation=0.70,
                 target_sd=REFERENCE_SD, tolerance=0.6, steps=14):
    """Find the contrast factor that lands the result near the reference sd.

    A fixed factor cannot work across themes: a brick floor comes back far
    contrastier than a snowfield, so one number either flattens the snow or
    leaves the brick too busy to see the spider against. Contrast can only be
    reduced, so this searches downward from 1.0 and returns the closest fit.
    """
    best = (float("inf"), 1.0)
    low, high = 0.02, 1.0
    for _ in range(steps):
        middle = (low + high) / 2.0
        result = convert(source, middle, mean, light, saturation)
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
    return factor, convert(source, factor, mean, light, saturation)


def describe(image):
    stats = profile(image)
    pixels = image.load()
    colors = len({pixels[x, y] for y in range(TARGET) for x in range(TARGET)})
    return (
        f"colors={colors} center_tones={stats['tones']} "
        f"mean={stats['mean']:.1f} sd={stats['sd']:.1f} "
        f"p10-p90={stats['p10_p90']:.1f} span={stats['span']:.1f}"
    )


def pixel_hash(image):
    return hashlib.sha256(image.tobytes()).hexdigest()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="generated PNG or JPEG")
    parser.add_argument("--theme", required=True, help="background theme name")
    parser.add_argument("--out", type=Path, required=True, help="scratch output directory")
    parser.add_argument("--contrast", type=float, default=None,
                        help="luminance contrast multiplier (default: fitted to "
                             f"the reference sd of {REFERENCE_SD})")
    parser.add_argument("--mean", type=float, default=None,
                        help="target mean luminance (default: the theme's own "
                             "shipped brightness, else "
                             f"{REFERENCE_MEAN})")
    parser.add_argument("--light", type=float, default=16,
                        help="top-left lighting strength (default 16)")
    parser.add_argument("--saturation", type=float, default=0.70,
                        help="saturation multiplier (default 0.70)")
    parser.add_argument("--install", action="store_true",
                        help="write into assets/sources/ as the approved source")
    args = parser.parse_args(argv)
    mean = theme_mean(args.theme) if args.mean is None else args.mean

    with Image.open(args.source) as raw:
        raw.load()
        if args.contrast is None:
            contrast, image = fit_contrast(raw, mean, args.light, args.saturation)
        else:
            contrast = args.contrast
            image = convert(raw, contrast, mean, args.light, args.saturation)

    print(f"source : {args.source.name} {raw.size}")
    print(f"target : mean={mean:.1f} contrast={contrast:.3f} "
          f"light={args.light} saturation={args.saturation}")
    print(f"result : {describe(image)}")
    print(f"sha256 : {pixel_hash(image)}")

    if args.install:
        SOURCES.mkdir(parents=True, exist_ok=True)
        destination = SOURCES / f"{args.theme}_floor.png"
        if destination.exists():
            print(f"refusing to overwrite {destination}", file=sys.stderr)
            return 1
        image.save(destination)
        print(f"installed: {destination}")
    else:
        args.out.mkdir(parents=True, exist_ok=True)
        destination = args.out / f"background_{args.theme}_0.png"
        image.save(destination)
        print(f"wrote  : {destination}")
        print("Review at native scale before installing; nothing was adopted.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
