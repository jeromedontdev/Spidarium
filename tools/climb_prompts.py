"""Build the approved prompt for a large climbable structure.

These are the six big props the spider walks up: a fallen branch, a vine arch, a
root bridge, an ivy column, a mossy boulder and a driftwood ramp. They are
authored at 192x192 and drawn 1:1, so they are the only decor that is not
doubled from a 64x64 source.

Three rules separate them from the small props:

* They must read as ONE continuous structure with a walkable spine, not as a
  pile of small objects. The spider climbs along the spine, so the silhouette
  has to suggest a route from the bottom edge to the top.
* They are seen from a low three-quarter angle, not straight down. The spider
  enters from the bottom and walks up, so the structure needs a visible near
  face and a receding top surface. A flat top-down view is what made the first
  attempt read as a hop.
* They are large, so they must stay calm. A busy 192x192 sprite competes with
  the spider standing on it.

Naming commercial game titles makes the image provider refuse the request with
a copyright block, so the style is described as "modern hand-crafted pixel art"
instead.

Usage:
    python tools/climb_prompts.py --list
    python tools/climb_prompts.py --name branch
    python tools/climb_prompts.py --all --out scratch/prompts
"""

import argparse
from pathlib import Path

# name -> (subject, form, material detail, palette)
STRUCTURES = {
    "branch": (
        "A single thick fallen tree branch lying diagonally across the frame",
        "one continuous limb running from the lower-left edge to the upper-right "
        "edge, thick at the bottom and tapering toward the top, with two short "
        "broken side stubs. The upper surface is a broad flat walkable ridge; the "
        "near face drops away below it in shadow",
        "a few broad bark plates suggested by flat colour bands, one pale cut end "
        "where a stub broke off, and a single pale strip along the top ridge",
        "warm mid-brown bark, pale sandy highlight on the ridge, deep red-brown "
        "shadow on the near face",
    ),
    "vine": (
        "A thick woody vine arching across the frame",
        "one continuous vine rising from the lower-left, curving over the middle "
        "and descending to the lower-right, with a clear walkable top edge along "
        "the whole arc. Two thinner tendrils wrap around it. The near face is in "
        "shadow below the top edge",
        "broad twisting bands running the length of the vine, two or three simple "
        "leaf clusters attached at intervals, and a pale worn strip along the top",
        "muted olive-green vine, warm yellow-green highlight on the top edge, cool "
        "dark green shadow",
    ),
    "root": (
        "A thick exposed tree root crossing the frame",
        "one continuous root emerging from the lower-left, swelling through the "
        "middle and splitting into two short forks at the upper-right. The top of "
        "the root is a broad rounded ridge the spider can walk along; the near "
        "face is a steep shadowed wall",
        "smooth worn bark in a few flat bands, two or three deep cracks, and a "
        "pale scuffed line along the crest",
        "cool grey-brown root, pale bone highlight on the crest, deep umber shadow",
    ),
    "ivy": (
        "A thick vertical ivy-covered trunk section",
        "one continuous trunk standing from the bottom edge to the top edge, "
        "slightly leaning, with ivy leaves climbing its near face. The top of the "
        "trunk is a narrow walkable ledge; the sides fall away in shadow",
        "overlapping ivy leaves in two or three distinct sizes, each a simple flat "
        "shape with a lit upper half and a shadowed lower half, over broad bands "
        "of visible bark",
        "deep forest green leaves, warm yellow-green highlights, cool blue-green "
        "shadow, dark brown bark showing through",
    ),
    "boulder": (
        "A large mossy boulder",
        "one continuous rounded mass filling most of the frame, wider at the base "
        "and domed on top, with a broad gently sloping upper surface the spider "
        "can stand on. The near face is a steep shadowed cliff",
        "a handful of large flat stone facets with chipped edges, overlaid by a "
        "few simple moss cushions that follow the top surface and spill a little "
        "over the near edge",
        "cool grey stone, pale blue-grey highlight on the dome, deep slate shadow, "
        "warm moss green on the top",
    ),
    "driftwood": (
        "A thick piece of weathered driftwood",
        "one continuous bleached log running from the lower-right to the upper-"
        "left, with a broad flat worn top surface and a splintered near face. One "
        "short broken branch stub points up at the lower end",
        "a few broad grain bands following the length of the log, two or three "
        "large dark splits, and a pale sanded strip along the top. Do NOT draw "
        "fine parallel grain lines or individual wood fibres",
        "pale silver-grey wood, warm cream highlight on the top, cool grey-brown "
        "shadow",
    ),
}

# Shared across every structure. The low angle and the single continuous form
# are the two things the first attempt got wrong.
FORM_RULE = (
    "VIEW: a low three-quarter view, as if standing on the ground looking "
    "slightly down at the object. The object has a clearly visible NEAR FACE "
    "facing the viewer and a TOP SURFACE receding away from the viewer. Do not "
    "draw it flat from directly above."
)

