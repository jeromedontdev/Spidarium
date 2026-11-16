"""Generate pixel art through OpenRouter's image API or PixelLab.

The two providers are independent: OpenRouter is a model marketplace reached at
openrouter.ai, PixelLab is a separate pixel-art service at pixellab.ai. Neither
routes through the other, and each has its own credential.

Dry-run by default: nothing is submitted and nothing is spent until --submit is
passed. Credentials come from the environment only, never from the command line,
so they cannot leak into shell history or a process listing.

    set OPENROUTER_API_KEY=...      (Windows)
    export OPENROUTER_API_KEY=...   (POSIX)

    # One model.
    python tools/imagegen.py generate --provider openrouter \
        --model google/gemini-3.1-flash-image \
        --prompt "mossy stone floor" --width 1024 --height 1024 --out scratch

    # Several models in one run: repeat --model or comma-separate the slugs.
    python tools/imagegen.py generate --provider openrouter \
        --model google/gemini-3.1-flash-image,openai/gpt-image-2.6-sunburst \
        --prompt "mossy stone floor" --out scratch --submit

    python tools/imagegen.py models --provider openrouter
    python tools/imagegen.py balance --provider pixellab

Generated files are candidates, not installed art. Review them, then integrate
through gen_sprites.py and validate with asset_build.py; this tool never writes
into assets/ and never overwrites an existing file.
"""

import argparse
import base64
import hashlib
import io
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
PROTECTED = ROOT / "assets"

PIXELLAB_BASE = "https://api.pixellab.ai/v2"
OPENROUTER_BASE = "https://openrouter.ai/api/v1"

PROVIDERS = ("pixellab", "openrouter")
KEY_ENV = {"pixellab": "PIXELLAB_API_KEY", "openrouter": "OPENROUTER_API_KEY"}
KEY_DOCS = {
    "pixellab": "https://pixellab.ai/account",
    "openrouter": "https://openrouter.ai/settings/keys",
}

# PixelLab's Pixen endpoint is synchronous and returns one base64 image.
# OpenRouter's Image API is synchronous too and returns data[].b64_json.
PIXELLAB_ENDPOINT = "/create-image-pixen"
OPENROUTER_ENDPOINT = "/images"

SUFFIX = {"png": "png", "jpeg": "jpg", "jpg": "jpg", "webp": "webp", "svg": "svg"}


def api_key(provider: str) -> str:
    name = KEY_ENV[provider]
    key = os.environ.get(name, "").strip()
    if not key:
        raise ValueError(
            f"{name} is not set; create a key at {KEY_DOCS[provider]} and export it "
            f"in the environment (never pass a key on the command line)"
        )
    return key


def redact(text: str, key: str) -> str:
    """Strip a credential from anything that might be printed or logged."""
    if not key:
        return text
    return text.replace(key, "<redacted>")


def slug(text: str, limit: int = 40) -> str:
    cleaned = re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")
    return (cleaned[:limit].rstrip("-") or "image")


def check_output_dir(path: Path) -> Path:
    resolved = path.resolve()
    if resolved == PROTECTED or PROTECTED in resolved.parents:
        raise ValueError(
            "Refusing to write into assets/: that is protected source art. "
            "Generate into a scratch directory and integrate deliberately."
        )
    return resolved


def pixellab_request(args) -> tuple[str, dict]:
    payload = {
        "description": args.prompt,
        "image_size": {"width": args.width, "height": args.height},
    }
    if args.no_background:
        payload["no_background"] = True
    if args.seed is not None:
        payload["seed"] = args.seed
    if args.detail:
        payload["detail"] = args.detail
    if args.view:
        payload["view"] = args.view
    if args.direction:
        payload["direction"] = args.direction
    if args.outline:
        payload["outline"] = args.outline
    return f"{PIXELLAB_BASE}{PIXELLAB_ENDPOINT}", payload


