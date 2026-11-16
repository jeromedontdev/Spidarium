"""Build the approved enclosure-frame prompt for a border theme.

A border is a transparent overlay, never a picture with a filled middle: the
device composites it over the floor, and the play opening must stay clear so
the spider is visible. The prompt therefore asks for a thin square frame whose
art is confined to the outer band, with a hollow centre.

The generated frame is nine-sliced down to the contract's 8px rim by
`import_border.py`, so the band the generator draws only has to be roughly the
same proportion of the canvas, not pixel-exact.

Naming commercial game titles makes the image provider refuse the request with
a copyright block, so the style is described as "modern hand-crafted pixel art"
instead.

Usage:
    python tools/border_prompts.py --list
    python tools/border_prompts.py --theme snow
    python tools/border_prompts.py --all --out scratch/prompts
"""

import argparse
from pathlib import Path

# theme -> (frame material, motif, palette)
#
# The motif is what makes each frame recognisable at 8 logical pixels thick, so
# it is described as a small repeated element along the band rather than a
# scene. Frames are decorative, so they are allowed more contrast than a floor.
THEMES = {
    "cork": (
        "thick cork oak bark",
        "irregular bark plates of different widths, with a few small round cork "
        "borer holes and a thin pale cork-dust line along the inner edge",
        "warm ochre bark, pale sandy highlight, muted red-brown shadow, faint "
        "grey-green lichen",
    ),
    "leaf_litter": (
        "weathered fallen timber",
        "overlapping fallen leaves tucked along the band at every angle, some "
        "curled, some half-hidden, with a few bare twig stubs",
        "warm russet, faded ochre, muted olive, cool brown shadow",
    ),
    "woodland": (
        "weathered brown timber",
        "small acorns and a few narrow oak leaves tucked along the band, with "
        "short twig stubs and a little dry grass between them",
        "warm brown wood, pale straw highlight, deep umber shadow, muted olive "
        "leaf",
    ),
    "sand": (
        "packed pale beach sand over buried stone",
        "small ridged seashells and a few fragments of bleached shell pressed "
        "into the sand, with soft wind ripples running along the band",
        "pale warm sand, soft cream highlight, muted tan shadow, faint grey shell",
    ),
    "fern": (
        "damp dark timber",
        "long fern fronds arching along the band, some whole, some broken, with "
        "small tight fiddleheads at the corners",
        "deep fern green, pale yellow-green highlight, cool blue-green shadow, "
        "faint brown wood",
    ),
    "orchid": (
        "dark polished wood",
        "small orchid blooms and closed buds spaced along the band, each with a "
        "few narrow leaves, never evenly spaced",
        "deep plum, soft mauve highlight, muted rose, cool dark shadow",
    ),
    "minimal": (
        "smooth pale stone",
        "a single narrow inset groove running along the middle of the band, with "
        "tiny mitred joints at the corners and nothing else",
        "pale warm grey, soft cream highlight, muted grey shadow",
    ),
    "mushroom": (
        "mossy dark timber",
        "small mushroom caps of several sizes clustered along the band, some tall "
        "and slender, some broad and flat, with a little moss between them",
        "warm cream cap, muted red-brown, deep moss green, cool shadow",
    ),
    "gothic": (
        "dark carved slate",
        "narrow pointed arch niches cut into the band, each with a small dark "
        "recess, separated by plain pilasters",
        "cold blue-grey slate, pale grey highlight, near-black shadow, faint "
        "violet tint",
    ),
    "seashell": (
        "sun-bleached driftwood and packed shell",
        "small spiral shells and ribbed scallop shells set along the band, with "
        "a scatter of shell grit between them",
        "warm cream shell, pale sand highlight, muted teal shadow, soft pink tint",
    ),
    "succulent": (
        "rough grey gravel set in mortar",
        "small rosette succulents of several sizes tucked along the band, each "
        "with plump pointed leaves, with loose gravel between them",
        "muted sage green, pale grey-green highlight, dusty blue shadow, warm "
        "stone",
    ),
    "willow": (
        "weathered pale timber",
        "long willow branches trailing along the band with narrow drooping "
        "leaves, a few small catkins hanging between them",
        "soft yellow-green, pale straw highlight, cool olive shadow, faint brown "
        "wood",
    ),
    "desert": (
        "sun-cracked sandstone",
        "small barrel cacti and a few flat prickly-pear pads along the band, with "
        "fine sand drifted against them",
        "warm ochre stone, pale sand highlight, muted red-brown shadow, dusty "
        "green",
    ),
    "crystal": (
        "chunky violet-slate stone blocks",
        "clusters of small angular crystal shards growing out of the stone along "
        "the band, some tall and thin, some short and broad. The stone is clearly "
        "violet-slate, not grey, and the crystals are bright lavender and cyan "
        "against it, so the band reads as a crystal cave frame",
        "violet-slate stone, pale lavender highlight, deep indigo shadow, bright "
        "cyan crystal",
    ),
    "cemetery": (
        "weathered grey slate",
        "small leaning headstones and low iron railings along the band, with a "
        "little dry ivy creeping between them",
        "cold grey slate, pale grey highlight, near-black shadow, muted moss green",
    ),
    "house": (
        "warm varnished timber",
        "small folded cloth bunting and a few wooden pegs along the band, with "
        "narrow plank joints between them",
        "warm honey wood, pale cream highlight, muted brown shadow, soft red cloth",
    ),
    "dark_cave": (
        "rough dark mineral rock",
        "small clusters of angular mineral crystals and a few thin stalactite "
        "spikes hanging from the band, never evenly spaced",
        "deep blue-grey rock, cool pale highlight, near-black shadow, faint teal "
        "crystal",
    ),
    "night": (
        "dark blue-grey stone",
        "small four-pointed stars and a few thin crescent moons set along the "
        "band, with a scatter of tiny dim stars between them",
        "deep indigo, pale blue-white highlight, near-black shadow, faint violet",
    ),
    "autumn": (
        "weathered brown timber",
        "small maple leaves in several shapes scattered along the band at every "
        "angle, some fresh, some curled and dry",
        "warm amber, pale gold highlight, deep russet shadow, muted brown",
    ),
    "snow": (
        "pale packed snow over dark stone",
        "small snow-laden pine sprigs and a few clear ice shards along the band, "
        "with soft snow drifts banked against them",
        "pale blue-white snow, bright white highlight, cool blue shadow, dark "
        "grey stone",
    ),
    "pond": (
        "wet dark timber and mossy stone",
        "tall reed clumps and a few flat lily pads along the band, with small "
        "water droplets beading on them",
        "deep pond green, pale yellow-green highlight, cool teal shadow, warm "
        "brown wood",
    ),
    "ruins": (
        "cracked pale flagstone",
        "small carved rune marks cut into the stone along the band, with a few "
        "chipped edges and a little dry moss in the cracks",
        "warm grey stone, pale sand highlight, muted green shadow, dark crack",
    ),
    "marsh": (
        "wet dark peat and timber",
        "clumps of marsh grass and a few cattail stalks along the band, with "
        "small patches of standing water between them",
        "muted olive green, pale straw highlight, deep brown shadow, cool teal "
        "water",
    ),
    "fairy_glade": (
        "mossy pale timber",
        "small five-petalled wildflowers and tiny glowing motes along the band, "
        "with soft moss creeping between them",
        "soft green, pale cream highlight, cool teal shadow, warm pink flower",
    ),
    "moonflower_grove": (
        "dark mossy stone",
        "small white moonflowers with long petals along the band, some open, some "
        "closed, with narrow leaves between them",
        "deep blue-green, pale silver highlight, near-black shadow, soft white "
        "flower",
    ),
    "cathedral": (
        "pale carved limestone",
        "narrow pointed arch mouldings and small quatrefoil roundels cut into the "
        "band, separated by plain shafts",
        "warm pale stone, bright cream highlight, cool grey shadow, faint blue "
        "glass",
    ),
    "rose_courtyard": (
        "warm grey flagstone",
        "small climbing roses with a few open blooms and tight buds along the "
        "band, with narrow leaves between them",
        "warm grey stone, pale sand highlight, deep rose shadow, muted green leaf",
    ),
    "library": (
        "dark polished wood",
        "small rows of book spines of different heights and colours along the "
        "band, some leaning, with a few loose pages between them",
        "deep walnut, warm tan highlight, near-black shadow, muted red and green "
        "spines",
    ),
    "observatory": (
        "dark blue-grey metal",
        "small brass gear wheels and thin engraved star charts along the band, "
        "with a few tiny rivets between them. The brass is warm and bright "
        "against the dark metal, so the band reads clearly",
        "deep blue-grey metal, bright brass highlight, near-black shadow, pale "
        "cyan glass",
    ),
    "kitchen": (
        "pale glazed ceramic tile",
        "small checkered tiles along the band with a few tiny copper hooks and a "
        "sprig of herbs between them",
        "warm cream tile, bright white highlight, muted grey shadow, soft red "
        "check",
    ),
    "sewing_room": (
        "warm painted timber",
        "small spools of thread and a few crossed pins along the band, with "
        "neat running stitches between them",
        "soft rose, pale cream highlight, muted plum shadow, warm brown wood",
    ),
    "witch_garden": (
        "dark weathered timber",
        "small bundles of hanging herbs and a few thin twisted roots along the "
        "band, with tiny dark berries between them. The herbs are pale sage and "
        "dull amber against the near-black wood, so the band reads clearly",
        "near-black wood, pale sage highlight, muted amber, deep purple shadow",
    ),
    "moon_altar": (
        "dark carved stone",
        "small crescent moon carvings and thin silver inlay lines along the band, "
        "with a few tiny stars between them",
        "deep blue-grey stone, pale silver highlight, near-black shadow, faint "
        "violet",
    ),
    "primeval_jungle": (
        "rough dark bark and moss",
        "small cycad fronds and a few thick coiled fern stems along the band, "
        "with damp moss creeping between them",
        "deep jungle green, pale yellow-green highlight, near-black shadow, warm "
        "brown bark",
    ),
    "amber_basin": (
        "warm ochre sandstone",
        "small fossil spirals and a few embedded amber nodules along the band, "
        "with fine sand drifted between them",
        "warm amber, pale gold highlight, deep brown shadow, muted teal fossil",
    ),
    "neon_alley": (
        "dark wet asphalt and steel",
        "thin glowing circuit traces and a few small neon tube segments along the "
        "band, with dark rivets between them",
        "near-black steel, bright cyan highlight, deep magenta glow, pale white "
        "core",
    ),
    "circuit_garden": (
        "dark brushed metal",
        "small circuit traces with a few tiny glowing nodes along the band, with "
        "thin copper vines creeping between them",
        "deep teal metal, bright cyan highlight, near-black shadow, warm copper",
    ),
    "lunar_base": (
        "pale grey metal plating",
        "small hexagonal bolt heads and thin seam lines along the band, with a "
        "few tiny warning stripes between them",
        "pale grey metal, bright white highlight, cool blue shadow, muted orange "
        "stripe",
    ),
    "starship": (
        "dark grey metal panelling",
        "small rectangular panel joints and a few thin glowing indicator strips "
        "along the band, with tiny rivets between them. The indicator strips are "
        "bright cyan against the dark metal, so the band reads clearly",
        "deep blue-grey metal, pale steel highlight, near-black shadow, bright "
        "cyan strip",
    ),
    "enchanted_court": (
        "pale carved stone",
        "small glowing rune marks and thin inlaid silver lines along the band, "
        "with a few tiny floating motes between them",
        "warm pale stone, bright cream highlight, cool violet shadow, soft cyan "
        "glow",
    ),
    "dragon_heath": (
        "charred dark rock",
        "small clusters of glowing embers and a few thin charred twigs along the "
        "band, with fine grey ash between them. The embers are bright orange "
        "against the near-black rock, so the band reads clearly",
        "near-black rock, bright orange ember, deep red shadow, pale grey ash",
    ),
    "lost_city": (
        "sun-bleached sandstone",
        "small carved glyph blocks and a few broken column stubs along the band, "
        "with fine sand drifted between them",
        "warm sand stone, pale cream highlight, muted brown shadow, faint teal "
        "glyph",
    ),
    "sunken_temple": (
        "waterlogged dark stone",
        "small carved glyphs and a few thin barnacle clusters along the band, "
        "with soft algae creeping between them. The glyphs are pale mint against "
        "the dark stone, so the band reads clearly",
        "deep teal stone, pale mint highlight, near-black shadow, muted coral",
    ),
    "zen_garden": (
        "raked pale gravel and dark stone",
        "small raked gravel ridges running along the band with a few smooth dark "
        "stones set into them, never evenly spaced",
        "pale warm gravel, soft cream highlight, muted grey shadow, dark stone",
    ),
    "bamboo_grove": (
        "pale split bamboo",
        "short bamboo culms with visible nodes along the band, with a few narrow "
        "leaves between them",
        "pale yellow-green bamboo, bright cream highlight, cool olive shadow, "
        "warm brown node",
    ),
    "sakura_courtyard": (
        "warm grey timber",
        "small cherry blossom clusters along the band, some open, some tight buds, "
        "with a few loose petals between them",
        "warm grey wood, pale cream highlight, soft pink blossom, muted brown "
        "shadow",
    ),
    "cloud_sanctuary": (
        "soft pale cloud",
        "small rounded cloud billows along the band with a few tiny four-pointed "
        "stars between them, never evenly spaced. The billows have clear pale "
        "highlights and soft lavender shadows, so the band reads as carved",
        "pale blue-white cloud, bright white highlight, muted lavender shadow, "
        "faint warm cream",
    ),
}

