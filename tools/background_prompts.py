"""Build the approved ground-texture prompt for a background theme.

Every background is a flat material sample, not a scene: the device composites a
separate border layer over it, so any edge decoration in the floor would be
drawn twice. The prompt that produced the approved woodland floor is the
template here, with the material swapped per theme.

Two rules are theme-dependent:

* Organic ground (soil, moss, litter, sand, gravel, ash, snow, cloud) must not
  repeat. Asking for "small rounded clumps" produced a regular cobblestone
  field, so the non-repetition rule is stated explicitly and at length.
* Manufactured ground (paving, floorboards, tiling, panelling, weave) is
  supposed to repeat. Forcing irregularity there produces mush.

Naming commercial game titles makes the image provider refuse the request with
a copyright block, so the style is described as "modern hand-crafted pixel art"
instead.

Usage:
    python tools/background_prompts.py --list
    python tools/background_prompts.py --theme snow
    python tools/background_prompts.py --all --out scratch/prompts
"""

import argparse
from pathlib import Path

# theme -> (subject, material, surface, palette, organic, repeat unit)
THEMES = {
    "cork": (
        "A cork oak trunk surface",
        "cork oak bark",
        "irregular vertical bark plates of wildly different widths, separated by "
        "deep narrow fissures. Some plates are broad and flat, some narrow and "
        "broken, some split in two, some almost closed. Plate widths range from "
        "about eight to about fifty pixels, all mixed together with no regularity. "
        "Each plate is lit from the top-left, so it has a gently lighter upper-left "
        "face and a gently darker lower-right face.",
        "warm ochre bark, pale sandy highlights, muted red-brown shadow, faint "
        "grey-green lichen tint",
        True,
        "",
    ),
    "moss": (
        "A moss-covered forest floor",
        "dense cushion moss",
        "soft rounded cushions of moss that merge into one another. Each cushion is "
        "lit from the top-left, so it has a gently lighter upper-left face and a "
        "gently darker lower-right face. Cushion sizes range from about eight to "
        "about forty pixels across, all mixed together with no regularity. Some "
        "cushions are tall and plump, some flat and spreading, some broken by a bare "
        "patch of dark soil.",
        "deep forest green, warm yellow-green highlights, cool blue-green shadow, "
        "faint brown soil showing through",
        True,
        "",
    ),
    "leaf_litter": (
        "A forest floor covered in fallen leaves",
        "a loose layer of fallen leaves",
        "overlapping fallen leaves lying at every angle, some flat, some curled, some "
        "half-buried, some stacked two or three deep. Leaf sizes range from about ten "
        "to about forty pixels long, all mixed together with no regularity. Each leaf "
        "is lit from the top-left, so it has a gently lighter upper-left face and a "
        "gently darker lower-right face.",
        "warm russet, faded ochre, muted olive, cool brown shadow",
        True,
        "",
    ),
    "sand": (
        "A beach of fine sand",
        "fine beach sand",
        "gentle wind-formed ripples and shallow drifts. Ripple crests are soft and "
        "rounded, running in loose irregular curves that wander and merge, never "
        "parallel and never evenly spaced. Ripple spacing ranges from about fifteen "
        "to about sixty pixels, all mixed together with no regularity. Each crest is "
        "lit from the top-left, so it has a gently lighter upper-left face and a "
        "gently darker lower-right face.",
        "pale warm sand, soft cream highlights, muted tan shadow, faint grey shell tint",
        True,
        "",
    ),
    "fern": (
        "A forest floor of fern fronds over damp moss",
        "fern fronds lying over damp moss",
        "long fern fronds lying at every angle over a bed of moss, some fronds whole, "
        "some broken, some half-hidden under others. Frond lengths range from about "
        "thirty to about ninety pixels, all mixed together with no regularity. Each "
        "frond is lit from the top-left, so it has a gently lighter upper-left face "
        "and a gently darker lower-right face.",
        "deep fern green, bright yellow-green highlights, cool blue-green shadow, "
        "faint brown stem",
        True,
        "",
    ),
    "orchid": (
        "A bed of dark orchid soil",
        "dark orchid soil scattered with fallen petals",
        "loose dark soil with a few fallen orchid petals resting on it. The soil is "
        "uneven and lumpy, made of soft irregular mounds that merge into one another. "
        "Petals are sparse and scattered at every angle, some flat, some curled, some "
        "half-sunk. Mound sizes range from about ten to about forty pixels across, all "
        "mixed together with no regularity.",
        "deep plum-brown soil, muted mauve highlights, cool violet shadow, pale pink "
        "petal",
        True,
        "",
    ),
    "minimal": (
        "A bare floor of pale stone dust",
        "pale stone dust",
        "an almost featureless expanse of fine pale dust with only the faintest "
        "unevenness, a few very soft and very shallow undulations that barely catch "
        "the light. No distinct shapes, no clumps, no debris. The surface is quiet and "
        "nearly uniform, with only the gentlest variation in tone.",
        "pale warm grey, soft cream, faint cool shadow",
        True,
        "",
    ),
    "mushroom": (
        "A mossy forest floor with mushrooms",
        "moss with small mushroom caps",
        "soft moss cushions with a scattering of small mushroom caps pushing through. "
        "Caps are sparse and irregularly placed, some single, some in twos and threes, "
        "some tiny buttons, some open and flat. Cap sizes range from about six to "
        "about twenty pixels across, all mixed together with no regularity.",
        "deep moss green, warm cream mushroom caps, muted red-brown caps, cool shadow",
        True,
        "",
    ),
    "gothic": (
        "A floor of worn dark stone paving",
        "worn dark stone paving slabs",
        "rectangular stone slabs laid in a regular running-bond pattern, each slab "
        "slightly different in tone and wear. Slab joints are narrow and dark. Some "
        "slabs are chipped at the corners, some have a faint crack, some are more "
        "polished than their neighbours. Slabs are about sixty pixels wide and about "
        "forty tall.",
        "dark slate grey, cool blue-grey highlights, near-black joint shadow, faint "
        "violet tint",
        False,
        "rectangular slabs in running bond",
    ),
    "seashell": (
        "A shore of sand strewn with shells",
        "shore sand with scattered shells",
        "fine pale sand with a scattering of small broken shells and shell fragments "
        "resting on it. Shells are sparse and scattered at every angle, some whole, "
        "some broken, some half-buried. Shell sizes range from about six to about "
        "twenty-four pixels across, all mixed together with no regularity.",
        "pale cream sand, soft white shell, muted pink and grey shell, faint tan shadow",
        True,
        "",
    ),
    "succulent": (
        "A bed of fine angular grit",
        "fine angular grit with small fleshy leaf rosettes",
        "a loose bed of small sharp-edged grit stones with a few low rosettes of thick "
        "fleshy leaves growing through. Grit stones are angular and varied, packed "
        "unevenly, never in a grid, with visible dark gaps between them. Rosettes are "
        "sparse and irregularly placed, each a tight ring of short pointed leaves. "
        "Stone sizes range from about four to about sixteen pixels, all mixed together "
        "with no regularity. The grit must read as distinct sharp stones, not as a "
        "smooth mottled surface.",
        "muted sage green, dusty grey-green grit, pale blue-green highlights, cool "
        "shadow",
        True,
        "",
    ),
    "willow": (
        "A damp riverbank under a willow",
        "damp riverbank soil with willow roots",
        "soft wet soil with pale willow roots threading across it. The soil is uneven "
        "and lumpy, made of soft irregular mounds that merge into one another. Roots "
        "are sparse, wandering in loose curves, some thick, some hair-fine, some "
        "disappearing under the soil. Mound sizes range from about ten to about forty "
        "pixels across, all mixed together with no regularity.",
        "muted olive-brown soil, pale cream root, cool green-grey shadow, faint teal "
        "damp",
        True,
        "",
    ),
    "desert": (
        "A field of desert dunes",
        "wind-rippled desert sand",
        "long soft dune ridges and shallow wind ripples. Ridge crests are smooth and "
        "rounded, running in loose irregular curves that wander and merge, never "
        "parallel and never evenly spaced. Ridge spacing ranges from about twenty to "
        "about seventy pixels, all mixed together with no regularity.",
        "warm ochre sand, pale gold highlights, muted red-brown shadow, faint dusty rose",
        True,
        "",
    ),
    "cemetery": (
        "An old cemetery path of cracked stone paving",
        "old cracked stone paving",
        "irregular stone slabs laid in a loose pattern, each slab a different size and "
        "tone. Joints are wide and dark, some filled with moss. Some slabs are cracked, "
        "some sunken, some tilted. Slab sizes range from about thirty to about eighty "
        "pixels across, all mixed together with no regularity.",
        "cold grey stone, pale lichen highlights, dark green moss in the joints, "
        "near-black shadow",
        False,
        "irregular stone slabs",
    ),
    "house": (
        "A worn wooden cottage floor",
        "worn wooden floorboards",
        "long straight floorboards laid side by side, each board a slightly different "
        "tone and grain. Board joints are narrow and dark. Some boards have a knot, "
        "some a faint split, some are more worn than their neighbours. Boards are "
        "about thirty pixels wide and run the full height of the image.",
        "warm honey wood, pale grain highlight, deep brown shadow, faint amber varnish",
        False,
        "long parallel floorboards",
    ),
    "dark_cave": (
        "A cave floor of dark mineral rock",
        "dark mineral cave rock",
        "a rough fractured rock face with a scatter of small mineral flecks. The rock "
        "is broken into angular facets of wildly different sizes, some broad and flat, "
        "some narrow slivers, some split by a hairline crack, some almost smooth. "
        "Facets are packed tightly against each other with no gaps and no rounded "
        "outlines. Facet sizes range from about six to about sixty pixels across, all "
        "mixed together with no regularity. This is fractured rock, not a field of "
        "rounded stones.",
        "deep blue-grey rock, cool slate highlights, near-black shadow, faint teal "
        "mineral fleck",
        True,
        "",
    ),
    "night": (
        "A patch of night-dark soil",
        "night-dark soil",
        "uneven lumpy ground made of soft irregular mounds of earth that merge into "
        "one another. Each mound is lit from the top-left, so it has a gently lighter "
        "upper-left face and a gently darker lower-right face. Mound sizes range from "
        "about ten to about forty pixels across, all mixed together with no regularity.",
        "deep indigo-brown, muted blue-grey highlights, near-black shadow, faint violet "
        "tint",
        True,
        "",
    ),
    "autumn": (
        "A forest floor covered in autumn leaves",
        "a thick layer of fallen autumn leaves",
        "overlapping fallen leaves lying at every angle, some flat, some curled, some "
        "half-buried, some stacked three or four deep. Leaf sizes range from about ten "
        "to about forty pixels long, all mixed together with no regularity.",
        "warm amber, burnt orange, faded ochre, deep russet shadow",
        True,
        "",
    ),
    "snow": (
        "A field of wind-packed snow",
        "wind-packed snow",
        "a fine granular snow surface with shallow wind-carved ridges called sastrugi. "
        "The ridges are long, thin and low, running in loose wandering curves that "
        "cross and merge, never parallel and never evenly spaced. Between the ridges "
        "the snow is smooth and slightly dimpled. Ridge spacing ranges from about "
        "fifteen to about fifty pixels, all mixed together with no regularity. The "
        "surface is fine-grained, not made of large soft blobs.",
        "pale blue-white snow, soft white highlights, muted blue shadow, faint lavender "
        "tint",
        True,
        "",
    ),
    "pond": (
        "A wet pond bank",
        "wet pond bank mud",
        "soft wet mud with shallow puddles and a few scattered pebbles. The mud is "
        "uneven and lumpy, made of soft irregular mounds that merge into one another. "
        "Puddles are shallow and irregular, some large, some tiny, some barely visible. "
        "Mound sizes range from about ten to about forty pixels across, all mixed "
        "together with no regularity.",
        "muted olive-brown mud, cool green-grey highlights, dark teal shadow, faint "
        "blue puddle",
        True,
        "",
    ),
    "ruins": (
        "A floor of broken flagstone paving",
        "broken flagstone paving",
        "irregular flagstones laid in a loose pattern, many broken or missing. Joints "
        "are wide and dark, some filled with dirt. Some stones are cracked, some "
        "sunken, some tilted, some replaced by bare earth. Stone sizes range from about "
        "thirty to about eighty pixels across, all mixed together with no regularity.",
        "warm grey stone, pale sand highlights, dark earth in the joints, faint green "
        "moss",
        False,
        "irregular flagstones",
    ),
    "marsh": (
        "A patch of soft marsh mud",
        "soft marsh mud",
        "wet uneven mud with shallow standing water and a few thin reed stems. The mud "
        "is soft and lumpy, made of irregular mounds that merge into one another. Water "
        "sits in the low places in shallow irregular pools. Mound sizes range from "
        "about ten to about forty pixels across, all mixed together with no regularity.",
        "muted olive mud, cool green-grey highlights, dark teal shadow, faint rust reed",
        True,
        "",
    ),
    "fairy_glade": (
        "A lush mossy glade floor",
        "lush fine-grained moss scattered with tiny flowers",
        "a dense even mat of fine moss made of countless tiny overlapping shoots, with "
        "a sparse scattering of tiny flowers. The moss is fine-grained and even, like a "
        "closely cropped lawn of moss, not made of large rounded lumps and not made of "
        "radiating star-shaped clusters. Individual shoots are only two to six pixels "
        "across, all mixed together with no regularity. Flowers are tiny and "
        "irregularly placed, some single, some in twos and threes.",
        "bright moss green, warm yellow-green highlights, cool blue-green shadow, tiny "
        "pale pink and white flowers",
        True,
        "",
    ),
    "moonflower_grove": (
        "A pale mossy grove floor in moonlight",
        "pale fine-grained moss with moonflower blossoms",
        "a dense mat of fine pale moss made of countless tiny overlapping fronds and "
        "shoots, with a sparse scattering of small pale blossoms. The moss is "
        "fine-grained and feathery, not made of large rounded lumps. Frond clusters "
        "range from about three to about twelve pixels across, all mixed together with "
        "no regularity. Blossoms are tiny and irregularly placed, some open, some "
        "closed.",
        "pale blue-green moss, soft silver highlights, cool indigo shadow, pale cream "
        "blossom",
        True,
        "",
    ),
    "cathedral": (
        "A polished stone cathedral floor",
        "polished stone paving",
        "large square stone tiles laid in a regular grid, each tile a slightly "
        "different tone. Tile joints are narrow and dark. Some tiles are more polished "
        "than their neighbours, some have a faint vein. Tiles are about sixty pixels "
        "square.",
        "cool grey stone, pale silver highlights, deep blue-grey shadow, faint violet "
        "vein",
        False,
        "large square tiles in a grid",
    ),
    "rose_courtyard": (
        "A flagstone courtyard scattered with rose petals",
        "flagstone paving with fallen rose petals",
        "irregular flagstones laid in a loose pattern, with a scattering of fallen rose "
        "petals resting on them. Joints are narrow and dark. Petals are sparse and "
        "scattered at every angle, some flat, some curled. Stone sizes range from about "
        "thirty to about eighty pixels across, all mixed together with no regularity.",
        "warm grey stone, pale sand highlights, deep rose petal, muted green leaf",
        False,
        "irregular flagstones",
    ),
    "library": (
        "A dark wooden library floor",
        "dark wooden floorboards",
        "long straight floorboards laid side by side, each board a slightly different "
        "tone and grain. Board joints are narrow and dark. Some boards have a knot, "
        "some a faint split, some are more worn than their neighbours. Boards are about "
        "thirty pixels wide and run the full height of the image.",
        "deep walnut wood, warm amber grain highlight, near-black shadow, faint "
        "red-brown varnish",
        False,
        "long parallel floorboards",
    ),
    "observatory": (
        "A stone observatory floor with brass inlay",
        "stone paving with thin brass inlay lines",
        "large stone tiles laid in a regular grid with thin brass lines set into the "
        "joints. Tile joints are narrow and dark. Some tiles are more polished than "
        "their neighbours. Tiles are about sixty pixels square.",
        "cool blue-grey stone, pale silver highlights, deep navy shadow, warm brass line",
        False,
        "large square tiles with brass inlay",
    ),
    "kitchen": (
        "A floor of small square glazed tiles",
        "small square glazed tiles",
        "small square glazed tiles laid in a regular grid, alternating between two "
        "closely related pale tones. Tile joints are narrow and pale. Some tiles are "
        "slightly more worn or more glossy than their neighbours. Tiles are about "
        "forty pixels square.",
        "warm off-white tile, muted sage tile, pale grout, faint grey shadow",
        False,
        "small square tiles in a regular grid",
    ),
    "sewing_room": (
        "A woven fabric floor covering",
        "tightly woven fabric",
        "a tight even weave of crossing threads, with a faint irregular mottling in the "
        "dye. The weave is fine and regular, the threads running straight across and "
        "down. Some areas are slightly more worn and faded than others.",
        "muted dusty rose, warm cream thread, deep plum shadow, faint mauve",
        False,
        "a fine even woven weave",
    ),
    "witch_garden": (
        "A dark herb garden bed",
        "dark soil with scattered herb leaves",
        "loose dark soil with a scattering of small herb leaves and thin stems. The "
        "soil is uneven and lumpy, made of soft irregular mounds that merge into one "
        "another. Leaves are sparse and scattered at every angle. Mound sizes range "
        "from about ten to about forty pixels across, all mixed together with no "
        "regularity.",
        "deep purple-brown soil, muted olive herb, cool green-grey highlights, "
        "near-black shadow",
        True,
        "",
    ),
    "moon_altar": (
        "A flagstone floor with a carved circle",
        "flagstone paving with a shallow carved ring",
        "irregular flagstones laid in a loose pattern, with a shallow carved circular "
        "groove running across them. Joints are narrow and dark. The groove is worn and "
        "pale. Stone sizes range from about thirty to about eighty pixels across, all "
        "mixed together with no regularity.",
        "cool grey stone, pale silver highlights, deep blue-grey shadow, faint violet "
        "groove",
        False,
        "irregular flagstones",
    ),
    "primeval_jungle": (
        "An ancient jungle floor",
        "deep moss over ancient roots",
        "thick moss over thick woody roots that push up through it. The roots wander in "
        "loose curves, some thick, some thin, some disappearing under the moss. Moss "
        "cushions are soft and rounded, merging into one another. Root widths range "
        "from about eight to about thirty pixels, all mixed together with no "
        "regularity.",
        "deep jungle green, warm yellow-green highlights, cool blue-green shadow, dark "
        "brown root",
        True,
        "",
    ),
    "amber_basin": (
        "A basin of amber gravel",
        "warm amber gravel",
        "a loose bed of small irregular stones packed unevenly, never in a grid. Stones "
        "are rounded and varied, some large, some tiny, some half-buried. Stone sizes "
        "range from about four to about twenty pixels across, all mixed together with "
        "no regularity.",
        "warm amber, pale gold highlights, deep red-brown shadow, faint honey tint",
        True,
        "",
    ),
    "neon_alley": (
        "A wet asphalt alley floor",
        "wet asphalt",
        "coarse dark asphalt with shallow puddles catching coloured light. The asphalt "
        "is grainy and uneven, with a few faint cracks. Puddles are shallow and "
        "irregular, some large, some tiny. The surface is quiet and dark, with only the "
        "gentlest variation in tone.",
        "deep blue-black asphalt, cool grey highlights, faint magenta and cyan "
        "reflections, near-black shadow",
        False,
        "coarse asphalt grain",
    ),
    "circuit_garden": (
        "A metal plate etched with circuit traces",
        "brushed metal with fine circuit traces",
        "a flat brushed metal plate with thin etched circuit traces running across it. "
        "Traces run in straight lines with right-angle turns, some wide, some "
        "hair-fine, some ending in small round pads. The metal is evenly brushed, with "
        "only the gentlest variation in tone.",
        "cool teal-grey metal, pale silver highlight, deep blue-green shadow, faint "
        "copper trace",
        False,
        "straight circuit traces with right-angle turns",
    ),
    "lunar_base": (
        "A lunar surface of regolith",
        "lunar regolith dust",
        "fine grey dust with soft shallow craters and a few small scattered stones. "
        "Crater rims are soft and rounded, some large, some tiny, some barely visible. "
        "Stones are sparse and irregularly placed. Crater sizes range from about ten "
        "to about sixty pixels across, all mixed together with no regularity.",
        "cool grey dust, pale silver highlights, deep blue-grey shadow, faint warm tan "
        "stone",
        True,
        "",
    ),
    "starship": (
        "A metal starship deck",
        "metal deck panels",
        "large rectangular metal panels laid in a regular grid, each panel a slightly "
        "different tone. Panel joints are narrow and dark, with small round fasteners "
        "at the corners. Some panels are more scuffed than their neighbours. Panels are "
        "about sixty pixels wide and about forty tall.",
        "cool blue-grey metal, pale silver highlight, deep navy shadow, faint teal tint",
        False,
        "rectangular metal panels in a grid",
    ),
    "enchanted_court": (
        "A flagstone court with glowing runes",
        "flagstone paving with faint glowing runes",
        "irregular flagstones laid in a loose pattern, with a few faint glowing rune "
        "marks cut into them. Joints are narrow and dark. Runes are sparse and "
        "irregularly placed, some complete, some worn away. Stone sizes range from "
        "about thirty to about eighty pixels across, all mixed together with no "
        "regularity.",
        "cool grey-violet stone, pale silver highlights, deep indigo shadow, faint cyan "
        "glow",
        False,
        "irregular flagstones",
    ),
    "dragon_heath": (
        "A scorched heath of volcanic ash",
        "volcanic ash over cracked scorched earth",
        "a fine grey ash layer broken by a network of dark cracks in the scorched earth "
        "beneath, with a few faint ember flecks. The cracks wander and branch "
        "irregularly, some wide, some hairline, some closing up entirely. Between the "
        "cracks the ash is fine and grainy, not lumpy. Crack spacing ranges from about "
        "fifteen to about sixty pixels, all mixed together with no regularity.",
        "warm grey ash, pale bone highlights, deep charcoal shadow, faint dull ember",
        True,
        "",
    ),
    "lost_city": (
        "A sun-bleached flagstone street",
        "sun-bleached flagstone paving",
        "irregular flagstones laid in a loose pattern, worn smooth by weather. Joints "
        "are wide and pale, some filled with sand. Some stones are cracked, some "
        "sunken, some almost buried. Stone sizes range from about thirty to about "
        "eighty pixels across, all mixed together with no regularity.",
        "pale warm stone, bleached cream highlights, muted tan shadow, faint dusty green",
        False,
        "irregular flagstones",
    ),
    "sunken_temple": (
        "A submerged temple floor",
        "stone paving under a film of silt",
        "irregular stone slabs laid in a loose pattern, half covered by a film of pale "
        "silt. Joints are wide and dark. Some slabs are cracked, some tilted, some "
        "almost buried. Stone sizes range from about thirty to about eighty pixels "
        "across, all mixed together with no regularity.",
        "muted teal stone, pale silt highlights, deep blue-green shadow, faint rust "
        "algae",
        False,
        "irregular stone slabs",
    ),
    "zen_garden": (
        "A raked zen garden of white gravel",
        "raked pale gravel",
        "fine pale gravel raked into long parallel grooves that curve gently around a "
        "few larger stones. The grooves are even and deliberate, running in smooth "
        "arcs. A few larger stones sit in the gravel, each with a soft ring raked "
        "around it. Grooves are about eight pixels apart.",
        "pale warm grey gravel, soft cream highlight, muted tan shadow, faint green moss",
        False,
        "long raked parallel grooves",
    ),
    "bamboo_grove": (
        "A bamboo grove floor",
        "bamboo leaf litter over bare earth",
        "a loose layer of long narrow bamboo leaves over bare earth, with a few fallen "
        "culm sections. Leaves lie at every angle, some flat, some curled, some "
        "stacked. Leaf lengths range from about twenty to about sixty pixels, all mixed "
        "together with no regularity.",
        "muted yellow-green leaf, pale straw highlight, cool olive shadow, faint brown "
        "earth",
        True,
        "",
    ),
    "sakura_courtyard": (
        "A flagstone courtyard scattered with cherry petals",
        "flagstone paving with fallen cherry petals",
        "irregular flagstones laid in a loose pattern, with a scattering of small pale "
        "cherry petals resting on them. Joints are narrow and dark. Petals are sparse "
        "and scattered at every angle, some single, some in small drifts. Stone sizes "
        "range from about thirty to about eighty pixels across, all mixed together with "
        "no regularity.",
        "warm grey stone, pale sand highlights, soft pink petal, muted green leaf",
        False,
        "irregular flagstones",
    ),
    "cloud_sanctuary": (
        "A floor of soft cloud",
        "soft billowing cloud",
        "soft rounded billows of cloud that merge into one another. Each billow is lit "
        "from the top-left, so it has a gently lighter upper-left face and a gently "
        "darker lower-right face. Billow sizes range from about fifteen to about sixty "
        "pixels across, all mixed together with no regularity.",
        "pale blue-white cloud, soft white highlights, muted lavender shadow, faint "
        "warm cream",
        True,
        "",
    ),
}

