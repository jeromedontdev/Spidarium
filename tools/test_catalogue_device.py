"""Verify every doubled background/frame on hardware, restoring the original pet."""
import argparse
import json
from pathlib import Path
import re
import sys
import time

import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run_tests import wait_ready
from test_web_device import command, export


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", nargs="?", default="COM7")
    parser.add_argument("--evidence-dir", type=Path, required=True)
    args = parser.parse_args()
    args.evidence_dir.mkdir(parents=True, exist_ok=True)
    with serial.Serial(args.port, 115200, timeout=0.2) as connection:
        connection.dtr = connection.rts = False
        if not wait_ready(connection):
            raise RuntimeError("Console not ready")
        command(connection, "open main")
        original = export(connection)
        (args.evidence_dir / "catalogue-original-pet.bin").write_bytes(original)
        schema = json.loads(next(line.removeprefix("SAVE SCHEMA ") for line in
            command(connection, "save schema") if line.startswith("SAVE SCHEMA ")))
        offsets = {field["name"]: 12 + field["offset"] for field in schema["fields"]}

        def restore(raw):
            command(connection, "save restore " + raw.hex())
            command(connection, "reboot")
            time.sleep(2)
            if not wait_ready(connection):
                raise RuntimeError("Restored pet did not reboot")
            command(connection, "open main")

        try:
            funded = bytearray(original)
            funded[offsets["silk"]:offsets["silk"] + 4] = (100000).to_bytes(4, "little")
            restore(funded)
            before = "\n".join(command(connection, "mem"))
            for scene in range(48):
                command(connection, f"background {scene}")
                command(connection, f"border {scene}")
            state = export(connection)
            for name in ("backgrounds_owned", "borders_owned"):
                if int.from_bytes(state[offsets[name]:offsets[name] + 4], "little") != 0xffffffff:
                    raise RuntimeError(name + " lost a high ownership bit")
            for name in ("backgrounds_owned_extra", "borders_owned_extra"):
                if int.from_bytes(state[offsets[name]:offsets[name] + 2], "little") != 65535:
                    raise RuntimeError(name + " lost an ownership bit")
            if state[offsets["border_id"]] != 47 or state[offsets["background_id"]] != 47:
                raise RuntimeError("Last theme/frame was not selected")
            wallet = state[offsets["silk"]:offsets["silk"] + 4]
            command(connection, "background 0")
            command(connection, "border none")
            command(connection, "border 47")
            mixed = export(connection)
            if mixed[offsets["silk"]:offsets["silk"] + 4] != wallet:
                raise RuntimeError("Reapplying owned content charged Silk")
            restore(mixed)
            persisted = export(connection)
            if persisted[176:] != mixed[176:]:
                raise RuntimeError("Theme/frame preferences did not survive reboot")
            command(connection, "border 48", "ERR")
            print("\n".join(command(connection, "uitest")))
            command(connection, "open borders")
            print("\n".join(command(connection, "capturetest")))
            print("\n".join(command(connection, "decortest")))
            for treat in range(12):
                command(connection, f"treat {treat}")
            after = "\n".join(command(connection, "mem"))
            largest = int(re.search(r"\blargest=(\d+)", after)[1])
            if largest < 51200:
                raise RuntimeError("Internal largest-block floor violated")
            print(f"CATALOGUES PASS: 48 floors/frames rendered and purchased; high bits/reboot; "
                  f"mixed frame reapplication free; 68 decor rendered; 12 treats; largest={largest}")
            print(before)
            print(after)
        finally:
            restore(original)
            restored = export(connection)
            if restored[176:] != original[176:]:
                raise RuntimeError("Original habitat was not restored")
            command(connection, "open close")
            print("ORIGINAL PET RESTORED after catalogue verification")


if __name__ == "__main__":
    main()
