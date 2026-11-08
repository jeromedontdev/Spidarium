"""Package the verified board images without touching the NVS partition."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import zipfile


REGIONS = {
    "bootloader.bin": (0, 0x8000),
    "partition-table.bin": (0x8000, 0x9000),
    "ota_data_initial.bin": (0x10000, 0x12000),
    "spidarium.bin": (0x20000, 0x320000),
    "storage.bin": (0x920000, 0xF20000),
}


def image_manifest(build: Path, version: str) -> dict:
    description = json.loads((build / "project_description.json").read_text())
    if description["project_name"] != "spidarium" or description["project_version"] != version:
        raise ValueError("Built project/version does not match the requested release")
    if description["target"] != "esp32s3":
        raise ValueError("Release is only for ESP32-S3")
    config = json.loads((build / "flasher_args.json").read_text())
    files = []
    seen = set()
    for address, relative in config["flash_files"].items():
        source = (build / relative).resolve()
        if not source.is_relative_to(build.resolve()):
            raise ValueError("Flash image escapes the build directory")
        name = source.name
        if name not in REGIONS or name in seen:
            raise ValueError(f"Unexpected/duplicate flash image: {name}")
        seen.add(name)
        start, end = REGIONS[name]
        offset = int(address, 0)
        data = source.read_bytes()
        if offset != start or not data or offset + ((len(data) + 4095) // 4096) * 4096 > end:
            raise ValueError(f"Unsafe offset or partition overflow: {name}")
        if name == "spidarium.bin":
            if data[0] != 0xE9 or len(data) < 112 or struct.unpack_from("<I", data, 32)[0] != 0xABCD5432:
                raise ValueError("Invalid ESP application descriptor")
            binary_version = data[48:80].split(b"\0", 1)[0].decode("ascii")
            binary_project = data[80:112].split(b"\0", 1)[0].decode("ascii")
            if binary_version != version or binary_project != "spidarium":
                raise ValueError("Application binary has the wrong version/project")
        files.append({"path": name, "offset": offset, "size": len(data),
                      "sha256": hashlib.sha256(data).hexdigest()})
    if seen != set(REGIONS):
        raise ValueError("The full-flash image set is incomplete")
    return {"version": version, "chip": "ESP32-S3", "baud": 460800,
            "flashSize": "16MB", "flashMode": "dio", "flashFreq": "80m",
            "preservesNvs": True, "files": sorted(files, key=lambda item: item["offset"])}


def copy_licenses(source: Path, destination: Path) -> None:
    if not source.is_dir():
        raise ValueError(f"Missing license directory: {source}")
    for path in sorted(source.rglob("*")):
        relative = path.relative_to(source)
        name = path.name.upper()
        if path.is_file() and (name.startswith(("LICENSE", "COPYING", "NOTICE", "COPYRIGHT"))
                               or "LICENSES" in (part.upper() for part in relative.parts[:-1])):
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)


def package(root: Path, version: str) -> Path:
    build = root / "build"
    manifest = image_manifest(build, version)
    output = root / "release" / f"spidarium-v{version}"
    web = root / "site" / "firmware"
    output.mkdir(parents=True, exist_ok=True)
    web.mkdir(parents=True, exist_ok=True)
    config = json.loads((build / "flasher_args.json").read_text())
    by_name = {Path(path).name: build / path for path in config["flash_files"].values()}
    for item in manifest["files"]:
        shutil.copyfile(by_name[item["path"]], output / item["path"])
        shutil.copyfile(by_name[item["path"]], web / item["path"])
    contents = json.dumps(manifest, indent=2) + "\n"
    (output / "manifest.json").write_text(contents, encoding="utf-8")
    (web / "manifest.json").write_text(contents, encoding="utf-8")
    notes = root / "docs" / f"RELEASE_v{version}.md"
    shutil.copyfile(notes, output / "README.md")
    shutil.copyfile(root / "LICENSE", output / "LICENSE")
    shutil.copyfile(root / "docs" / "BROWSER_PARITY.md", output / "BROWSER_PARITY.md")
    schema = root / "site" / "save-schema.json"
    if not schema.is_file():
        raise ValueError("Capture the compiled save schema before packaging")
    shutil.copyfile(schema, output / "save-schema.json")
    description = json.loads((build / "project_description.json").read_text())
    notices = output / "third-party"
    notices.mkdir(exist_ok=True)
    sdk_license = Path(description["idf_path"]) / "LICENSE"
    if not sdk_license.is_file():
        raise ValueError("ESP-IDF license missing")
    shutil.copyfile(sdk_license, notices / "ESP-IDF-LICENSE")
    copy_licenses(sdk_license.parent / "components", notices / "ESP-IDF-components")
    copy_licenses(root / "managed_components", notices / "managed-components")
    additional = root / "tools" / "licenses"
    for name in ("LICENSE-FatFs.txt", "LICENSE-Montserrat.txt", "LICENSE-FontAwesome.txt"):
        if not (additional / name).is_file() or not (additional / name).stat().st_size:
            raise ValueError(f"Missing additional firmware notice: {name}")
    copy_licenses(additional, notices / "additional-notices")
    archive = output.parent / (output.name + ".zip")
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as bundle:
        for path in sorted(output.rglob("*")):
            if path.is_file():
                bundle.write(path, path.relative_to(output.parent))
    print(f"PACKAGED {archive.name}: {len(manifest['files'])} verified images; NVS excluded")
    return archive


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", default="1.1.1")
    args = parser.parse_args()
    package(Path(__file__).resolve().parents[1], args.version)


if __name__ == "__main__":
    main()
