"""Prepare offline PixelLab art briefs and check returned candidates. Never submits."""

import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image

from asset_build import BACKGROUNDS, validate_pixel_art


ROOT = Path(__file__).resolve().parents[1]
API_DOCS = "https://www.pixellab.ai/pixellab-api"
STYLE = (
    "16-bit SNES-era pixel art, not flat 8-bit icons. Crisp intentional pixel "
    "clusters, stepped contours, hue-shifted finite material ramps, consistent "
    "top-left light, shaded volume, broken highlights and readable surface texture. "
    "No blur, antialiasing, gradients, smooth noise, photographic texture, text, "
    "logos, characters, spider, insects or pre-placed furniture. Keep the input "
    "theme, recognizable motifs, camera and composition; enrich craftsmanship "
    "rather than redesigning or merely recoloring. The supplied style reference "
    "is a nine-panel woodland, ruins, desert, mushroom, autumn, snow, beach, "
    "crystal cave and pond study. Learn its overlapping leaves with separate "
    "lit/shadow halves and veins, beveled chipped stones, ribbed bark, layered "
    "ground clusters and small broken highlight pixels. Guide materials and "
    "pixel technique, not a copy of any panel or its crowded layout. Deliver "
    "one square asset, never a nine-panel sheet. Do not paint the reference's "
    "rounded black corners into the asset: game masking handles those."
)
MOSS = (
    "Layered moss cushions with distinct overlapping leafy clusters and short "
    "shoots; rimmed stones with readable lit tops, cool shadow planes and chipped "
    "edges; restrained earthy olive/green ground. Avoid repeated identical "
    "round blobs and random bright speckles."
)


def asset_name(kind: str, theme: str) -> str:
    if kind not in ("background", "border") or theme not in BACKGROUNDS:
        raise ValueError("Choose a background/border and an existing theme key")
    return f"{kind}_{theme}_0.png"


