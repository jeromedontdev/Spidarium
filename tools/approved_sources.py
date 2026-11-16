"""Registry of hash-pinned approved background and border sources.

Most backgrounds and borders are drawn procedurally by `terrain_art`. A few are
imported unchanged from a reviewed source image because procedural drawing
cannot reproduce their texture. Those imports are pinned here by pixel hash so
the published art can never drift from what was approved.

To adopt a new source, generate it, convert it with `import_background.py
--install` (floors) or `import_border.py --install` (borders), then add its
theme and pixel hash to `APPROVED_FLOORS` or `APPROVED_BORDERS`. The hash is
printed by the import tool.
"""

import hashlib
from pathlib import Path

from PIL import Image

SOURCES = Path(__file__).resolve().parents[1] / "assets" / "sources"

# theme -> (source filename, sha256 of the decoded 240x240 RGBA pixels)
APPROVED_FLOORS = {
    "crystal": (
        "crystal_floor.png",
        "89d076f4219d5839d72f9b137a0f893647177223f745ee95887a5886f76f8c6b",
    ),
    "woodland": (
        "woodland_floor.png",
        "345c34f2111db12ae78d5b320c6c0d02144b5d94133b5467708405e3e8581f88",
    ),
        "cork": ("cork_floor.png", "bfa565adff46a39182701434246b4da90abcc313fc02cb42e5a4e93a14f08b3c"),
    "leaf_litter": ("leaf_litter_floor.png", "2f1b41dc2b731d2bec9e285c6c696908495cef5f97405dcc8ec0fd2100b510d3"),
    "sand": ("sand_floor.png", "6c840ef5aa3ca4059971440716334bec1a99945fc97ce16be90f65bcc78b2f11"),
    "fern": ("fern_floor.png", "6f2cb7d0cee49137f829a59687ba9c7c01646fa0edad0b1bdeb99c6fd3ea4a51"),
    "orchid": ("orchid_floor.png", "9ffb506b115c2bd75b40243eabe926b0df683df026234fb66863c88093d8df3e"),
    "minimal": ("minimal_floor.png", "89e4cb03ed20d3aac7de6a1fb23e6036a6065281f2ee990600c87e5281085d3a"),
    "mushroom": ("mushroom_floor.png", "0971dd551fcafabbfea4508fec3fb8f4fe72d74138a3e6eb9ccb272eb5717582"),
    "gothic": ("gothic_floor.png", "7f52c955cdd80bd79056490ed797e6fa8900306be4e7039561cd1361bfcc161c"),
    "seashell": ("seashell_floor.png", "a937f0a37333ad94de84350023e884870f75d19336aa92da7d04410264c1b983"),
    "succulent": ("succulent_floor.png", "be7995d6b08b4d4f8a5d32add38f9381458dad016381d7e518d26c147f8ff98d"),
    "willow": ("willow_floor.png", "678c5d7e17531c7c81ef9f53a882e35b24566a9d75d55b94c1f93a017baa5b69"),
    "desert": ("desert_floor.png", "8259177977b0c8e2dd587cd8131e99aad109d688242f866e27829684e07ac9dd"),
    "cemetery": ("cemetery_floor.png", "8e7a9c28e6fa91e9e4b36d4c7eaa662c5bd5ec97877171054e92c1b60c495fb5"),
    "house": ("house_floor.png", "4856d572f8373669e7fa8636175dc402b30379f2a8bea3fc0e82b2b45fbe98c7"),
    "dark_cave": ("dark_cave_floor.png", "2dd10970a1f13f72d57be843acc27f94e3f663112ace257df418f610313347f0"),
    "night": ("night_floor.png", "f43e48fc7ba53d3aa74dc587516449cda849796385c7b7a5fcb0c99c62dcb0a0"),
    "autumn": ("autumn_floor.png", "51521fdce4ae13899a3e21a2fbb700de6cc363046ffb0cf99921dc5df3e0f17f"),
    "snow": ("snow_floor.png", "ff0c459a8a5e33ca7a6ec3c07b0f211d0af6ff1a3f07910cc288d7d4da553f6b"),
    "pond": ("pond_floor.png", "8527e8eae9edc6597b4b39cc119b1b073e887dce0224fe94f36c5d0d4d9b91c8"),
    "ruins": ("ruins_floor.png", "e3ab6907a30f08e2426d8a752e7a241a8c16a3552002e1777143d9914c5519ec"),
    "marsh": ("marsh_floor.png", "faedf31ea6b17700e250e7d6f8ed380a85eccd65fd29d67031059fe5aacda4fe"),
    "fairy_glade": ("fairy_glade_floor.png", "175d9918526a2be27a416ea5dd5fe60e3983373611847898e568f653959b7636"),
    "moonflower_grove": ("moonflower_grove_floor.png", "5b057d0decadd182c7780a4b672c9ff234558c60d5c9e20b95a37d65f5016f0e"),
    "cathedral": ("cathedral_floor.png", "dd317682331fa8a2fe0270d719b7522db14350c68239ef4acc4a2908f2b35aaa"),
    "rose_courtyard": ("rose_courtyard_floor.png", "09fd15796921839fc0290fb16383042355713fea20ee90aa9cf159ae662f9a9b"),
    "library": ("library_floor.png", "7656b44511902d20701b1924a1134f6a74f16f052514b9a3ef983ed6db2b9c4b"),
    "observatory": ("observatory_floor.png", "f5646663e65bacc2e2eafb969e3a6866a0c2f24f20d2a8c8ba26359b8230865b"),
    "kitchen": ("kitchen_floor.png", "43c8a883a23f814c5d9634e5910b3b035552196390e8b5ba201ef1ca2c7ae17e"),
    "sewing_room": ("sewing_room_floor.png", "59ceb17a0addf5fba6dda01606e63b2cf3ddff5bcbb056ad04da61a7c7b39988"),
    "witch_garden": ("witch_garden_floor.png", "8bb4278871b421b5beccb04e2e4b999a73ad7ee9387515c7d3f28f55bc7f17af"),
    "moon_altar": ("moon_altar_floor.png", "a92cd22b7af0b0e841db2e10d2d3871e1979fe94cf5d58c2eb5243cfb47d64ed"),
    "primeval_jungle": ("primeval_jungle_floor.png", "dcbb39e9656f2b0091e4a3f39e7a6f5b48907c1fa7eb88dd8f09218a0e0d28f3"),
    "amber_basin": ("amber_basin_floor.png", "8a4c19d736a10ce4baabe1e7ee8942c15c7fbb00aa3f8bd1b9e76b655169e1e1"),
    "neon_alley": ("neon_alley_floor.png", "ba64b404655ba8f182692a8c28e04b1809d9b1e87c6f01848f47eb322cd78109"),
    "circuit_garden": ("circuit_garden_floor.png", "178818b1c367fbcb820d63daa4861356fff505d761838011403b1d79d71719e3"),
    "lunar_base": ("lunar_base_floor.png", "0e42d9fd1bfed4ab393e3fb029056352c3bb94d443f02bbd95e1258888826388"),
    "starship": ("starship_floor.png", "75bfe78b03e38c50098d70d3099b0bd2f49032a5e32bc9c67548796ea49dc452"),
    "enchanted_court": ("enchanted_court_floor.png", "36cc68221276571fa137e6a0ae3d74dc1d1c31fbd66729804234907d164ea06a"),
    "dragon_heath": ("dragon_heath_floor.png", "9547d07c3e763ad242718221c45f584fe927a02b3f639a9ce8a7ba200682cbbc"),
    "lost_city": ("lost_city_floor.png", "43a03765d2d1d8f21324c046f83137a36c3a456aaf98da085653727e9159748a"),
    "sunken_temple": ("sunken_temple_floor.png", "bb3843e30a0fab040990f8b6f04f37f9d50d7452fe2e482dc048d485ad2911c0"),
    "zen_garden": ("zen_garden_floor.png", "530e85fa0ead0d9945c10896daa40767b4fcb1854f6860031d0818e9592d3b1e"),
    "bamboo_grove": ("bamboo_grove_floor.png", "f71e5d9688ab9ca390a3c4d0fb58a5378d7b58c89f4f3ef7c5242a29d2ca81b2"),
    "sakura_courtyard": ("sakura_courtyard_floor.png", "8731cb818cd745b51d08999919702ee242a24e38a89d027e5ac03043a68ffa47"),
    "cloud_sanctuary": ("cloud_sanctuary_floor.png", "44be16ea784fb6e550f7188df93faa94ecb9d08d2eeb4203bb3762132800cacf"),
}