# Themes whose frame is deliberately plain. Asking for motifs here would break
# the theme, so the motif paragraph is replaced with a restraint instruction.
PLAIN = {"minimal"}

GEOMETRY_RULE = (
    "GEOMETRY: a thin square frame only. The decorated band is NARROW, about "
    "one fifteenth of the image width, and runs along all four edges, meeting "
    "cleanly at the four corners. The entire middle of the image is EMPTY and "
    "hollow, with nothing drawn in it at all. The frame is a flat overlay seen "
    "straight on, not a picture frame in perspective and not a room."
)

LIGHTING_RULE = (
    "LIGHTING: one light from the top-left. The upper and left faces of each "
    "element catch a lighter tone and the lower and right faces fall into a "
    "darker tone, so the band reads as carved and three-dimensional."
)

PLAIN_RULE = (
    "RESTRAINT: this frame is deliberately plain. One narrow inset groove along "
    "the middle of the band, tiny mitred joints at the corners, and nothing "
    "else. No motifs, no decoration, no ornament."
)

PIXEL_RULE = (
    "PIXEL ART RULES: Hard-edged pixels, no anti-aliasing, no blur, no "
    "photographic texture. Discrete color ramps only, each element shaded with "
    "three to four flat tones. Maximum 16 colors total. Modern hand-crafted "
    "pixel art, the kind used in contemporary indie games."
)