def openrouter_request(args, model: str) -> tuple[str, dict]:
    payload = {"model": model, "prompt": args.prompt}
    # Gemini image models reject `size`; they take resolution + aspect_ratio.
    if args.resolution:
        payload["resolution"] = args.resolution
        if args.aspect_ratio:
            payload["aspect_ratio"] = args.aspect_ratio
    elif args.width and args.height:
        payload["size"] = f"{args.width}x{args.height}"
    if args.n and args.n > 1:
        payload["n"] = args.n
    if args.seed is not None:
        payload["seed"] = args.seed
    if args.no_background:
        payload["background"] = "transparent"
    if args.output_format:
        payload["output_format"] = args.output_format
    return f"{OPENROUTER_BASE}{OPENROUTER_ENDPOINT}", payload


def requested_models(provider: str, args) -> list[str]:
    """Every model to run, in order, with duplicates removed.

    OpenRouter is a marketplace, so one prompt is often worth trying against
    several models. Accept both repeated flags and comma-separated slugs.
    """
    if provider != "openrouter":
        return [""]
    slugs = []
    for value in args.model or []:
        for part in value.split(","):
            part = part.strip()
            if part and part not in slugs:
                slugs.append(part)
    if not slugs:
        raise ValueError(
            "OpenRouter needs --model; run 'imagegen.py models --provider openrouter' "
            "to list image models. Model slugs and availability change, so there is "
            "no built-in default. Repeat --model or comma-separate slugs to try several."
        )
    return slugs


def build_requests(provider: str, args) -> list[tuple[str, str, dict]]:
    """Return [(model, url, payload)] for every requested generation."""
    if provider == "pixellab":
        url, payload = pixellab_request(args)
        return [("", url, payload)]
    return [(model, *openrouter_request(args, model))
            for model in requested_models(provider, args)]


def http_json(url: str, key: str, payload: dict | None = None, timeout: int = 300) -> dict:
    """One request, no automatic retry: a retry can spend twice."""
    data = None if payload is None else json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(url, data=data, method="POST" if data else "GET")
    request.add_header("Authorization", f"Bearer {key}")
    request.add_header("Accept", "application/json")
    if data:
        request.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        body = error.read().decode("utf-8", errors="replace")[:2000]
        raise RuntimeError(f"{url} -> HTTP {error.code}: {redact(body, key)}") from None
    except urllib.error.URLError as error:
        raise RuntimeError(f"{url} -> {error.reason}") from None


def extract_images(provider: str, result: dict) -> list[tuple[str, str]]:
    """Return [(base64, format)] for every image in a provider response."""
    if provider == "pixellab":
        image = result.get("image") or {}
        encoded = image.get("base64")
        if not encoded:
            raise RuntimeError("PixelLab response contained no image")
        return [(encoded, image.get("format") or "png")]
    entries = result.get("data") or []
    if not entries:
        raise RuntimeError("OpenRouter response contained no images")
    images = []
    for entry in entries:
        encoded = entry.get("b64_json")
        if not encoded:
            raise RuntimeError("OpenRouter image entry contained no b64_json")
        media = entry.get("media_type") or "image/png"
        images.append((encoded, media.rsplit("/", 1)[-1]))
    return images


def describe_image(raw: bytes) -> dict:
    """Decode the bytes so a corrupt or mislabelled response fails loudly."""
    with Image.open(io.BytesIO(raw)) as image:
        image.load()
        info = {"format": image.format, "mode": image.mode, "size": list(image.size)}
        if image.mode in ("RGBA", "LA", "P"):
            info["visible_colors"] = len(image.convert("RGBA").getcolors(maxcolors=1 << 20) or [])
    return info


def write_new(path: Path, data: bytes) -> None:
    with path.open("xb") as output:
        output.write(data)