# Themes that are deliberately near-uniform. Asking for visible forms here would
# break the theme, so the tonal paragraph is replaced.
SPARSE = {"minimal", "neon_alley"}

ORGANIC_RULE = (
    "NON-REPETITION IS THE MOST IMPORTANT RULE: this is organic ground, so it must "
    "NOT repeat, tile visibly, or fall into any pattern. No two areas may look alike. "
    "No grid, no honeycomb, no evenly spaced shapes, no uniform bubble or cobblestone "
    "field, no repeating motif, no wallpaper effect. The eye must never be able to "
    "find a unit that repeats. Every region of the image is unique and irregular, the "
    "way real {material} is."
)

MANUFACTURED_RULE = (
    "REGULARITY IS CORRECT HERE: this is a manufactured surface, so a repeating unit "
    "is expected and desirable, namely {unit}. Keep the repeat clean and consistent, "
    "the way real {material} is laid. Do not let the pattern drift, warp, or dissolve "
    "into noise, and do not add organic clutter on top of it."
)

TONAL_RULE = (
    "TONAL RANGE: keep it MODERATE and calm. All the forms are similar in brightness, "
    "because this is quiet ground that a small character must stay clearly visible "
    "against. Do not make it dramatic or high contrast. The shading on each form is "
    "gentle, only a few steps lighter and darker than its base tone."
)