def floor(theme):
    """Return the approved floor for `theme`, or None if it is procedural."""
    entry = APPROVED_FLOORS.get(theme)
    if entry is None:
        return None
    filename, expected = entry
    path = SOURCES / filename
    if not path.exists():
        raise ValueError(f"{theme}: approved source {filename} is missing")
    with Image.open(path) as source:
        source.load()
        if (source.format != "PNG" or source.mode != "RGBA"
                or source.size != (240, 240)):
            raise ValueError(
                f"{theme}: source must be a 240x240 RGBA PNG, "
                f"got {source.format} {source.mode} {source.size}"
            )
        actual = hashlib.sha256(source.tobytes()).hexdigest()
        if actual != expected:
            raise ValueError(
                f"{theme}: source pixels differ from the approved floor "
                f"(expected {expected}, got {actual})"
            )
        return source.copy()


def is_approved(theme):
    return theme in APPROVED_FLOORS


# theme -> (source filename, sha256 of the decoded 240x240 RGBA pixels)
#
# Border sources are authored at 120x120 logical pixels and enlarged 2x with
# nearest-neighbour, so the published 240x240 frame is exactly the source with
# every pixel doubled. The hash below is of that published 240x240 result.
APPROVED_BORDERS = {
    "amber_basin": ("amber_basin_border.png", "8aafee4d6f1b13f664194a766e714c2aba89abc605f97c26d9df617109799e19"),
    "autumn": ("autumn_border.png", "b5b958ccf2fba3dc1355852b078f08adfafc7c2a34cc905fce0484c8a1a02abc"),
    "bamboo_grove": ("bamboo_grove_border.png", "1afe9b903e7c546c05731601093a3ee070fa7d7e3a88eee752ecd997bf0e7749"),
    "cathedral": ("cathedral_border.png", "9790bf710804fd4dcb01fae81ed24aeb1145feae25731a6a4370b6c7d728975f"),
    "cemetery": ("cemetery_border.png", "14c3c327a79e5106e5c4ce9ecaec8d1a21a34ed4841c02651460a2ccd059735e"),
    "circuit_garden": ("circuit_garden_border.png", "f5fbc0c7cb60d6b331d92220f52af05e60e2e395cb9580dd2d47f0147e44c981"),
    "cloud_sanctuary": ("cloud_sanctuary_border.png", "62e2fcde636f562f1af1da424a600e00590a17b69f72c4ab3ef69ec6dafff1eb"),
    "cork": ("cork_border.png", "aa295165bbeb56d1fe4102ed20f92a1c931f7ef1f2a1bab2d01a9aba9e73edc9"),
    "crystal": ("crystal_border.png", "3bc5cb017ac8aa7b5270fe59cfc66b1387d51898143862d731143481fb93598c"),
    "dark_cave": ("dark_cave_border.png", "966a00d3168fdf87d4a30421a8a5fc1f0f72b8f9b19d20411fdd91988e42f5b6"),
    "desert": ("desert_border.png", "2f4058d51433fa27abb3ff6729bea619d27da4555831a2224594078facfacca6"),
    "dragon_heath": ("dragon_heath_border.png", "7566737666e450d4b8331c9de6e0a19f9e8b15422d82ebf59a0ea0b9cc248f56"),
    "enchanted_court": ("enchanted_court_border.png", "bdc50de05bb76eff40e6d357a17d80ab553bd3833f0d68c2dcc35985b602ff44"),
    "fairy_glade": ("fairy_glade_border.png", "3876ca0584412043e813052453813c48ecda6810e9ef539191a7e21c700af463"),
    "fern": ("fern_border.png", "7b2d6dac712129bbbe08fdddd1d3bee55fc8650457519895a703d50f6ce48b86"),
    "gothic": ("gothic_border.png", "b6f2df2fdee284cffd7ba795f1aa640aadd453df7dd3e528a15762838ee4f099"),
    "house": ("house_border.png", "dfb32c4fb9b598b7dc907325c0900fe4766f73e5d30e38ffaea0680cd92e00bf"),
    "kitchen": ("kitchen_border.png", "26e5d000dfc91d09a0cda4eac9bc0348c38fcab14d072944c1a42bee87a258c1"),
    "leaf_litter": ("leaf_litter_border.png", "23dfd0a2f948884c92142b21a17ab55e57a50671daa4fc6c89e9d64158c5cd1f"),
    "library": ("library_border.png", "ad3beb42bf89fe25f801b2b5312281bb62d46841b09e131ebc838bbc84f4d2c8"),
    "lost_city": ("lost_city_border.png", "a0eb188b227e100ac026223c3b1c4c0db198ca792260b0fea2dc8d0106d14811"),
    "lunar_base": ("lunar_base_border.png", "f0596af31710429101039951f9a2fd9188820029cf6184386be7b4dfefd9e034"),
    "marsh": ("marsh_border.png", "441c6cdd761caea972a3d19addd57542edb6414c75f4b5a09021251b43379bd9"),
    "minimal": ("minimal_border.png", "aab9e1207faf05316b4ec58b172e774b57fedde5d7672575b2700c3da33d0e48"),
    "moon_altar": ("moon_altar_border.png", "da501453e19a4c177a01ab0e82c33684d5e0228015a13e328e037cad8f5e48d3"),
    "moonflower_grove": ("moonflower_grove_border.png", "97e7dc063679dd5317456d49ab6debb9894401794aef9a1fba55b917ace34473"),
    "moss": ("moss_border.png", "ac24829fa37058028b319dcc7e4893ce4b8b030f7836806b7f5f2e6f24b4b72c"),
    "mushroom": ("mushroom_border.png", "7973d792c424d0350627f779b1e02c1cb27e90c5159b1edf9b650248eeb6998a"),
    "neon_alley": ("neon_alley_border.png", "a257202e18d41cf289eff07a01039d4073ed1123483aaa65ea62eec274ca90c3"),
    "night": ("night_border.png", "a4fbdef4cdeaf0d2f924340c855e4a3a890748ba1e5828c7fd476ae1c793318a"),
    "observatory": ("observatory_border.png", "afbf07ae7089323b78effb0db4463e52212d60d3ec17ff80c15e3cf1373e6f3d"),
    "orchid": ("orchid_border.png", "8e9c548472db8c31cfdd5b512254ef7634776e7d013d6b0a9b1e722b68063b12"),
    "pond": ("pond_border.png", "63db561e3fe533df71369017a27713447511ab2f9e7cf6ff7b698e22f4ef742d"),
    "primeval_jungle": ("primeval_jungle_border.png", "b351b58f3f747e4a768eb77056dd4df16bf8118eebdfc17528e2c92623363345"),
    "rose_courtyard": ("rose_courtyard_border.png", "0b904dbdbf11e44fea418db111cc0fb31fe9d00e9f3309484ab6f9565ebb3ecf"),
    "ruins": ("ruins_border.png", "d5a113444bf0e6216dccc40e57a18a80a35c885c2b81f26d6a9ecfe1126113dc"),
    "sakura_courtyard": ("sakura_courtyard_border.png", "17b81d55f5eee88211f36308046361a7744fac4d6938aeeb66fa5c04058ecc83"),
    "sand": ("sand_border.png", "639dbecf016361bcfabeed465ba67b897ee7e1c36f8d47a5efed94ae295949ff"),
    "seashell": ("seashell_border.png", "582f96fb70c24931162f63fdd328be16243aae0947532f99fb1ec259c931ad61"),
    "sewing_room": ("sewing_room_border.png", "7d3b5a52782421cff1d27476572edd8b88a65d35fd915344086ddd2bf5d0e85a"),
    "snow": ("snow_border.png", "f501b2725530a099316e9af8b5dcf88bb1537fad5eb5dca20c3bbb728e7605e5"),
    "starship": ("starship_border.png", "ea71efe15535ceba5b1b4ee2f0d6e8819511b6fc9a1b83713cf155ec60252d2d"),
    "succulent": ("succulent_border.png", "4e4082573865142b8556d360a8bedaa358974f6af72d52b2c4884bdd3454274f"),
    "sunken_temple": ("sunken_temple_border.png", "c6e35502f6e5bddec14bbb76989db2a543319d2404a951fc933b0236f4ada3a8"),
    "willow": ("willow_border.png", "98e9b568d35400691db807084f9f14c6a8c98820f30ab57d124a14119c8a14e9"),
    "witch_garden": ("witch_garden_border.png", "6772dd5db01e06cb8def1add63d769d515cbf09aa5d81ef0addd653b1046679e"),
    "woodland": ("woodland_border.png", "a8e977e70cac939b3b9acd5a733e707969b58c308d5ac8ae58b7ef4878044a71"),
    "zen_garden": ("zen_garden_border.png", "dcb774e8a547948923a1cf08fc287d74f021b0780cafe12217034b317cfe4bd1"),
}