EXCLUSIONS = (
    "No text, no UI, no characters, no creatures, no spider, no scene, no "
    "landscape, no floor, no background, no filled centre, no vignette, no "
    "drop shadow outside the frame, no perspective, no 3D render."
)


def prompt(theme):
    """The full generation prompt for one border theme."""
    if theme not in THEMES:
        raise KeyError(f"no prompt recipe for theme {theme!r}")
    material, motif, palette = THEMES[theme]
    detail = PLAIN_RULE if theme in PLAIN else f"MOTIF: {motif}."
    return "\n\n".join([
        "Modern pixel art ENCLOSURE FRAME for a virtual pet game. A thin square "
        "decorative border that surrounds a small play area.",
        GEOMETRY_RULE,
        f"MATERIAL: {material}, forming the band itself.",
        detail,
        LIGHTING_RULE,
        f"PALETTE: {palette}. All closely spaced tones of one another.",
        PIXEL_RULE,
        EXCLUSIONS,
    ])


def missing():
    """Themes that still need a border prompt recipe."""
    import approved_sources
    import asset_build
    return [
        theme for theme in asset_build.BACKGROUNDS
        if theme not in THEMES and not approved_sources.is_border_approved(theme)
    ]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--theme", help="print the prompt for one theme")
    parser.add_argument("--all", action="store_true", help="write every prompt")
    parser.add_argument("--out", type=Path, help="directory for --all")
    parser.add_argument("--list", action="store_true", help="list themes")
    args = parser.parse_args(argv)

    if args.list:
        for theme in THEMES:
            print(f"{theme:22} {'plain' if theme in PLAIN else 'motif'}")
        return 0

    if args.theme:
        print(prompt(args.theme))
        return 0

    if args.all:
        if not args.out:
            parser.error("--all needs --out")
        args.out.mkdir(parents=True, exist_ok=True)
        for theme in THEMES:
            (args.out / f"prompt_border_{theme}.txt").write_text(
                prompt(theme) + "\n", encoding="utf-8"
            )
        print(f"wrote {len(THEMES)} prompts to {args.out}")
        return 0

    parser.error("choose --list, --theme, or --all")


if __name__ == "__main__":
    raise SystemExit(main())