SPARSE_TONAL_RULE = (
    "TONAL RANGE: keep it EXTREMELY calm and nearly uniform. This floor is meant to be "
    "almost featureless, so the whole image sits inside a very narrow band of "
    "brightness. Do not make it dramatic or high contrast. A small character must stay "
    "clearly visible against it."
)

AVOID_ORGANIC = (
    "repeating pattern, tiling, uniform bubble or cobblestone field, evenly spaced "
    "round shapes, honeycomb, grid, wallpaper, rounded stones packed together, pebble "
    "mosaic, radiating star-shaped clusters, starburst rosettes, large soft blobs, "
    "smooth amoeba shapes, marbled swirls, large contrasting regions, big dark "
    "blotches, dramatic lighting, strong cast shadows, scattered loose pebbles, "
    "gravel, isolated bright dots, isolated dark dots, confetti debris, "
    "high-frequency black-and-white noise, camouflage pattern"
)

AVOID_MANUFACTURED = (
    "organic clutter, scattered debris, moss patches, loose leaves, puddles, "
    "large contrasting regions, big dark blotches, dramatic lighting, strong cast "
    "shadows, isolated bright dots, isolated dark dots, confetti debris, "
    "high-frequency black-and-white noise, camouflage pattern, warped or drifting "
    "pattern, melting edges"
)

