"""Run a set of console commands and write the results to a file.

This exists because the interactive terminal in this environment can hang on
serial I/O. Running the checks as a batch and writing to a file means the result
can be read back afterwards regardless.

Usage:  python run_tests.py COM7
"""
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM7"
OUT = "test_results.txt"

COMMANDS = [
    ("help", 10),
    ("mem", 10),
    ("stat", 10),
    ("radio", 10),
    ("render 200", 30),
    ("scan", 40),
    ("scanloop 3", 90),
    ("mem", 10),
]


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
    time.sleep(3.0)

    passed = 0
    failed = 0

    for cmd, timeout in COMMANDS:
        emit("=" * 60)
        emit(">>> " + cmd)
        lines, verdict = run(s, cmd, timeout)
        for l in lines:
            emit(l)
        if verdict == "OK":
            passed += 1
        else:
            failed += 1
            emit("!!! %s" % (verdict or "TIMEOUT"))

    s.close()

    emit("=" * 60)
    emit("SUMMARY passed=%d failed=%d" % (passed, failed))

    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
