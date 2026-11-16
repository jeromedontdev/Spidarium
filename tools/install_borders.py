"""Install reviewed border candidates and register their pixel hashes.

This is the deliberate adoption step that `border_batch.py` never performs. It
reads the batch's `report.json`, re-runs `import_border.py --install` for each
converted theme so the stored source is the authored 120x120 logical art, then
prints the registry lines to paste into `approved_sources.APPROVED_BORDERS`.

Usage:
    python tools/install_borders.py scratch/borders_all/report.json
    python tools/install_borders.py scratch/borders_all/report.json --write
"""

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
REGISTRY = TOOLS / "approved_sources.py"


def run(command):
    result = subprocess.run(
        command, cwd=TOOLS, capture_output=True, text=True, encoding="utf-8",
        errors="replace",
    )
    return result.returncode, (result.stdout or "") + (result.stderr or "")


def entries(report):
    """The themes that produced a converted candidate, in report order."""
    return [
        entry for entry in json.loads(Path(report).read_text(encoding="utf-8"))
        if "converted" in entry
    ]


def install(entry):
    """Install one theme's source and return its pixel hash."""
    code, text = run([
        sys.executable, "import_border.py", entry["source"],
        "--theme", entry["theme"], "--out", str(ROOT / "scratch" / "install"),
        "--install",
    ])
    if code != 0:
        return None, text
    match = re.search(r"^sha256 : ([0-9a-f]{64})$", text, re.MULTILINE)
    if not match:
        return None, text
    return match.group(1), text


def registry_block(pairs):
    """The `APPROVED_BORDERS` entries for the installed themes."""
    lines = []
    for theme, digest in pairs:
        lines.append(
            f'    "{theme}": ("{theme}_border.png", "{digest}"),'
        )
    return "\n".join(lines)


def write_registry(pairs):
    """Insert the entries into `APPROVED_BORDERS`, keeping it sorted."""
    text = REGISTRY.read_text(encoding="utf-8")
    start = text.index("APPROVED_BORDERS = {")
    end = text.index("\n}\n", start)
    body = text[start:end]
    existing = dict(re.findall(
        r'"([a-z_]+)": \("[a-z_]+_border\.png", "([0-9a-f]{64})"\)', body
    ))
    existing.update(dict(pairs))
    rebuilt = "APPROVED_BORDERS = {\n" + "\n".join(
        f'    "{theme}": ("{theme}_border.png", "{digest}"),'
        for theme, digest in sorted(existing.items())
    )
    REGISTRY.write_text(text[:start] + rebuilt + text[end:], encoding="utf-8")
    return len(existing)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path, help="a border_batch report.json")
    parser.add_argument("--write", action="store_true",
                        help="also rewrite APPROVED_BORDERS in place")
    args = parser.parse_args(argv)

    pairs = []
    for entry in entries(args.report):
        digest, text = install(entry)
        if digest is None:
            print(f"{entry['theme']}: FAILED")
            print(text[-800:])
            continue
        print(f"{entry['theme']}: {digest}")
        pairs.append((entry["theme"], digest))

    print(f"\n{len(pairs)} installed")
    print(registry_block(pairs))
    if args.write:
        total = write_registry(pairs)
        print(f"\nAPPROVED_BORDERS now holds {total} themes")
    return 0 if pairs else 1


if __name__ == "__main__":
    raise SystemExit(main())