PIXEL_RULE = (
    "PIXEL ART RULES: Hard-edged pixels, no anti-aliasing, no blur, no photographic "
    "texture. Discrete color ramps only, each form shaded with three to four flat "
    "tones. Maximum 64 colors total. Modern hand-crafted pixel art, the kind used in "
    "contemporary indie games."
)

EXCLUSIONS = (
    "No text, no UI, no characters, no creatures, no border, no frame, no vignette."
)


def prompt(theme):
    """The full generation prompt for one background theme."""
    if theme not in THEMES:
        raise KeyError(f"no prompt recipe for theme {theme!r}")
    subject, material, surface, palette, organic, unit = THEMES[theme]
    if organic:
        pattern = ORGANIC_RULE.format(material=material)
        avoid = AVOID_ORGANIC
    else:
        pattern = MANUFACTURED_RULE.format(unit=unit, material=material)
        avoid = AVOID_MANUFACTURED
    tonal = SPARSE_TONAL_RULE if theme in SPARSE else TONAL_RULE
    return "\n\n".join([
        f"Modern pixel art GROUND TEXTURE for a virtual pet game. {subject} seen "
        f"from directly above, filling the entire square frame edge to edge.",
        "THIS IS A FLAT MATERIAL SAMPLE, NOT A SCENE. No vignette, no frame, no "
        "border, no darkened edges, no objects arranged around the outside. Equally "
        "bright and equally detailed from corner to corner.",
        f"MATERIAL: One single material, {material}. Uniform in character across the "
        f"whole image.",
        pattern,
        f"SURFACE: {surface}",
        tonal,
        f"VARIATION: subtle shifts in hue between areas. Gentle, closely spaced tones.",
        f"STRICTLY AVOID: {avoid}.",
        PIXEL_RULE,
        f"PALETTE: {palette}. All closely spaced tones of one another.",
        EXCLUSIONS,
    ])


def missing():
    """Themes that still need a prompt recipe.

    Themes already adopted through the approved-source registry are done, so
    they are not missing even though they have no recipe here.
    """
    import approved_sources
    import asset_build
    return [
        theme for theme in asset_build.BACKGROUNDS
        if theme not in THEMES and not approved_sources.is_approved(theme)
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
            kind = "organic" if THEMES[theme][4] else "manufactured"
            print(f"{theme:22} {kind}")
        return 0

    if args.theme:
        print(prompt(args.theme))
        return 0

    if args.all:
        if not args.out:
            parser.error("--all needs --out")
        args.out.mkdir(parents=True, exist_ok=True)
        for theme in THEMES:
            (args.out / f"prompt_{theme}.txt").write_text(
                prompt(theme) + "\n", encoding="utf-8"
            )
        print(f"wrote {len(THEMES)} prompts to {args.out}")
        return 0

    parser.error("choose --list, --theme, or --all")


if __name__ == "__main__":
    raise SystemExit(main())
