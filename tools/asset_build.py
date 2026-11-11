#!/usr/bin/env python3
"""Generate and validate the complete deterministic Spidarium PNG set."""

import hashlib
import os
import re
import struct
import subprocess
import sys

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("ERROR: Pillow is required. Install it with: pip install Pillow")


SPIDER_COUNT = 39
PREY_SPECIES = ("fly", "cricket", "moth", "beetle", "roach", "mealworm", "fruitfly", "waxworm")
ASSET_COUNT = 201
NON_SPIDER_COUNT = 162
BACKGROUNDS = (
    "cork", "moss", "leaf_litter", "sand", "fern", "orchid", "woodland", "minimal",
    "mushroom", "gothic", "seashell", "succulent", "willow", "desert", "crystal",
    "cemetery", "house", "dark_cave", "night", "autumn", "snow", "pond", "ruins",
    "marsh",
)
PREY_CLIPS = (("walk", 2), ("flee", 2), ("dead", 1))
UI_ICONS = (
    "feed", "clean", "play", "sleep", "decor", "status", "back",
    "hunger", "mood", "energy", "hygiene", "xp", "lock", "daily", "treats",
    "backgrounds", "achievements", "prey", "settings", "wifi", "sd", "about",
    "battery", "display", "sound", "auto_feed", "screenshot", "reset", "volume",
    "music",
)
ACHIEVEMENTS = (
    "first_meal", "first_pet", "first_clean", "first_play", "first_hunt",
    "first_molt", "ten_meals", "adult", "level_5", "survived_sick",
    "first_perfect",
)
ACHIEVEMENT_ACCENTS = (
    0xE9A44F, 0xE781A5, 0x68C8C0, 0x72C58B, 0xD5634D, 0xC6B9DE,
    0xD8AE43, 0xA8C6C7, 0x7C9CFF, 0x58D68D, 0xA7D8E0,
)
PREY_COLORS = (
    (0xC78B48, 0x623D2D, 0xE8D7B3),
    (0xA9A05A, 0x575032, 0xD8C98A),
    (0xB6A08E, 0x62544B, 0xE1D2BB),
    (0x416D68, 0x263F40, 0x8DB8A0),
    (0x98613D, 0x513423, 0xC59558),
    (0xCBA866, 0x795338, 0xF0D399),
    (0xD2AA70, 0x67533F, 0xDDDCC6),
    (0xEADCB2, 0xAB9471, 0xFFF0D4),
)
GIFTS = (
    "pebble", "leaf", "twig", "acorn", "moss", "sap", "flower", "crystal",
    "amber", "coral", "silk", "bone", "pearl", "ruby", "sapphire", "emerald",
    "diamond", "crown",
)
DECOR = ("rock_0", "rock_1", "plant_0", "plant_1", "wood_log")
DECOR_ITEMS = (
    "rock_small", "rock_large", "pebbles", "log", "bark", "fern", "moss_clump",
    "succulent", "flower_white", "flower_pink", "mushroom", "shell", "crystal",
    "leaf_pile", "twig", "bone", "fern_pot", "acorn_hide", "driftwood_arch",
    "web_hammock", "dew_pool", "mushroom_pair", "flower_planter", "crystal_cluster",
    "shell_cove", "twig_ladder", "moss_stone", "seed_pod", "silk_tent",
    "leaf_umbrella", "bone_arch", "fossil", "lantern_cap", "stone_bridge",
)
PIXEL_SCALE = 2
MAX_VISIBLE_COLORS = 12
MAX_BACKGROUND_COLORS = 64