def image_info(path: Path) -> dict:
    with Image.open(path) as image:
        image.load()
        size = list(image.size)
    return {"path": str(path.resolve()), "size": size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def prompt(kind: str, theme: str, action: str) -> str:
    asset_name(kind, theme)
    if action not in ("edit", "inpaint"):
        raise ValueError("Choose edit or inpaint")
    if kind == "background" and theme == "crystal":
        return (
            f"{STYLE}\nPreserve the approved Crystal floor's richly clustered "
            "violet mineral texture across the entire opaque 240x240 square, "
            "at most 64 colors. No large crystals, boulders, enclosing rock "
            "rim or baked-in decorative frame: those belong to separate "
            "border/decor layers. Do not restrict its center to three tones. "
            "Crystal is hash-pinned to an approved source; a changed candidate "
            "needs explicit visual approval and contract updates before installation."
        )
    if kind == "background" and theme == "moss":
        return (
            f"{STYLE}\nPreserve the exact approved moss floor: layered low-relief "
            "growth with warm/cool green material ramps, 240x240 opaque RGBA, "
            "17 center tones and luminance span at most 80/255. Its pixels are "
            "hash-pinned; changes need explicit approval before installation. "
            "No large plants, boulders or baked-in border."
        )
    tone_limit, luma_limit = (3, 24) if theme == "minimal" else (16, 64)
    composition = (
        "One 240x240 opaque top-down enclosure terrain image, NOT a side-view "
        "landscape, horizon, diorama, room interior or nine-panel collage. "
        f"Keep [40,200) on both axes open: at most {tone_limit} close substrate tones, "
        f"integer luminance span <={luma_limit}/255. Use richer low-relief material "
        "texture across the floor; larger accent details only in the outer "
        "40px margins. Use at most 64 visible colors. Fine center "
        "marks must be sparse, low-contrast clusters. No large rocks, logs, "
        "trees, shelters or other furniture in the playable center."
        if kind == "background" else
        "One transparent ornamental enclosure frame, NOT terrain or a filled "
        "picture. Preserve all four matching edge/corner motifs. Source is "
        "240x240; logical art is 120x120, nearest-neighbor doubled into uniform "
        "aligned 2x2 blocks. At most 16 visible material colors; alpha only "
        "0 or 255. Only the outer 16px rim may contain art. Every pixel of "
        "[16,224) on both axes must remain transparent. Enrich layered "
        "material planes, small carvings, leaf veins and broken highlights "
        "without thickening the frame or shrinking its opening."
    )
    repair = (
        " Edit only the supplied white mask region; preserve every black-mask "
        "pixel exactly. Do not modify the protected opening/quiet center."
        if action == "inpaint" else ""
    )
    materials = (
        "Crystal border: thin violet/slate chipped mineral edging with small "
        "lavender/cyan faceted tips. Copy the reference's material shading, "
        "not its crystal sizes or enclosing cave layout. On a 120x120 logical "
        "canvas, corner groups fit inside 8x8 and edge tips are only 3-6px tall. "
        "No giant boulders, pillars, filigree, filled ground or white glare. "
        "Generated geometry is not guaranteed: retain the raw source and "
        "document any explicit nearest-neighbor nine-slice fit before review."
        if kind == "border" and theme == "crystal" else MOSS if theme == "moss" else (
        f"Theme: {theme.replace('_', ' ')}. Use the input image as the authority "
        "for palette families and edge motifs; preserve its design."
        )
    )
    return f"{STYLE}\n{composition}\n{materials}{repair}"


def make_brief(kind: str, theme: str, action: str, reference: Path | None) -> dict:
    name = asset_name(kind, theme)
    return {
        "offline_only": True,
        "ready_for_reference_review": reference is not None,
        "target": name,
        "source": image_info(ROOT / "assets" / name),
        "style_reference": image_info(reference) if reference else None,
        "action": action,
        "suggested_endpoint": (
            "/v2/edit-images-v2" if action == "edit" else "/v2/inpaint-v3"
        ),
        "api_documentation": API_DOCS,
        "api_payload": None,
        "submission_blockers": [
            "Verify the current endpoint schema, reference-image roles and mask encoding.",
            "Confirm trial API access and exact generation debit before submitting.",
            "Request a single output, not a multi-variation grid; verify its cost.",
            "Review the actual original reference image and this prompt together.",
        ],
        "budget": {
            "user_reported_generations": 40,
            "pilot_max_requests": 1,
            "automatic_retries": 0,
            "generations_spent_by_this_tool": 0,
            "generations_per_api_request": None,
            "policy": "One background first; review before a matching border or any batch.",
        },
        "prompt": prompt(kind, theme, action),
        "acceptance": [
            "Pass native dimensions, palette, binary-alpha and quiet-center/frame checks.",
            "Review at native scale and nearest-neighbor 2x, composed with existing decor.",
            "Design remains recognizable; edge detail is materially richer, not noisier.",
            "Never replace the approved spider or automatically install candidate artwork.",
            "Integration requires a reproducible source path; current generator overwrites PNGs.",
        ],
    }


def mask_image(kind: str) -> Image.Image:
    if kind not in ("background", "border"):
        raise ValueError("Choose background or border")
    image = Image.new("L", (240, 240), 255)
    box = (40, 40, 200, 200) if kind == "background" else (16, 16, 224, 224)
    image.paste(0, box)
    return image


def check_candidate(path: Path, kind: str, theme: str) -> dict:
    name = asset_name(kind, theme)
    with Image.open(path) as image:
        image.load()
        if image.format != "PNG" or image.mode != "RGBA" or image.size != (240, 240):
            raise ValueError("Candidate must be a 240x240 RGBA PNG; no implicit conversion")
        colors = validate_pixel_art(image, name)
    return {"target": name, "candidate": image_info(path), "visible_colors": colors,
            "technical_checks": "PASS", "visual_approval": "not established",
            "installed": False}


def write_new(path: Path, data: str) -> None:
    with path.open("x", encoding="utf-8") as output:
        output.write(data + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    brief = commands.add_parser("brief", help="Create one offline, non-API prompt brief")
    brief.add_argument("--kind", choices=("background", "border"), required=True)
    brief.add_argument("--theme", choices=BACKGROUNDS, default="moss")
    brief.add_argument("--action", choices=("edit", "inpaint"), default="edit")
    brief.add_argument("--reference", type=Path)
    brief.add_argument("--draft-without-reference", action="store_true")
    brief.add_argument("--output", type=Path, required=True)
    mask = commands.add_parser("mask", help="Create a review mask: white editable, black protected")
    mask.add_argument("--kind", choices=("background", "border"), required=True)
    mask.add_argument("--output", type=Path, required=True)
    check = commands.add_parser("validate", help="Validate an external candidate, never install it")
    check.add_argument("candidate", type=Path)
    check.add_argument("--kind", choices=("background", "border"), required=True)
    check.add_argument("--theme", choices=BACKGROUNDS, default="moss")
    args = parser.parse_args()
    try:
        if args.command != "validate" and args.output.resolve().is_relative_to(ROOT):
            raise ValueError("Write staging files outside the repository to protect source art")
        if args.command == "brief":
            if args.reference is None and not args.draft_without_reference:
                raise ValueError("Supply the original --reference, or explicitly create a draft")
            result = make_brief(args.kind, args.theme, args.action, args.reference)
            write_new(args.output, json.dumps(result, indent=2))
            print("OFFLINE BRIEF: zero API calls; not a verified API request payload")
        elif args.command == "mask":
            if args.output.suffix.lower() != ".png":
                raise ValueError("Mask output must end in .png")
            with args.output.open("xb") as output:
                mask_image(args.kind).save(output, format="PNG")
            print("OFFLINE MASK: white editable, black protected; verify API mask conventions")
        else:
            print(json.dumps(check_candidate(args.candidate, args.kind, args.theme), indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, f"ERROR: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
