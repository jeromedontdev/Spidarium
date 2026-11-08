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


def export(connection):
    lines = command(connection, "save export")
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
        if schema["payloadSizes"] != [120, 132] or schema["headerSize"] != 12:
            raise RuntimeError("Unexpected compiled save layout")
        (root / "site" / "save-schema.json").write_text(json.dumps(schema, indent=2) + "\n", encoding="utf-8")
        original = export(connection)
        if len(original) != 144:
            raise RuntimeError("Incorrect current raw save size")
        bad = bytearray(original)
        bad[0] ^= 0xFF
        command(connection, "save restore " + bad.hex(), "ERR")
        bad = bytearray(original)
        bad[12] = 255
        command(connection, "save restore " + bad.hex(), "ERR")
        command(connection, "save restore zzzz", "ERR")
        command(connection, "save restore " + original[:-1].hex(), "ERR")
        if export(connection) != original:
            raise RuntimeError("Rejected input altered the saved pet")
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
            if field["name"] in {"age_ticks", "days_lived"}:
                continue
            sizes = {"u8": 1, "u16": 2, "u32": 4, "string": field["length"]}
            length = sizes[field["type"]]
            offset = 12 + field["offset"]
            if original[offset:offset + length] != restored[offset:offset + length]:
                raise RuntimeError("Reboot changed restored field: " + field["name"])
        command(connection, "open close")
    print("WEB DEVICE: PASS - schema, 144-byte export, four invalid restores, exact restore, overwrite guard, reboot")


if __name__ == "__main__":
    main()