def expected_dimensions():
    dimensions = {}
    for view in ("e", "n", "s"):
        for clip, frames in (
            ("walk", 4), ("idle", 2), ("eat", 2), ("sleep", 1),
            ("pounce", 2), ("molt", 2),
        ):
            for frame in range(frames):
                dimensions[f"spider_{view}_{clip}_{frame}.png"] = (96, 96)
    for species in PREY_SPECIES:
        for clip, frames in PREY_CLIPS:
            for frame in range(frames):
                dimensions[f"prey_{species}_{clip}_{frame}.png"] = (48, 48)
    for icon in UI_ICONS:
        dimensions[f"ui_{icon}.png"] = (48, 48)
    for achievement in ACHIEVEMENTS:
        dimensions[f"achievement_{achievement}.png"] = (48, 48)
    for gift in GIFTS:
        dimensions[f"gift_{gift}.png"] = (32, 32)
    dimensions.update({
        "decor_rock_0.png": (48, 32),
        "decor_rock_1.png": (40, 28),
        "decor_plant_0.png": (48, 64),
        "decor_plant_1.png": (52, 56),
        "decor_wood_log.png": (96, 36),
    })
    dimensions.update({
        f"decoritem_{name}.png": (64, 64) for name in DECOR_ITEMS
    })
    dimensions.update({
        f"background_{name}_0.png": (240, 240) for name in BACKGROUNDS
    })
    return dimensions


def hashes(directory, names):
    results = {}
    for name in sorted(names):
        with open(os.path.join(directory, name), "rb") as asset:
            results[name] = hashlib.sha256(asset.read()).hexdigest()
    return results


def read_bytes(path):
    with open(path, "rb") as asset:
        return asset.read()


