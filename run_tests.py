"""Run a set of console commands and write the results to a file.

This exists because the interactive terminal in this environment can hang on
serial I/O. Running the checks as a batch and writing to a file means the result
can be read back afterwards regardless.

Usage:  python run_tests.py COM7
        python run_tests.py COM7 results.txt --gameplay
The gameplay mode skips care/save/state commands that would change the pet.
"""
import sys
import time
import re

import serial

ARGS = [arg for arg in sys.argv[1:] if arg != "--gameplay"]
PORT = ARGS[0] if ARGS else "COM7"
OUT = ARGS[1] if len(ARGS) > 1 else "test_results.txt"

COMMANDS = [
    ("help", 20, "OK"),
    ("mem", 10, "OK"),
    ("stat", 10, "OK"),
    ("radio", 10, "OK"),
    ("audiotest", 60, "OK"),
    ("wifi saved", 10, "OK"),
    ("render 500", 40, "OK"),
    ("play", 10, "OK"),
    ("feed", 10, "OK"),
    ("clean", 10, "OK"),
    ("sleep", 10, "OK"),
    ("save", 10, "OK"),
    ("scan", 40, "OK"),
    ("scanloop 5", 120, "OK"),
    ("radio", 10, "OK"),
    ("gifttest", 10, "OK"),
    ("uitest", 20, "OK"),
    ("hunttest", 10, "OK"),
    ("gaittest", 10, "OK"),
    ("preytest", 10, "OK"),
    ("background", 10, "OK"),
    ("background 99", 10, "ERR"),
    ("refresh", 10, "OK"),
    ("spawn 0", 10, "OK"),
    ("state 0", 10, "OK"),
    ("set sick 0", 10, "OK"),
    ("open main", 10, "OK"),
    ("open wifi", 10, "OK"),
    ("open ach", 10, "OK"),
    ("open settings", 10, "OK"),
    ("open theme", 10, "OK"),
    ("open sound", 10, "OK"),
    ("open saved", 10, "OK"),
    ("open password", 10, "OK"),
    ("open ota", 10, "OK"),
    ("open about", 10, "OK"),
    ("open species", 10, "OK"),
    ("open gift", 10, "OK"),
    ("open reset", 10, "OK"),
    ("open close", 10, "OK"),
    ("uitest", 20, "OK"),
    ("wifi add invalid|short", 10, "ERR"),
    ("wifi add Spidarium-NoAP-Verification|test-only-passphrase", 40, "ERR"),
    ("radio", 10, "OK"),
    ("wifi sync 99", 10, "ERR"),
    ("wifi ota http://invalid.example/firmware.bin", 10, "ERR"),
    ("rtcset -1", 10, "ERR"),
    ("silk add 0", 10, "ERR"),
    ("silk add -1", 10, "ERR"),
    ("silk add 1x", 10, "ERR"),
    ("silk add 2147483648", 10, "ERR"),
    ("mem", 10, "OK"),
]
if "--gameplay" in sys.argv:
    mutating = {"play", "feed", "clean", "sleep", "save", "spawn 0", "state 0", "set sick 0"}
    COMMANDS = [case for case in COMMANDS if case[0] not in mutating]

def wait_ready(connection, timeout=45.0):
    """Wait for boot completion, or a help response if opening didn't reset."""
    deadline = time.monotonic() + timeout
    probe_at = time.monotonic() + 3.0
    pending = b""
    while time.monotonic() < deadline:
        if time.monotonic() >= probe_at:
            connection.write(b"\nhelp\n")
            connection.flush()
            probe_at = float("inf")
        pending += connection.read(4096)
        while b"\n" in pending:
            line, pending = pending.split(b"\n", 1)
            if line.startswith((b"OK console ready", b"OK commands:")):
                return True
    return False


def run(s, cmd, timeout):
    s.reset_input_buffer()
    s.write((cmd + "\n").encode())
    s.flush()

    deadline = time.time() + timeout
    buf = b""
    lines = []
    verdict = None

    while time.time() < deadline:
        data = s.read(4096)
        if not data:
            continue
        buf += data
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            txt = raw.decode("utf-8", "replace").rstrip()
            if not txt:
                continue
            lines.append(txt)
            if txt.startswith("OK"):
                verdict = "OK"
            elif txt.startswith("ERR"):
                verdict = "ERR"
        if verdict:
            break

    return lines, verdict


def main():
    out = []

    def emit(s=""):
        out.append(s)
        print(s, flush=True)

    s = serial.Serial(PORT, 115200, timeout=0.2)
    s.dtr = False
    s.rts = False
    if not wait_ready(s):
        s.close()
        emit("ERR console not ready within 45 seconds")
        return 1

    passed = 0
    failed = 0

    for cmd, timeout, expected in COMMANDS:
        emit("=" * 60)
        emit(">>> " + cmd)
        lines, verdict = run(s, cmd, timeout)
        for l in lines:
            emit(l)
        valid = verdict == expected
        if expected == "OK":
            valid = valid and not any(re.search(r"Guru Meditation|Backtrace|panic|E \(\d+", line)
                                          for line in lines)
        if cmd == "mem":
            readings = [re.search(r"largest=(\d+)", line) for line in lines if line.startswith("MEM")]
            valid = valid and any(match and int(match.group(1)) >= 51200 for match in readings)
        if cmd == "radio":
            valid = valid and any("active=0 busy=0" in line for line in lines)
        if cmd == "gifttest":
            valid = valid and any("tested=36 removed_neutral=1" in line for line in lines)
        if valid:
            passed += 1
        else:
            failed += 1
            emit("!!! expected=%s actual=%s or output assertion failed" %
                 (expected, verdict or "TIMEOUT"))

    s.close()

    emit("=" * 60)
    emit("SUMMARY passed=%d failed=%d" % (passed, failed))

    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