CONTINUITY_RULE = (
    "ONE CONTINUOUS OBJECT: this must read as a single solid structure, not as a "
    "pile or a cluster of separate pieces. Every part connects to the main body. "
    "The silhouette must suggest a route the eye can follow from the bottom edge "
    "to the top, because a small creature walks up it."
)

LIGHT_RULE = (
    "LIGHTING: lit from the top-left. The upper-left faces are lighter, the "
    "lower-right faces are darker. The near face is in shadow relative to the top "
    "surface. Shading is hard-banded, not smooth or airbrushed."
)

TONAL_RULE = (
    "TONAL RANGE: keep it moderate and calm. A small dark character must stay "
    "clearly visible standing on top of it, so do not make the top surface busy "
    "or high contrast. Use a limited palette of about eight to twelve tones."
)

STYLE_RULE = (
    "STYLE: modern hand-crafted pixel art, the kind used in contemporary indie "
    "games. Crisp individual pixels, no anti-aliasing, no blur, no gradients, no "
    "dithering. Sharp pixel shading with distinct bands of colour. "
    "IMPORTANT: this is a STYLISED GAME SPRITE, not a painting and not a "
    "photograph. Simplify aggressively. Reduce every surface to a few flat "
    "shapes with hard-edged shading bands. Do not render fine grain, individual "
    "fibres, pores, scratches, weathering speckle or photographic texture. "
    "Read as a clean cartoon prop that a small cartoon creature can stand on."
)

# The first generated set was rejected as "almost too realistic". The failure
# mode was photographic material rendering -- driftwood in particular came back
# as a detailed photograph of bleached timber. These rules pull it back toward
# the same visual language as the 64x64 props, which are flat-shaded and read
# as objects rather than as photographs of objects.
SIMPLIFY_RULE = (
    "SIMPLIFICATION: at most three or four flat tones per material, separated "
    "by hard edges. No texture noise, no speckling, no stippling, no fine "
    "hatching, no individual wood fibres, no stone grain, no leaf veins at "
    "pixel level. Shapes are chunky and readable at a glance. If a detail would "
    "be smaller than about four pixels, leave it out entirely."
)

# The 2.5D illusion depends on the spider being able to stand on the object
# convincingly, which needs a broad, calm, clearly-lit top surface. A busy or
# high-contrast top surface fights the character standing on it.
SURFACE_RULE = (
    "WALKABLE TOP: the upper surface must be a broad, calm, clearly readable "
    "band of flat colour running the length of the object, with only a single "
    "highlight tone and a single shadow tone. This is the surface a small "
    "creature stands on, so it must stay uncluttered and must not be broken up "
    "by cracks, moss patches or debris across its whole width."
)

BACKGROUND_RULE = (
    "BACKGROUND: fully transparent. The object is isolated on transparency with "
    "no ground, no shadow cast on the ground, no scenery and no frame."
)

# The sprite is drawn 1:1 at 192x192 and centred on its footprint, so an object
# that bleeds off the canvas loses its ends and cannot be placed sensibly.
FRAMING_RULE = (
    "FRAMING: the ENTIRE object must fit inside the square canvas with a clear "
    "transparent margin of about five percent on every side. Nothing may touch "
    "or cross the canvas edge. The object is centred."
)

EXCLUSIONS = (
    "No text, no UI, no characters, no creatures, no spider, no border, no frame, "
    "no vignette, no drop shadow, no ground plane, no scenery, no smooth "
    "airbrushed shading, no photographic realism, no 3D render, no blur, no "
    "photograph, no realistic wood grain, no realistic stone texture, no "
    "hyper-detailed rendering, no texture noise, no speckle, no stippling."
)


def prompt(name):
    """The full generation prompt for one climb structure."""
    if name not in STRUCTURES:
        raise KeyError(f"no prompt recipe for structure {name!r}")
    subject, form, detail, palette = STRUCTURES[name]
    return "\n\n".join([
        f"{subject}.",
        f"FORM: {form}.",
        FORM_RULE,
        CONTINUITY_RULE,
        f"MATERIAL: {detail}.",
        f"PALETTE: {palette}.",
        LIGHT_RULE,
        TONAL_RULE,
        SURFACE_RULE,
        SIMPLIFY_RULE,
        STYLE_RULE,
        BACKGROUND_RULE,
        FRAMING_RULE,
        EXCLUSIONS,
    ])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true", help="list structure names")
    parser.add_argument("--name", help="print the prompt for one structure")
    parser.add_argument("--all", action="store_true",
                        help="write every prompt to --out")
    parser.add_argument("--out", type=Path, help="directory for --all")
    args = parser.parse_args()

    if args.list:
        for name in STRUCTURES:
            print(name)
        return
    if args.name:
        print(prompt(args.name))
        return
    if args.all:
        if not args.out:
            parser.error("--all requires --out")
        args.out.mkdir(parents=True, exist_ok=True)
        for name in STRUCTURES:
            path = args.out / f"prompt_{name}.txt"
            path.write_text(prompt(name) + "\n", encoding="utf-8")
            print(path)
        return
    parser.print_help()


if __name__ == "__main__":
    main()