def save_images(provider: str, args, result: dict, out_dir: Path,
                model: str = "") -> list[dict]:
    stamp = time.strftime("%Y%m%d-%H%M%S")
    tag = f"{provider}-{slug(model)}" if model else provider
    stem = f"{tag}-{stamp}-{slug(args.prompt)}"
    saved = []
    for index, (encoded, fmt) in enumerate(extract_images(provider, result)):
        try:
            raw = base64.b64decode(encoded, validate=True)
        except ValueError as error:
            raise RuntimeError(f"image {index} was not valid base64: {error}") from None
        suffix = SUFFIX.get(fmt.lower(), fmt.lower() or "png")
        path = out_dir / f"{stem}-{index}.{suffix}"
        write_new(path, raw)
        saved.append({
            "path": str(path),
            "model": model or None,
            "bytes": len(raw),
            "sha256": hashlib.sha256(raw).hexdigest(),
            "reported_format": fmt,
            "decoded": describe_image(raw),
        })
    return saved


def write_receipt(out_dir: Path, provider: str, args, url: str, payload: dict,
                  result: dict, saved: list[dict], model: str = "") -> Path:
    stamp = time.strftime("%Y%m%d-%H%M%S")
    tag = f"{provider}-{slug(model)}" if model else provider
    path = out_dir / f"{tag}-{stamp}-{slug(args.prompt)}.json"
    receipt = {
        "provider": provider,
        "endpoint": url,
        "model": model or payload.get("model"),
        "prompt": args.prompt,
        "request": payload,
        "usage": result.get("usage"),
        "created": result.get("created"),
        "images": saved,
        "installed": False,
        "note": (
            "Candidate art only. Integrate through gen_sprites.py and validate with "
            "asset_build.py; this tool never writes into assets/."
        ),
    }
    write_new(path, (json.dumps(receipt, indent=2) + "\n").encode("utf-8"))
    return path


def cmd_generate(args) -> int:
    provider = args.provider
    requests = build_requests(provider, args)
    out_dir = check_output_dir(args.out)
    key = api_key(provider) if args.submit else ""

    print(f"provider : {provider}")
    print(f"models   : {len(requests)}")
    for model, url, payload in requests:
        print(f"  {model or '(fixed endpoint)'} -> {url}")
        print(f"    {json.dumps(payload)}")
    if not args.submit:
        print()
        print(f"DRY RUN: zero requests, zero spend. {len(requests)} request(s) would be "
              f"sent. Re-run with --submit to generate.")
        print(f"Credential would be read from ${KEY_ENV[provider]}.")
        return 0

    out_dir.mkdir(parents=True, exist_ok=True)
    failures = []
    for model, url, payload in requests:
        label = model or provider
        print(f"\n== {label}")
        try:
            result = http_json(url, key, payload, timeout=args.timeout)
            saved = save_images(provider, args, result, out_dir, model)
            receipt = write_receipt(out_dir, provider, args, url, payload, result,
                                    saved, model)
        except (OSError, RuntimeError) as error:
            # One bad model must not discard the images already paid for.
            failures.append((label, str(error)))
            print(f"FAILED   : {error}")
            continue
        for entry in saved:
            decoded = entry["decoded"]
            print(f"saved    : {entry['path']} "
                  f"({decoded['size'][0]}x{decoded['size'][1]} {decoded['mode']} "
                  f"{decoded['format']}, {entry['bytes']} bytes)")
        print(f"receipt  : {receipt}")
        if result.get("usage"):
            print(f"usage    : {json.dumps(result['usage'])}")

    if failures:
        print(f"\n{len(failures)} of {len(requests)} model(s) failed:")
        for label, message in failures:
            print(f"  {label}: {message}")
        return 1
    print("\nReview at native scale before integrating; nothing was installed.")
    return 0