# Crystal was originally fitted from a PixelLab frame by nine-slicing an 18px
# rim down to 8px. It is now a plain approved import like every other theme, so
# this set is empty; it is kept so callers do not need a special case.
BORDER_SPECIAL = set()


def border(theme):
    """Return the approved 240x240 border for `theme`, or None if procedural."""
    entry = APPROVED_BORDERS.get(theme)
    if entry is None:
        return None
    filename, expected = entry
    path = SOURCES / filename
    if not path.exists():
        raise ValueError(f"{theme}: approved source {filename} is missing")
    with Image.open(path) as source:
        source.load()
        if source.format != "PNG" or source.mode != "RGBA":
            raise ValueError(
                f"{theme}: source must be an RGBA PNG, "
                f"got {source.format} {source.mode}"
            )
        if source.size == (120, 120):
            image = source.resize((240, 240), Image.Resampling.NEAREST)
        elif source.size == (240, 240):
            image = source.copy()
        else:
            raise ValueError(
                f"{theme}: source must be 120x120 or 240x240, got {source.size}"
            )
    actual = hashlib.sha256(image.tobytes()).hexdigest()
    if actual != expected:
        raise ValueError(
            f"{theme}: source pixels differ from the approved border "
            f"(expected {expected}, got {actual})"
        )
    return image


def is_border_approved(theme):
    return theme in APPROVED_BORDERS or theme in BORDER_SPECIAL
