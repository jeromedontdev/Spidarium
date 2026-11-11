"""Verify the browser save contract on hardware without changing pet progress."""
import json
from pathlib import Path
import sys
import time

import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run_tests import run, wait_ready


def command(connection, text, expected="OK"):
    if len(text) > 800:
        connection.reset_input_buffer()
        for offset in range(0, len(text), 128):
            connection.write(text[offset:offset + 128].encode())
            connection.flush()
            time.sleep(0.02)
        lines, verdict = run(connection, "", 30)
    else:
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
        if schema["payloadSizes"] != [120, 260] or schema["headerSize"] != 12:
            raise RuntimeError("Unexpected compiled save layout")
        (root / "site" / "save-schema.json").write_text(json.dumps(schema, indent=2) + "\n", encoding="utf-8")
        original = export(connection)
        if len(original) != 12 + int.from_bytes(original[8:12], "little"):
            raise RuntimeError("Incorrect current raw save size")
        flags = [field for field in schema["fields"]
                 if field["name"] in {"auto_feed", "auto_hunt"}]
        if schema["version"] != 11 or len(schema["fields"]) != 47 or len(flags) != 2 or any(field.get("minVersion") != 3 for field in flags):
            raise RuntimeError("Fields missing from compiled version 11 schema")
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
        for offset, value in ((236, 0), (240, 24), (241, 3), (242, 5), (243, 5), (244, 3), (248, 4), (252, 2), (253, 2), (254, 101), (255, 3)):
            invalid = bytearray(enabled)
            invalid[12 + offset] = value
            command(connection, "save restore " + invalid.hex(), "ERR")
        preferences = bytearray(original)
        preferences[12 + 236:12 + 240] = (1 | (1 << 23)).to_bytes(4, "little")
        preferences[12 + 240:12 + 245] = bytes((23, 1, 1, 0, 2))
        preferences[12 + 248:12 + 252] = (3).to_bytes(4, "little")
        preferences[12 + 252:12 + 254] = bytes((0, 1))
        preferences[12 + 254:12 + 256] = bytes((35, 2))
        preferences = preferences[:272]
        extra_count = 260
        for i in range(12):
            offset = 12 + 164 + i * 6
            preferences[offset:offset + 6] = bytes((0, 0)) + (70 + i * 20).to_bytes(2, "little") + (100).to_bytes(2, "little")
        preferences[12 + 256:12 + 260] = extra_count.to_bytes(4, "little")
        for i in range(extra_count):
            preferences.extend(bytes((32 + i % 2, i % 2)) +
                               (60 + (i % 50) * 7).to_bytes(2, "little") +
                               (80 + (i % 50) * 5).to_bytes(2, "little"))
        preferences[8:12] = (len(preferences) - 12).to_bytes(4, "little")
        command(connection, "save restore " + preferences.hex())
        command(connection, "reboot")
        time.sleep(2)
        if not wait_ready(connection):
            raise RuntimeError("Habitat preferences did not reboot")
        command(connection, "open main")
        persisted = export(connection)
        if persisted[12 + 236:12 + 245] != preferences[12 + 236:12 + 245]:
            raise RuntimeError("Background ownership or power preferences did not persist")
        if persisted[12 + 248:] != preferences[12 + 248:]:
            raise RuntimeError("Extended decor, music or lights did not persist")
        command(connection, "decor item 271")
        command(connection, "decor move 271 330 350")
        command(connection, "decor block 271")
        command(connection, "decor place 33 200 200")
        edited = export(connection)
        if int.from_bytes(edited[268:272], "little") != 261 or len(edited) != 272 + 261 * 6:
            raise RuntimeError("Runtime decoration growth did not persist every record")
        last_old = 272 + 259 * 6
        if edited[last_old + 2:last_old + 6] != (330).to_bytes(2, "little") + (350).to_bytes(2, "little"):
            raise RuntimeError("Moving a decoration beyond uint8 slots failed")
        command(connection, "reboot")
        time.sleep(2)
        if not wait_ready(connection):
            raise RuntimeError("Expanded runtime enclosure did not reboot")
        command(connection, "open main")
        grown = export(connection)
        if grown[12 + 164:] != edited[12 + 164:]:
            raise RuntimeError("Runtime-grown decoration enclosure did not survive reboot")
        command(connection, "decortest")
        print("EXTENDED HABITAT: 273 placements survive reboot; slot 271 opens/moves/blocks; runtime grows and saves; kinds 32/33, scene 23, music/lights retained")
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
    print("WEB DEVICE: PASS - v11/47-field schema, variable decor persistence, music/lights/volume/track, invalid fields, exact restore, overwrite guard, reboot")


if __name__ == "__main__":
    main()
