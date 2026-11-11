"""Verify real codec writes and prompt ambience interruption, without changing the pet.

Usage: python audio_check.py COM7
Requires sound enabled. Logs prove PCM writes, not subjective speaker quality.
"""
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
        try:
            command("audiotest")
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
            command("open close")
    print("VERDICT: PASS - PCM parity, full writes, foreground/radio interruption")


if __name__ == "__main__":
    main()