def cmd_models(args) -> int:
    if args.provider != "openrouter":
        raise ValueError("Only OpenRouter exposes a model list; PixelLab endpoints are fixed")
    # The catalogue is public, so listing works before a key is configured.
    key = os.environ.get("OPENROUTER_API_KEY", "").strip() or None
    result = http_json(f"{OPENROUTER_BASE}/images/models", key, timeout=args.timeout)
    for entry in result.get("data") or []:
        architecture = entry.get("architecture") or {}
        outputs = ",".join(architecture.get("output_modalities") or [])
        print(f"{entry.get('id')}\t{outputs}\t{entry.get('name') or ''}")
    return 0


def cmd_balance(args) -> int:
    if args.provider != "pixellab":
        raise ValueError("Only PixelLab exposes a balance endpoint; OpenRouter bills per call")
    key = api_key("pixellab")
    result = http_json(f"{PIXELLAB_BASE}/balance", key, timeout=args.timeout)
    credits = result.get("credits") or {}
    if credits.get("usd") is not None:
        print(f"credits      : ${credits['usd']:.2f}")
    subscription = result.get("subscription") or {}
    if subscription:
        print(f"plan         : {subscription.get('plan') or 'unknown'}")
        print(f"status       : {subscription.get('status') or 'unknown'}")
        if subscription.get("generations") is not None:
            print(f"generations  : {subscription['generations']} of "
                  f"{subscription.get('total', '?')} left")
    if not credits and not subscription:
        print(json.dumps(result, indent=2))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)

    generate = commands.add_parser("generate", help="Build a request; submit only with --submit")
    generate.add_argument("--provider", choices=PROVIDERS, required=True)
    generate.add_argument("--prompt", required=True)
    generate.add_argument("--out", type=Path, required=True,
                          help="Scratch directory for images and the receipt; never assets/")
    generate.add_argument("--model", action="append", metavar="SLUG",
                          help="OpenRouter model slug (required for openrouter). "
                               "Repeat the flag or comma-separate slugs to run several.")
    generate.add_argument("--width", type=int, default=240)
    generate.add_argument("--height", type=int, default=240)
    generate.add_argument("--resolution",
                          choices=("512", "768", "1K", "1.5K", "2K", "4K"),
                          help="OpenRouter resolution tier; overrides --width/--height "
                               "for models that reject an explicit size")
    generate.add_argument("--aspect-ratio", help="OpenRouter aspect ratio, e.g. 1:1")
    generate.add_argument("--seed", type=int)
    generate.add_argument("--n", type=int, default=1, help="OpenRouter image count")
    generate.add_argument("--no-background", action="store_true",
                          help="Transparent background where the provider supports it")
    generate.add_argument("--output-format", choices=("png", "jpeg", "webp"),
                          help="OpenRouter output format")
    generate.add_argument("--detail", choices=("low detail", "medium detail", "highly detailed"),
                          help="PixelLab detail level")
    generate.add_argument("--view", choices=("side", "low top-down", "high top-down"),
                          help="PixelLab camera view")
    generate.add_argument("--direction",
                          choices=("north", "north-east", "east", "south-east",
                                   "south", "south-west", "west", "north-west"),
                          help="PixelLab subject direction")
    generate.add_argument("--outline",
                          choices=("single color black outline", "single color outline",
                                   "selective outline", "lineless"),
                          help="PixelLab outline style")
    generate.add_argument("--timeout", type=int, default=300)
    generate.add_argument("--submit", action="store_true",
                          help="Actually call the API and spend. Without it this is a dry run.")
    generate.set_defaults(handler=cmd_generate)

    models = commands.add_parser("models", help="List OpenRouter image models")
    models.add_argument("--provider", choices=PROVIDERS, default="openrouter")
    models.add_argument("--timeout", type=int, default=60)
    models.set_defaults(handler=cmd_models)

    balance = commands.add_parser("balance", help="Read the PixelLab balance")
    balance.add_argument("--provider", choices=PROVIDERS, default="pixellab")
    balance.add_argument("--timeout", type=int, default=60)
    balance.set_defaults(handler=cmd_balance)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.handler(args)
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(1, f"ERROR: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
