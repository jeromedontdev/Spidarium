"""Validate official release images and assemble the static Pages input/output."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

from package_release import REGIONS


def install_firmware(root: Path, archive: Path) -> None:
    destination = root / "site" / "firmware"
    with zipfile.ZipFile(archive) as release:
        prefix = "spidarium-v1.0.0/"
        manifest = json.loads(release.read(prefix + "manifest.json"))
        if manifest["version"] != "1.0.0" or manifest["chip"] != "ESP32-S3":
            raise ValueError("Wrong release version/chip")
        images = {}
        for item in manifest["files"]:
            name = item["path"]
            if name not in REGIONS or name in images:
                raise ValueError("Unexpected or duplicate image")
            data = release.read(prefix + name)
            start, end = REGIONS[name]
            if item["offset"] != start or len(data) != item["size"] or not data:
                raise ValueError("Invalid image range/size")
            if start + ((len(data) + 4095) // 4096) * 4096 > end:
                raise ValueError("Image erase range overlaps a protected partition")
            if hashlib.sha256(data).hexdigest() != item["sha256"]:
                raise ValueError("Image SHA-256 mismatch")
            images[name] = data
        if set(images) != set(REGIONS):
            raise ValueError("Missing full-flash image")
        destination.mkdir(parents=True, exist_ok=True)
        for name, data in images.items():
            (destination / name).write_bytes(data)
        (destination / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("WEBSITE firmware: five verified images, NVS excluded")


def copy_art(root: Path) -> None:
    destination = root / "site" / "dist" / "assets"
    destination.mkdir(parents=True, exist_ok=True)
    sources = sorted((root / "assets").glob("*.png"))
    if len(sources) != 134:
        raise ValueError("Expected the complete 134-image release artwork")
    for source in sources:
        shutil.copyfile(source, destination / source.name)
    (root / "site" / "dist" / "index.html").write_text(
        '<!doctype html><meta charset="utf-8"><meta http-equiv="refresh" content="0;url=site/">'
        '<title>Spidarium</title><a href="site/">Open Spidarium</a>\n', encoding="utf-8")
    print("WEBSITE artwork: 134 authentic PNGs and subpath-safe root redirect")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", nargs="?", type=Path)
    parser.add_argument("--assets-only", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    if args.assets_only:
        copy_art(root)
    elif args.archive:
        install_firmware(root, args.archive)
    else:
        parser.error("Provide a release ZIP or --assets-only")


if __name__ == "__main__":
    main()
