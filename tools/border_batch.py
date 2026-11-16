"""Generate and convert enclosure borders for a set of themes, end to end.

This is the driver that turns a theme name into a reviewable 240x240 frame: it
asks imagegen.py for the frame, then runs import_border.py over the result. It
never writes into assets/; adoption is a separate, deliberate step through
import_border.py --install plus a registry entry.

Usage:
    python tools/border_batch.py --themes snow gothic --out scratch/borders
    python tools/border_batch.py --remaining --out scratch/borders --submit

Without --submit the generation step is a dry run, so the whole plan can be
inspected for free.
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path

import approved_sources
import asset_build
import border_prompts as bp

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
MODEL = "google/gemini-nano-banana-2.1"


def remaining():
    """Themes whose border is not yet approved."""
    return [
        theme for theme in asset_build.BACKGROUNDS
        if not approved_sources.is_border_approved(theme)
    ]


def run(command):
    """Run a tool, streaming its output, and return (code, stdout+stderr)."""
    result = subprocess.run(
        command, cwd=TOOLS, capture_output=True, text=True, encoding="utf-8",
        errors="replace",
    )
    return result.returncode, (result.stdout or "") + (result.stderr or "")


def snapshot(directory):
    """Every file currently in a directory, so new output can be identified."""
    return {path for path in directory.glob("*") if path.is_file()}


def new_images(directory, before):
    """Images written since `before` was taken, oldest first.

    imagegen names its output after the prompt, not the theme, so the only
    reliable way to attribute a file to a theme is to diff the directory.
    """
    fresh = [
        path for path in snapshot(directory) - before
        if path.suffix.lower() in {".png", ".jpg", ".jpeg", ".webp"}
    ]
    return sorted(fresh, key=lambda path: path.stat().st_mtime)


def generate(theme, out, submit):
    prompt = bp.prompt(theme)
    prompt_file = out / f"prompt_border_{theme}.txt"
    prompt_file.write_text(prompt + "\n", encoding="utf-8")
    command = [
        sys.executable, "imagegen.py", "generate",
        "--provider", "openrouter",
        "--model", MODEL,
        "--prompt", prompt,
        "--resolution", "1K",
        "--aspect-ratio", "1:1",
        "--out", str(out / "raw"),
    ]
    if submit:
        command.append("--submit")
    return run(command)


def convert(theme, source, out):
    return run([
        sys.executable, "import_border.py", str(source),
        "--theme", theme,
        "--out", str(out / "converted"),
    ])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--themes", nargs="+", help="themes to process")
    parser.add_argument("--remaining", action="store_true",
                        help="every theme whose border is not yet approved")
    parser.add_argument("--out", type=Path, required=True, help="scratch directory")
    parser.add_argument("--submit", action="store_true",
                        help="actually call the API and spend")
    parser.add_argument("--skip-generate", action="store_true",
                        help="convert already-generated images only")
    args = parser.parse_args(argv)

    themes = list(args.themes or [])
    if args.remaining:
        themes += [theme for theme in remaining() if theme not in themes]
    if not themes:
        parser.error("choose --themes or --remaining")

    out = args.out.resolve()
    (out / "raw").mkdir(parents=True, exist_ok=True)
    (out / "converted").mkdir(parents=True, exist_ok=True)

    report = []
    for theme in themes:
        print(f"\n== {theme}")
        entry = {"theme": theme}
        before = snapshot(out / "raw")
        if not args.skip_generate:
            code, text = generate(theme, out, args.submit)
            if code != 0:
                print(f"generate FAILED ({code})")
                print(text[-1200:])
                entry["error"] = f"generate exit {code}"
                report.append(entry)
                continue
            if not args.submit:
                print("generate dry run ok")
                entry["dry_run"] = True
                report.append(entry)
                continue
        fresh = new_images(out / "raw", before)
        if not fresh:
            print("no generated image found")
            entry["error"] = "no image"
            report.append(entry)
            continue
        source = fresh[0]
        code, text = convert(theme, source, out)
        print(text.strip())
        if code != 0:
            entry["error"] = f"convert exit {code}"
            report.append(entry)
            continue
        entry["source"] = str(source)
        entry["converted"] = str(out / "converted" / f"border_{theme}_0.png")
        report.append(entry)

    (out / "report.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8"
    )
    done = sum(1 for entry in report if "converted" in entry)
    print(f"\n{done} of {len(themes)} converted; report at {out / 'report.json'}")
    return 0 if done == len(themes) or not args.submit else 1


if __name__ == "__main__":
    raise SystemExit(main())
