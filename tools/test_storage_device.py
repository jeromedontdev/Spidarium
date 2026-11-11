"""Verify real SD backup/restore and screenshots, restoring the original pet."""
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
        (args.evidence_dir / "storage-original-pet.bin").write_bytes(original)

        def restore(raw):
            command(connection, "save restore " + raw.hex())
            command(connection, "reboot")
            time.sleep(2)
            if not wait_ready(connection):
                raise RuntimeError("Restored pet did not reboot")
            command(connection, "open main")

        try:
            status = "\n".join(command(connection, "sd status"))
            if "present=1" not in status:
                raise RuntimeError("Real SD media required")
            if "backup=1" in status:
                command(connection, "sd restore")
                (args.evidence_dir / "storage-previous-sd-backup.bin").write_bytes(export(connection))
                restore(original)
            schema = json.loads(next(line.removeprefix("SAVE SCHEMA ") for line in
                command(connection, "save schema") if line.startswith("SAVE SCHEMA ")))
            offsets = {field["name"]: 12 + field["offset"] for field in schema["fields"]}
            funded = bytearray(original)
            funded[offsets["silk"]:offsets["silk"] + 4] = (100000).to_bytes(4, "little")
            funded[offsets["catalogue_owned_high"]] = 255
            funded[offsets["backgrounds_owned_extra"]:offsets["backgrounds_owned_extra"] + 2] = b"\xff\xff"
            restore(funded)
            command(connection, "background 47")
            command(connection, "border 47")
            command(connection, "set music_volume 35")
            command(connection, "set music_track 2")
            expected = export(connection)
            print("\n".join(command(connection, "sd backup")))
            command(connection, "set music_volume 80")
            command(connection, "set music_track 1")
            command(connection, "background 0")
            command(connection, "border none")
            print("\n".join(command(connection, "sd restore")))
            restored = export(connection)
            if restored[176:] != expected[176:]:
                raise RuntimeError("SD restore changed habitat records or music preferences")
            print("SD ROUNDTRIP PASS: full habitat tail, high ownership, floor/frame=47, music volume=35, track=2")
            command(connection, "open settings")
            time.sleep(0.2)
            before = "\n".join(command(connection, "mem"))
            filenames = []
            for _ in range(3):
                lines = command(connection, "screenshot sd")
                result = "\n".join(lines)
                print(result)
                match = re.search(r"Saved (spidarium-\d{5}\.bmp)", result)
                if not match:
                    raise RuntimeError("Screenshot did not confirm a verified filename")
                filenames.append(match[1])
                time.sleep(0.2)
            after = "\n".join(command(connection, "mem"))
            if len(set(filenames)) != 3:
                raise RuntimeError("Screenshots reused a filename")
            psram = lambda text: int(re.search(r"\bpsram=(\d+)", text)[1])
            if abs(psram(after) - psram(before)) > 4096:
                raise RuntimeError("Repeated screenshots leaked more than 4096 PSRAM bytes")
            if int(re.search(r"\blargest=(\d+)", after)[1]) < 51200:
                raise RuntimeError("Internal largest-block floor violated")
            print(f"SCREENSHOTS PASS: three verified BMPs, PSRAM drift={psram(after)-psram(before)}")
            print("\n".join(command(connection, "uitest")))
        finally:
            restore(original)
            restored = export(connection)
            if restored[176:] != original[176:]:
                raise RuntimeError("Original habitat/preferences were not restored")
            print("\n".join(command(connection, "sd backup")))
            command(connection, "open close")
            print("ORIGINAL PET RESTORED; card backup now contains original pet")


if __name__ == "__main__":
    main()
