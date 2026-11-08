"""Save an actual LVGL screen from the USB-JTAG screenshot command."""
import argparse
from pathlib import Path
import struct
import sys
import time

from PIL import Image
import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run_tests import run, wait_ready


def capture(connection, destination: Path, spider: bool = False, playfield: bool = False) -> None:
    if spider and playfield:
        raise ValueError("Choose spider or playfield, not both")
    connection.reset_input_buffer()
    connection.write(b"screenshot spider\n" if spider else
                     b"screenshot playfield\n" if playfield else b"screenshot\n")
    connection.flush()
    pending = b""
    width = height = 0
    pixel_format = ""
    rows = {}
    recent = []
    deadline = time.monotonic() + 150
    while time.monotonic() < deadline:
        pending += connection.read(4096)
        while b"\n" in pending:
            line, pending = pending.split(b"\n", 1)
            text = line.decode("ascii", "replace").strip()
            if text.startswith("ERR"):
                raise RuntimeError("\n".join(recent[-12:] + [text]))
            if not text.startswith("SCREEN"):
                recent.append(text)
                recent = recent[-12:]
            if text.startswith("SCREEN BEGIN "):
                _, _, w, h, pixel_format = text.split()
                width, height = int(w), int(h)
                if not 0 < width <= 1024 or not 0 < height <= 1024 or pixel_format not in ("rgb565le", "bgra8888"):
                    raise ValueError("Unsupported capture dimensions/format")
            elif text.startswith("SCREEN ROW "):
                fields = text.split()
                if len(fields) != 4:
                    raise ValueError("Screenshot row interleaved: " + repr(text[:70]) + " ... " + repr(text[-180:]))
                _, _, y, hex_data = fields
                index = int(y)
                data = bytes.fromhex(hex_data)
                stride = width * (4 if pixel_format == "bgra8888" else 2)
                if not width or not 0 <= index < height or index in rows or len(data) != stride:
                    raise ValueError(f"Corrupt screenshot row {index}: bytes={len(data)}, expected={stride}, duplicate={index in rows}")
                rows[index] = data
            elif text.startswith("SCREEN PART "):
                fields = text.split()
                if len(fields) != 5:
                    raise ValueError("Screenshot part interleaved with console output")
                index, offset = int(fields[2]), int(fields[3])
                data = bytes.fromhex(fields[4])
                stride = width * (4 if pixel_format == "bgra8888" else 2)
                row = rows.setdefault(index, bytearray())
                if not width or not 0 <= index < height or offset != len(row) or len(row) + len(data) > stride:
                    raise ValueError("Corrupt screenshot part offset/length")
                row.extend(data)
            elif text == "OK screenshot":
                stride = width * (4 if pixel_format == "bgra8888" else 2)
                if not height or len(rows) != height or any(len(row) != stride for row in rows.values()):
                    raise ValueError("Incomplete screenshot")
                raw = b"".join(rows[y] for y in range(height))
                if pixel_format == "bgra8888":
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    Image.frombytes("RGBA", (width, height), raw, "raw", "BGRA").save(destination)
                    print(f"CAPTURED {width}x{height} native spider: {destination.name}")
                    return
                pixels = bytearray()
                for (value,) in struct.iter_unpack("<H", raw):
                    red, green, blue = (value >> 11) & 31, (value >> 5) & 63, value & 31
                    pixels.extend(((red << 3) | (red >> 2), (green << 2) | (green >> 4),
                                   (blue << 3) | (blue >> 2)))
                destination.parent.mkdir(parents=True, exist_ok=True)
                Image.frombytes("RGB", (width, height), bytes(pixels)).save(destination)
                print(f"CAPTURED {width}x{height}: {destination.name}")
                return
    raise TimeoutError("Screenshot did not complete within 150 seconds")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port")
    parser.add_argument("output", type=Path)
    parser.add_argument("--menu")
    targets = parser.add_mutually_exclusive_group()
    targets.add_argument("--spider", action="store_true")
    targets.add_argument("--playfield", action="store_true",
                         help="Exclude all menus and HUD; leave the device UI unchanged")
    args = parser.parse_args()
    with serial.Serial(args.port, 115200, timeout=0.2) as connection:
        connection.dtr = connection.rts = False
        if not wait_ready(connection):
            raise RuntimeError("Device did not become ready")
        time.sleep(3)  # The console can announce readiness before app startup finishes.
        if args.menu:
            _, verdict = run(connection, "open " + args.menu, 10)
            if verdict != "OK":
                raise RuntimeError("Menu did not open")
        capture(connection, args.output, args.spider, args.playfield)


if __name__ == "__main__":
    main()
