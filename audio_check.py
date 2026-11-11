"""Verify PCM parity, real codec writes and prompt ambience interruption, without changing the pet.

Usage: python audio_check.py COM7
Requires sound enabled. Logs prove PCM writes, not subjective speaker quality.
"""
import json
import re
import sys
import time

import serial

from run_tests import wait_ready


def main() -> None:
    events: list[tuple[float, str]] = []
    with serial.Serial(sys.argv[1] if len(sys.argv) > 1 else "COM7",
                       115200, timeout=0.1) as connection:
        connection.dtr = False
        connection.rts = False
        if not wait_ready(connection):
            raise RuntimeError("console not ready within 45 seconds")

        def read() -> None:
            line = connection.readline().decode("utf-8", "replace").strip()
            if line:
                print(line, flush=True)
                events.append((time.monotonic(), line))
                if re.search(r"Guru Meditation|Backtrace|panic|codec error|"
                             r"drain.*timeout|E \(\d+", line):
                    raise RuntimeError("device fault during audio verification")

        def wait(pattern: str, start: int, timeout: float = 15) -> tuple[float, str]:
            end = time.monotonic() + timeout
            while time.monotonic() < end:
                for event in events[start:]:
                    if re.search(pattern, event[1]):
                        return event
                read()
            raise RuntimeError(f"timed out waiting for {pattern}")

        def command(text: str) -> int:
            print(f">>> {text}", flush=True)
            start = len(events)
            connection.write((text + "\n").encode())
            connection.flush()
            _, result = wait(r"^(OK|ERR)\b", start)
            if result.startswith("ERR") or "sound muted" in result:
                raise RuntimeError(f"{text}: {result}")
            return start

        def settle(seconds: float) -> None:
            end = time.monotonic() + seconds
            while time.monotonic() < end:
                read()

        command("open sound")
        start = command("save schema")
        schema = json.loads(next(text.removeprefix("SAVE SCHEMA ")
                                 for _, text in events[start:] if text.startswith("SAVE SCHEMA ")))
        offsets = {field["name"]: 12 + field["offset"] for field in schema["fields"]}
        start = command("save export")
        original = bytes.fromhex(next(text.removeprefix("SAVE HEX ")
                                      for _, text in events[start:] if text.startswith("SAVE HEX ")))
        original_music, original_muted, original_volume = (
            original[offsets[key]] for key in ("music_enabled", "muted", "volume"))
        try:
            command("set muted 0")
            command("set volume 70")
            command("set music_enabled 0")
            settle(0.3)
            start = command("set music_enabled 1")
            wait(r"music started: position=", start)
            settle(1.6)
            start = command("set music_enabled 0")
            wait(r"music stopped: disabled samples=[1-9]", start)
            start = command("set music_enabled 1")
            wait(r"music started: position=", start)
            settle(0.4)
            start = command("set muted 1")
            wait(r"music stopped: muted samples=[1-9]", start)
            start = command("set muted 0")
            wait(r"music started: position=", start)
            print("PASS music actual playback, independent toggle, master mute and resume", flush=True)
            start = command("audiotest")
            for name in ("air", "leaves", "music", "pet"):
                wait(fr"PCM {name} .*PASS", start)
            start = command("sound 8")
            wait(r"effect 8: written samples=2560", start)
            print("PASS pet bell full codec write", flush=True)
            for effect, name, samples, seconds in [(7, "air", 128000, 8),
                                                   (9, "leaves", 160000, 10)]:
                started = time.monotonic()
                start = command(f"sound {effect}")
                finished, _ = wait(fr"ambience {name}: complete samples={samples}/{samples}", start)
                elapsed = finished - started
                if not seconds - 1 <= elapsed <= seconds + 2:
                    raise RuntimeError(f"{name} playback duration {elapsed:.3f}s")
                print(f"PASS {name} full codec write duration={elapsed:.3f}s", flush=True)

            for action, expected in [("sound 1", r"effect 1: written samples=1440"),
                                     ("scan", r"^OK")]:
                command("sound 9")
                settle(0.7)
                started = time.monotonic()
                start = command(action)
                interrupted, line = wait(r"ambience leaves: interrupted samples=(\d+)/160000", start)
                match = re.search(r"samples=(\d+)/160000", line)
                if not match or not 320 < int(match[1]) < 160000 or interrupted - started >= 1:
                    raise RuntimeError("ambience did not interrupt within one second")
                wait(expected, start)
                print(f"PASS {action} interruption={interrupted - started:.3f}s", flush=True)
            start = command("radio")
            if not any("active=0 busy=0" in text for _, text in events[start:]):
                raise RuntimeError("radio not fully stopped")
            start = command("mem")
            if not any(text.startswith("MEM") and int(match[1]) >= 51200
                       for _, text in events[start:]
                       if (match := re.search(r"largest=(\d+)", text))):
                raise RuntimeError("largest internal block below 51200")
        finally:
            command(f"set music_enabled {original_music}")
            command(f"set muted {original_muted}")
            command(f"set volume {original_volume}")
            command("open close")
    print("VERDICT: PASS - music toggles/mute/resume, PCM parity, full writes, foreground/radio interruption")


if __name__ == "__main__":
    main()
