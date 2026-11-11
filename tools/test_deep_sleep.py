"""Verify committed RTC timer sleep/wake on COM7, preserving the existing pet."""
import json
from pathlib import Path
import re
import sys
import time

import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run_tests import wait_ready
from test_web_device import command, export


def connect(port):
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.2)
    connection.dtr = connection.rts = False
    connection.port = port
    connection.open()
    return connection


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM7"
    connection = connect(port)
    try:
        if not wait_ready(connection):
            raise RuntimeError("Console not ready before sleep")
        command(connection, "open main")
        before = export(connection)
        started = time.monotonic()
        connection.write(b"nap 5\n")
        committed = False
        disconnected = False
        deadline = started + 30
        while time.monotonic() < deadline:
            try:
                line = connection.readline().decode("utf-8", errors="replace").strip()
            except serial.SerialException:
                disconnected = True
                connection.close()
                time.sleep(0.25)
                try:
                    connection = connect(port)
                except serial.SerialException:
                    continue
                continue
            if line:
                print(line)
                committed |= "deep sleep committed" in line
            if time.monotonic() - started > 8:
                break
        if not connection.is_open:
            connection = connect(port)
        if not wait_ready(connection):
            raise RuntimeError("Console did not return after RTC wake")
        telemetry = command(connection, "battery")
        print("\n".join(telemetry))
        if not any(re.search(r"\bwake_cause=4\b", line) for line in telemetry):
            raise RuntimeError("Wake was not ESP_SLEEP_WAKEUP_TIMER (4)")
        command(connection, "open main")
        after = export(connection, stored=True)
        schema = json.loads((Path(__file__).resolve().parents[1] / "site" / "save-schema.json").read_text())
        stable = {"name", "colour", "silk", "gifts_owned", "decor_owned", "decor",
                  "backgrounds_owned", "background_id", "power_mode", "screen_timeout",
                  "deep_sleep_after", "rtc_wake", "decor_owned_extra",
                  "decor_extra_count", "music_enabled", "lights_dimmed", "music_volume", "music_track"}
        for field in schema["fields"]:
            if field["name"] not in stable:
                continue
            offset = 12 + field["offset"]
            width = {"u8": 1, "u32": 4}.get(field["type"], field["length"])
            if before[offset:offset + width] != after[offset:offset + width]:
                raise RuntimeError("Sleep changed saved field: " + field["name"])
        if before[272:] != after[272:]:
            raise RuntimeError("Sleep changed extended decor records")
        command(connection, "open close")
        print(f"DEEP SLEEP: PASS commit_log={int(committed)} timer_wake=4 disconnected={int(disconnected)} "
              "duration=5s saved_habitat=preserved; physical-button wake/current not measured")
    finally:
        connection.close()


if __name__ == "__main__":
    main()