def validate_pixel_art(image, name):
    """Reject smoothing, off-grid detail, and unrestricted color ramps.

    Backgrounds use native 240x240 pixels and finite material ramps, not the
    sprite grid. Their central 160x160 area stays quiet for spider and decor.

    Spider frames are exempt from the 2x2 grid rule because they are a direct
    export of main/cspider.c, which writes single pixels into a 96x96 canvas.
    Forcing them onto a 2x2 grid would make the published art disagree with the
    spider the panel actually draws.
    """
    width, height = image.size
    is_background = name.startswith("background_")
    is_spider = name.startswith("spider_")
    if not is_background and (width % PIXEL_SCALE or height % PIXEL_SCALE):
        raise ValueError(f"{name}: dimensions must align to the 2x2 pixel grid")
    pixels = image.load()
    colors = set()
    for y in range(height):
        for x in range(width):
            pixel = pixels[x, y]
            if pixel[3] not in (0, 255):
                raise ValueError(f"{name}: non-binary alpha {pixel[3]} at {(x, y)}")
            if is_background and pixel[3] != 255:
                raise ValueError(f"{name}: background must be fully opaque at {(x, y)}")
            if pixel[3]:
                colors.add(pixel)
            if is_background or is_spider:
                continue
            anchor = pixels[x - x % PIXEL_SCALE, y - y % PIXEL_SCALE]
            if pixel != anchor:
                raise ValueError(f"{name}: nonuniform 2x2 pixel block at {(x, y)}")
    if not colors:
        raise ValueError(f"{name}: empty pixel-art silhouette")
    if not is_background and len(colors) > MAX_VISIBLE_COLORS:
        raise ValueError(
            f"{name}: {len(colors)} visible RGBA colors exceeds {MAX_VISIBLE_COLORS}"
        )
    if name.startswith(("decoritem_", "gift_")) and len(colors) < 8:
        raise ValueError(f"{name}: furnishings need at least 8 visible material tones")
    minimum = (6 if "_dead_" in name else 9) if name.startswith("prey_") else (
        8 if name.startswith("achievement_") else 5 if name.startswith("ui_") else 0)
    if len(colors) < minimum:
        raise ValueError(f"{name}: shaded materials need at least {minimum} visible tones")
    if is_background:
        if len(colors) > MAX_BACKGROUND_COLORS:
            raise ValueError(f"{name}: background exceeds {MAX_BACKGROUND_COLORS} colors")
        center = {color for _, color in image.crop((40, 40, 200, 200)).getcolors(160 * 160)}
        if len(center) > 3:
            raise ValueError(f"{name}: quiet center exceeds 3 substrate tones")
        luminances = [(r * 299 + g * 587 + b * 114) // 1000 for r, g, b, _ in center]
        if max(luminances) - min(luminances) > 24:
            raise ValueError(f"{name}: quiet center luminance span exceeds 24/255")
    return len(colors)


def rgba(value):
    return tuple((value >> shift) & 255 for shift in (16, 8, 0)) + (255,)


def logical_pixels(directory, name):
    with Image.open(os.path.join(directory, name)) as image:
        return image.resize((24, 24), Image.Resampling.NEAREST)


def silhouette_components(image):
    remaining = {
        (x, y) for y in range(image.height) for x in range(image.width)
        if image.getpixel((x, y))[3]
    }
    components = []
    while remaining:
        pending = [remaining.pop()]
        component = set(pending)
        while pending:
            x, y = pending.pop()
            for neighbor in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if neighbor in remaining:
                    remaining.remove(neighbor)
                    component.add(neighbor)
                    pending.append(neighbor)
        components.append(component)
    return sorted(components, key=len, reverse=True)


def validate_achievements(directory):
    content_hashes = set()
    silhouettes = set()
    reused = {
        logical_pixels(directory, f"ui_{name}.png").tobytes() for name in UI_ICONS
    }
    for name in GIFTS:
        with Image.open(os.path.join(directory, f"gift_{name}.png")) as image:
            reused.add(image.resize((24, 24), Image.Resampling.NEAREST).tobytes())
    for name, color in zip(ACHIEVEMENTS, ACHIEVEMENT_ACCENTS):
        filename = f"achievement_{name}.png"
        image = logical_pixels(directory, filename)
        pixels = [image.getpixel((x, y)) for y in range(24) for x in range(24)]
        content = image.tobytes()
        digest = hashlib.sha256(content).digest()
        mask = image.getchannel("A").tobytes()
        if digest in content_hashes or mask in silhouettes or content in reused:
            raise ValueError(f"{filename}: reused badge content or silhouette")
        content_hashes.add(digest)
        silhouettes.add(mask)
        visible = sum(pixel[3] != 0 for pixel in pixels)
        if not 70 <= visible <= 350 or pixels.count(rgba(color)) < 20:
            raise ValueError(f"{filename}: badge motif/accent is not visibly substantial")
        if image.getpixel((0, 0))[3] or image.getpixel((23, 23))[3]:
            raise ValueError(f"{filename}: badge lost transparent margin")


def validate_splatters(directory):
    silhouettes = set()
    for variant, (species, colors) in enumerate(zip(PREY_SPECIES, PREY_COLORS)):
        filename = f"prey_{species}_dead_0.png"
        image = logical_pixels(directory, filename)
        body, dark, highlight = map(rgba, colors)
        pixels = [image.getpixel((x, y)) for y in range(24) for x in range(24)]
        if image.getpixel((12, 12)) != body or image.getpixel((8, 12)) != highlight:
            raise ValueError(f"{filename}: missing species-colored puddle/highlight")
        if image.getpixel((14, 14)) != dark:
            raise ValueError(f"{filename}: missing puddle shading")
        for position in ((2 + variant % 2, 5), (20, 8 + variant % 3),
                         (18 - variant % 3, 20)):
            if image.getpixel(position) != body:
                raise ValueError(f"{filename}: missing organic droplet at {position}")
        if image.getpixel((0, 0))[3] or image.getpixel((12, 6))[3]:
            raise ValueError(f"{filename}: puddle/droplets lost transparent separation")
        components = silhouette_components(image)
        if len(components) < 4 or len(components[0]) < 90 or pixels.count(body) < 70:
            raise ValueError(f"{filename}: splatter must have a broad puddle and detached drops")
        bounds = image.getbbox()
        if bounds is None or bounds[2] - bounds[0] < 18 or bounds[3] - bounds[1] < 15:
            raise ValueError(f"{filename}: splatter footprint is too small")
        mask = image.getchannel("A").tobytes()
        if mask in silhouettes:
            raise ValueError(f"{filename}: species splatter silhouette is reused")
        silhouettes.add(mask)
        for clip, frames in PREY_CLIPS:
            if clip == "dead":
                continue
            for frame in range(frames):
                living = logical_pixels(directory, f"prey_{species}_{clip}_{frame}.png")
                difference = sum(a != b for a, b in zip(
                    mask, living.getchannel("A").tobytes()
                ))
                if difference < 70:
                    raise ValueError(f"{filename}: resembles intact {clip}_{frame} prey")


def validate(directory):
    expected = expected_dimensions()
    actual = {name for name in os.listdir(directory) if name.lower().endswith(".png")}
    if actual != set(expected):
        missing = sorted(set(expected) - actual)
        extra = sorted(actual - set(expected))
        raise ValueError(f"PNG name/count mismatch: missing={missing}, extra={extra}")
    if len(actual) != ASSET_COUNT:
        raise ValueError(f"Expected {ASSET_COUNT} PNG assets, found {len(actual)}")
    non_spiders = sum(not name.startswith("spider_") for name in actual)
    if non_spiders != NON_SPIDER_COUNT:
        raise ValueError(f"Expected {NON_SPIDER_COUNT} non-spider PNGs, found {non_spiders}")

    for name, dimensions in expected.items():
        path = os.path.join(directory, name)
        with Image.open(path) as image:
            image.verify()
        with Image.open(path) as image:
            if image.format != "PNG":
                raise ValueError(f"{name}: expected PNG, got {image.format}")
            if image.mode != "RGBA":
                raise ValueError(f"{name}: expected RGBA, got {image.mode}")
            if image.size != dimensions:
                raise ValueError(f"{name}: expected {dimensions}, got {image.size}")
            if not name.startswith("spider_"):
                validate_pixel_art(image, name)
    validate_achievements(directory)
    validate_splatters(directory)
    return expected


def decode_qoi_rgba(data):
    marker = data[-8:]
    zero_marker = marker == b"\0" * 8
    if len(data) < 22 or data[:4] != b"qoif" or not (
        zero_marker or marker == b"\0\0\0\0\0\0\0\1"
    ):
        raise ValueError("invalid QOI signature, length, or end marker")

    width, height = struct.unpack_from(">II", data, 4)
    channels = data[12]
    if not width or not height or channels not in (3, 4) or data[13] not in (0, 1):
        raise ValueError("invalid QOI header")

    pixels = width * height
    index = [(0, 0, 0, 0)] * 64
    r, g, b, a = 0, 0, 0, 255
    cursor = 14
    end = len(data) - 8
    run = 0
    output = bytearray()

    def byte(value):
        return value & 0xFF

    for pixel in range(pixels):
        if run:
            run -= 1
        else:
            if cursor >= end:
                if not zero_marker or cursor != end or pixel != pixels - 1:
                    raise ValueError("QOI stream ended before all pixels")
            else:
                op = data[cursor]
                cursor += 1
                if op == 0xFE:
                    if cursor + 3 > end:
                        raise ValueError("truncated QOI RGB opcode")
                    r, g, b = data[cursor:cursor + 3]
                    cursor += 3
                elif op == 0xFF:
                    if cursor + 4 > end:
                        raise ValueError("truncated QOI RGBA opcode")
                    r, g, b, a = data[cursor:cursor + 4]
                    cursor += 4
                elif op & 0xC0 == 0:
                    r, g, b, a = index[op & 0x3F]
                elif op & 0xC0 == 0x40:
                    r = byte(r + ((op >> 4) & 3) - 2)
                    g = byte(g + ((op >> 2) & 3) - 2)
                    b = byte(b + (op & 3) - 2)
                elif op & 0xC0 == 0x80:
                    if cursor >= end:
                        raise ValueError("truncated QOI luma opcode")
                    next_byte = data[cursor]
                    cursor += 1
                    dg = (op & 0x3F) - 32
                    r = byte(r + dg + (next_byte >> 4) - 8)
                    g = byte(g + dg)
                    b = byte(b + dg + (next_byte & 0x0F) - 8)
                else:
                    run = op & 0x3F
                    if run > pixels - pixel - 1:
                        raise ValueError("QOI run exceeds image dimensions")

        output.extend((r, g, b, a))
        hash_slot = (r * 3 + g * 5 + b * 7 + a * 11) & 0x3F
        index[hash_slot] = (r, g, b, a)

    if cursor != end:
        raise ValueError(f"unexpected {end - cursor} trailing QOI payload bytes")
    return width, height, bytes(output)


def validate_packed_assets(path, expected_dimensions, asset_dir):
    with open(path, "rb") as packed:
        image = packed.read()
    if len(image) < 32 or image[:4] != b"MMAP":
        raise ValueError(f"{path}: invalid MMAP image header")

    version, name_len, files, checksum, payload_len = struct.unpack_from("<5I", image, 4)
    if version != 0x00010000:
        raise ValueError(f"{path}: unsupported MMAP version {version:#x}")
    table_len = files * (name_len + 12)
    if 32 + payload_len != len(image) or table_len > payload_len:
        raise ValueError(f"{path}: inconsistent MMAP payload/table length")
    table = image[32:32 + table_len]
    if sum(image[32:32 + payload_len]) & 0xFFFF != checksum:
        raise ValueError(f"{path}: MMAP payload checksum mismatch")

    payload = image[32 + table_len:]
    expected_names = {name[:-4] + ".qoi": size for name, size in expected_dimensions.items()}
    seen = set()
    ranges = []
    stride = name_len + 12

    for index in range(files):
        entry = table[index * stride:(index + 1) * stride]
        raw_name = entry[:name_len].split(b"\0", 1)[0]
        try:
            name = raw_name.decode("ascii")
        except UnicodeDecodeError as error:
            raise ValueError(f"{path}: non-ASCII asset name at index {index}") from error
        asset_size, offset, width, height = struct.unpack_from("<IIHH", entry, name_len)
        if name not in expected_names or name in seen:
            raise ValueError(f"{path}: unexpected or duplicate packed asset {name!r}")
        seen.add(name)
        if (width, height) != expected_names[name]:
            raise ValueError(f"{name}: packed dimensions {(width, height)} do not match PNG")
        stored_size = asset_size + 2
        if asset_size <= 0 or offset + stored_size > len(payload):
            raise ValueError(f"{name}: invalid packed asset size or offset")
        ranges.append((offset, offset + stored_size, name))
        if payload[offset:offset + 2] != b"\x5a\x5a":
            raise ValueError(f"{name}: missing mmap 0x5a5a file marker")
        qoi = payload[offset + 2:offset + stored_size]
        try:
            qoi_width, qoi_height, decoded = decode_qoi_rgba(qoi)
        except ValueError as error:
            raise ValueError(f"{name}: {error}") from error
        if (qoi_width, qoi_height) != (width, height):
            raise ValueError(f"{name}: QOI header dimensions do not match packed table")
        png_name = name[:-4] + ".png"
        with Image.open(os.path.join(asset_dir, png_name)) as source:
            expected_rgba = source.convert("RGBA").tobytes()
        if decoded != expected_rgba:
            raise ValueError(f"{name}: packed QOI pixels differ from source PNG")

    missing = sorted(set(expected_names) - seen)
    if missing:
        raise ValueError(f"{path}: packed asset table is missing {missing}")

    previous_end = 0
    for start, end, name in sorted(ranges):
        if start < previous_end:
            raise ValueError(f"{name}: packed asset data overlaps a previous entry")
        if end > len(payload):
            raise ValueError(f"{name}: packed data exceeds payload")
        previous_end = end
    print(f"PACKED: {files} MMAP entries, QOI dimensions/opcodes/pixels verified")


def validate_runtime_mappings(root):
    with open(os.path.join(root, "main", "sprites.c"), encoding="utf-8") as source:
        mappings = source.read()
    required = []
    for species in PREY_SPECIES:
        required.extend(
            f"MMAP_STORAGE_PREY_{species.upper()}_{clip.upper()}_0_QOI"
            for clip, _ in PREY_CLIPS
        )
    required.extend(f"MMAP_STORAGE_GIFT_{name.upper()}_QOI" for name in GIFTS)
    required.extend(f"MMAP_STORAGE_DECOR_{name.upper()}_QOI" for name in DECOR)
    required.extend(f"MMAP_STORAGE_DECORITEM_{name.upper()}_QOI" for name in DECOR_ITEMS)
    required.extend(f"MMAP_STORAGE_UI_{name.upper()}_QOI" for name in UI_ICONS)
    required.extend(f"MMAP_STORAGE_ACHIEVEMENT_{name.upper()}_QOI" for name in ACHIEVEMENTS)
    required.extend(f"MMAP_STORAGE_BACKGROUND_{name.upper()}_0_QOI" for name in BACKGROUNDS)
    missing = [name for name in required if name not in mappings]
    if missing:
        raise ValueError(f"Assets missing runtime index mappings: {missing}")

    def named_entries(array_name):
        match = re.search(
            rf"static const icon_entry_t {array_name}\[\]\s*=\s*\{{(.*?)\n\}};",
            mappings, re.DOTALL,
        )
        if not match:
            raise ValueError(f"sprites.c is missing the {array_name} catalogue")
        return re.findall(r'\{\s*"([^"]+)"\s*,\s*(MMAP_STORAGE_\w+)\s*\}', match.group(1))

    expected_decor = [
        (name, f"MMAP_STORAGE_DECORITEM_{name.upper()}_QOI") for name in DECOR_ITEMS
    ]
    if named_entries("k_decor_items") != expected_decor:
        raise ValueError("Placeable decor IDs, names and sprite mappings are out of sync")
    expected_icons = [
        (name, f"MMAP_STORAGE_UI_{name.upper()}_QOI") for name in UI_ICONS
    ]
    if named_entries("k_icons") != expected_icons:
        raise ValueError("UI icon names and sprite mappings are out of sync")

    background_match = re.search(
        r"static const int backgrounds\[\]\s*=\s*\{(.*?)\n\s*\};",
        mappings, re.DOTALL,
    )
    expected_backgrounds = [
        f"MMAP_STORAGE_BACKGROUND_{name.upper()}_0_QOI" for name in BACKGROUNDS
    ]
    if not background_match or re.findall(
        r"MMAP_STORAGE_BACKGROUND_\w+_0_QOI", background_match.group(1)
    ) != expected_backgrounds:
        raise ValueError("Background IDs and sprite mappings are out of sync")

    with open(os.path.join(root, "main", "decor.h"), encoding="utf-8") as source:
        decor_header = source.read()
    enum_match = re.search(
        r"typedef enum\s*\{(.*?)\}\s*decor_kind_t\s*;", decor_header, re.DOTALL
    )
    if not enum_match:
        raise ValueError("decor.h is missing decor_kind_t")
    enum_ids = re.findall(r"^\s*(DECOR_\w+)\s*(?:=\s*\d+)?\s*,", enum_match.group(1), re.MULTILINE)
    if enum_ids != [f"DECOR_{name.upper()}" for name in DECOR_ITEMS] + ["DECOR_COUNT"]:
        raise ValueError("Decor enum order does not match the sprite/catalogue order")
    with open(os.path.join(root, "main", "decor.c"), encoding="utf-8") as source:
        decor_source = source.read()
    decor_table = re.search(
        r"static const decor_def_t k_decor_kinds\[DECOR_COUNT\]\s*=\s*\{(.*?)\n\};",
        decor_source, re.DOTALL,
    )
    decor_rows = re.findall(
        r'\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*(\d+)\s*,\s*(\d+)\s*,'
        r'\s*(\d+)\s*,\s*(\d+)\s*,\s*"decor_([^"]+)"\s*\}',
        decor_table.group(1),
    ) if decor_table else []
    if len(decor_rows) != len(DECOR_ITEMS) or [
        row[-1] for row in decor_rows
    ] != list(DECOR_ITEMS):
        raise ValueError("Decor prices/definitions do not match decor enum and sprite order")

    with open(os.path.join(root, "main", "theme.h"), encoding="utf-8") as source:
        theme_header = source.read()
    if not re.search(r"#define\s+THEME_COUNT\s+24\b", theme_header):
        raise ValueError("theme.h must expose all 24 background IDs")
    with open(os.path.join(root, "main", "theme.c"), encoding="utf-8") as source:
        themes = source.read()
    theme_match = re.search(
        r"const theme_def_t k_themes\[THEME_COUNT\]\s*=\s*\{(.*?)\n\};",
        themes, re.DOTALL,
    )
    theme_names = re.findall(r'\{\s*"([^"]+)"', theme_match.group(1)) if theme_match else []
    if [name.replace(" ", "_") for name in theme_names] != list(BACKGROUNDS):
        raise ValueError("Theme definitions do not match background IDs and art")

    for name in ACHIEVEMENTS:
        symbol = f"ACH_{name.upper()}"
        asset = f"MMAP_STORAGE_ACHIEVEMENT_{name.upper()}_QOI"
        if not re.search(rf"case\s+{symbol}\s*:\s*return\s+{asset}\s*;", mappings):
            raise ValueError(f"{symbol} must map to its dedicated achievement badge")
    with open(os.path.join(root, "main", "game.h"), encoding="utf-8") as source:
        defined = set(re.findall(r"^#define\s+(ACH_\w+)\s+", source.read(), re.MULTILINE))
    if defined != {f"ACH_{name.upper()}" for name in ACHIEVEMENTS}:
        raise ValueError("Achievement badge set does not cover every game.h ACH_* bit")
    if not re.search(r"if\s*\(dead\)\s*return\s+base\s*;", mappings):
        raise ValueError("Dead prey must resolve dead_0 regardless of animation frame")

    for path, symbol in (
        ("main/prey.c", "sprite_prey_index"),
        ("main/terrarium.c", "sprite_gift_index"),
        ("main/ui_hud.c", "sprite_icon_index"),
    ):
        with open(os.path.join(root, *path.split("/")), encoding="utf-8") as source:
            if symbol not in source.read():
                raise ValueError(f"{path} does not consume {symbol}")


def write_preview(directory, destination):
    """Make a review sheet from validated sources, never filtered game artwork."""
    names = sorted(expected_dimensions())
    backgrounds = [name for name in names if name.startswith("background_")]
    sprites = [name for name in names if not name.startswith(("background_", "spider_"))]
    top = ((len(backgrounds) + 3) // 4) * 260
    sheet = Image.new("RGB", (960, top + ((len(sprites) + 5) // 6) * 140), "#16283c")
    draw = ImageDraw.Draw(sheet)
    for group, columns, width, height, start in (
        (backgrounds, 4, 240, 260, 0), (sprites, 6, 160, 140, top),
    ):
        for index, name in enumerate(group):
            with Image.open(os.path.join(directory, name)) as source:
                scale = 1 if name.startswith("background_") else min(
                    2, 128 // max(source.size))
                image = source.resize((source.width * scale, source.height * scale),
                                      Image.Resampling.NEAREST)
            x, y = index % columns * width, start + index // columns * height
            sheet.paste(image, (x + (width - image.width) // 2, y), image)
            label = name.removesuffix(".png")
            draw.text((x + 2, y + height - 24), label[:24], fill="#e8efda")
            draw.text((x + 2, y + height - 12), label[24:], fill="#e8efda")
    os.makedirs(os.path.dirname(os.path.abspath(destination)), exist_ok=True)
    sheet.save(destination)
    print(f"PREVIEW: all {len(backgrounds) + len(sprites)} non-spider assets: {destination}")


def main():
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    packed_options = parser.add_mutually_exclusive_group()
    packed_options.add_argument("--packed", help="packed storage.bin to validate")
    packed_options.add_argument(
        "--png-only", action="store_true",
        help="validate source PNGs without checking an existing (possibly stale) pack",
    )
    parser.add_argument("--preview", help="write a nearest-neighbor non-spider review sheet")
    args = parser.parse_args()
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    asset_dir = os.path.join(root, "assets")
    generator = os.path.join(root, "tools", "gen_sprites.py")
    packed_path = args.packed or os.path.join(
        root, "build", "mmap_build", "assets", "storage", "storage.bin"
    )
    os.makedirs(asset_dir, exist_ok=True)

    preserved = {
        name: read_bytes(os.path.join(asset_dir, name))
        for name in expected_dimensions()
    }

    subprocess.run([sys.executable, generator], cwd=root, check=True,
                   stdout=subprocess.DEVNULL)
    expected = validate(asset_dir)
    validate_runtime_mappings(root)
    for name, contents in preserved.items():
        with open(os.path.join(asset_dir, name), "rb") as generated:
            if generated.read() != contents:
                raise ValueError(f"Preserved PNG changed: {name}")

    first = hashes(asset_dir, expected)
    subprocess.run([sys.executable, generator], cwd=root, check=True,
                   stdout=subprocess.DEVNULL)
    validate(asset_dir)
    second = hashes(asset_dir, expected)
    if first != second:
        changed = sorted(name for name in first if first[name] != second[name])
        raise ValueError(f"Generation is not deterministic: {changed}")
    for name, contents in preserved.items():
        with open(os.path.join(asset_dir, name), "rb") as generated:
            if generated.read() != contents:
                raise ValueError(f"Preserved PNG changed on repeat generation: {name}")

    if args.png_only:
        print("PACKED: skipped (--png-only; rebuild the pack before packed validation)")
    elif os.path.isfile(packed_path):
        validate_packed_assets(packed_path, expected, asset_dir)
    elif args.packed:
        raise ValueError(f"packed asset image not found: {packed_path}")
    else:
        print("PACKED: skipped (no build/mmap_build/assets/storage/storage.bin)")
    if args.preview:
        write_preview(asset_dir, args.preview)

    print(f"VALID: {len(expected)} PNGs, RGBA, expected dimensions and names")
    print(f"PIXEL ART: {NON_SPIDER_COUNT} non-spider PNGs, binary alpha; sprites use uniform "
          f"2x2 blocks and at most {MAX_VISIBLE_COLORS} visible colors each")
    print("DETERMINISTIC: two generation passes have identical SHA-256 hashes")
    print(f"SPIDER: {SPIDER_COUNT} frames exported from main/cspider.c geometry and palette")
    print(f"PRESERVED: all {len(preserved)} source PNGs are byte-identical across generation")
    print(f"ACHIEVEMENTS: {len(ACHIEVEMENTS)} unique dedicated badge contents and silhouettes")
    print(f"SPLATTERS: all {len(PREY_SPECIES)} species, colored puddles and detached droplets")
    print(f"BACKGROUNDS: {len(BACKGROUNDS)} opaque 240x240 keeper enclosures, "
          f"at most {MAX_BACKGROUND_COLORS} colors, quiet three-tone 160x160 centers")
    print(f"DECOR: {len(DECOR_ITEMS)} placeable props with 8-12 material tones each")
    print(f"GIFTS: {len(GIFTS)} furnishings with 8-12 material tones each")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        sys.exit(f"ERROR: {error}")
