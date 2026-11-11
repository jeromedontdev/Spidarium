"""Verify the browser save contract on hardware without changing pet progress."""
import json
from pathlib import Path
import sys
import time

import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run_tests import run, wait_ready


def command(connection, text, expected="OK"):
    lines, verdict = run(connection, text, 30)
    if verdict != expected:
        raise RuntimeError(f"{text.split()[0]}: expected {expected}, got {verdict}\n" +
                           "\n".join(lines))
    return lines


def export(connection, stored=False):
    lines = command(connection, "save stored" if stored else "save export")
    return bytes.fromhex(next(line.removeprefix("SAVE HEX ") for line in lines
                              if line.startswith("SAVE HEX ")))


def main():
    root = Path(__file__).resolve().parents[1]
    with serial.Serial(sys.argv[1] if len(sys.argv) > 1 else "COM7", 115200, timeout=0.2) as connection:
        connection.dtr = connection.rts = False
        if not wait_ready(connection):
            raise RuntimeError("Console not ready")
        command(connection, "open main")  # Pause tank before comparing snapshots.
        schema_lines = command(connection, "save schema")
        schema = json.loads(next(line.removeprefix("SAVE SCHEMA ") for line in schema_lines
                                 if line.startswith("SAVE SCHEMA ")))
        if schema["payloadSizes"] != [120, 248] or schema["headerSize"] != 12:
            raise RuntimeError("Unexpected compiled save layout")
        (root / "site" / "save-schema.json").write_text(json.dumps(schema, indent=2) + "\n", encoding="utf-8")
        original = export(connection)
        if len(original) != 260:
            raise RuntimeError("Incorrect current raw save size")
        flags = [field for field in schema["fields"]
                 if field["name"] in {"auto_feed", "auto_hunt"}]
        if schema["version"] != 9 or len(schema["fields"]) != 41 or len(flags) != 2 or any(field.get("minVersion") != 3 for field in flags):
            raise RuntimeError("Fields missing from compiled version 9 schema")
        for field in flags:
            command(connection, f"set {field['name']} 1")
        command(connection, "reboot")
        time.sleep(2)
        if not wait_ready(connection):
            raise RuntimeError("Automation settings did not reboot")
        command(connection, "open main")
        enabled = export(connection)
        if any(enabled[12 + field["offset"]] != 1 for field in flags):
            raise RuntimeError("Checked automation settings did not persist across reboot")
        for field in flags:
            invalid = bytearray(enabled)
            invalid[12 + field["offset"]] = 2
            command(connection, "save restore " + invalid.hex(), "ERR")
        for offset, value in ((236, 0), (240, 16), (241, 3), (242, 5), (243, 5), (244, 3)):
            invalid = bytearray(enabled)
            invalid[12 + offset] = value
            command(connection, "save restore " + invalid.hex(), "ERR")
        preferences = bytearray(original)
        preferences[12 + 236:12 + 240] = (1 | (1 << 14)).to_bytes(4, "little")
        preferences[12 + 240:12 + 245] = bytes((14, 1, 1, 0, 2))
        command(connection, "save restore " + preferences.hex())
        command(connection, "reboot")
        time.sleep(2)
        if not wait_ready(connection):
            raise RuntimeError("Habitat preferences did not reboot")
        command(connection, "open main")
        persisted = export(connection)
        if persisted[12 + 236:12 + 245] != preferences[12 + 236:12 + 245]:
            raise RuntimeError("Background ownership or power preferences did not persist")
        command(connection, "save restore " + original.hex())
        command(connection, "reboot")
        time.sleep(2)
        if not wait_ready(connection):
            raise RuntimeError("Original automation settings did not reboot")
        command(connection, "open main")
        original = export(connection)
        bad = bytearray(original)
        bad[0] ^= 0xFF
        command(connection, "save restore " + bad.hex(), "ERR")
        bad = bytearray(original)
        bad[12] = 255
        command(connection, "save restore " + bad.hex(), "ERR")
        command(connection, "save restore zzzz", "ERR")
        command(connection, "save restore " + original[:-1].hex(), "ERR")
        rejected = export(connection, stored=True)
        if rejected != original:
            changes = [index - 12 for index, (a, b) in enumerate(zip(original, rejected))
                       if a != b]
            raise RuntimeError(f"Rejected input altered the saved pet at payload offsets {changes}")
        command(connection, "save restore " + original.hex())
        command(connection, "save", "ERR")  # Restored bytes must not be overwritten.
        if export(connection) != original:
            raise RuntimeError("Restore did not preserve exact bytes")
        memory = command(connection, "mem")
        stack_free = int(next(line.split("=")[1] for line in memory if line.startswith("STACK console_free=")))
        if stack_free < 2048:
            raise RuntimeError("Console stack headroom below 2048 bytes")
        print(f"CONSOLE stack_free={stack_free} after valid/invalid restores")
        command(connection, "reboot")
        time.sleep(2)
        if not wait_ready(connection):
            raise RuntimeError("Restored pet did not reboot")
        command(connection, "open main")
        restored = export(connection)
        for field in schema["fields"]:
            if field["name"] in {"age_ticks", "days_lived", "mood", "last_seen"}:
                continue
            sizes = {"u8": 1, "u16": 2, "u32": 4, "string": field["length"], "decor[12]": 72}
            length = sizes[field["type"]]
            offset = 12 + field["offset"]
            if original[offset:offset + length] != restored[offset:offset + length]:
                raise RuntimeError("Reboot changed restored field: " + field["name"])
        command(connection, "open close")
    print("WEB DEVICE: PASS - v9/41-field schema, background/power persistence, 260-byte export, invalid v9 fields, exact restore, overwrite guard, reboot")


if __name__ == "__main__":
    main()
