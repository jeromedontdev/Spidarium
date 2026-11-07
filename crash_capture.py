"""Capture the serial log to a file for a long window, so a crash can be read
back afterwards instead of being guessed at.

Usage:  python crash_capture.py COM7 90 crash.log

Writes every line to the output file as it arrives (flushed), so the log is
complete even if the capture is interrupted. Prints a running summary of
anything that looks like a fault.
"""
import re
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM7"
DURATION = float(sys.argv[2]) if len(sys.argv) > 2 else 90.0
OUT = sys.argv[3] if len(sys.argv) > 3 else "crash.log"

FAULT = re.compile(
    r"Guru Meditation|abort\(\)|assert failed|Backtrace|"
    r"task_wdt|Task watchdog|Stack canary|CORRUPT HEAP|"
    r"StoreProhibited|LoadProhibited|InstrFetchProhibited|"
    r"ESP_ERR_NO_MEM|Draw bitmap failed|rst:0x|boot:0x|"
    r"E \(|W \(.*\) .*(fail|error|timeout)",
    re.IGNORECASE,
)

s = serial.Serial(PORT, 115200, timeout=1)
s.reset_input_buffer()

start = time.time()
end = start + DURATION
buf = b""
n_lines = 0
faults = []

with open(OUT, "w", encoding="utf-8") as f:
    f.write("# capture %s for %.0fs\n" % (PORT, DURATION))
    f.flush()
    while time.time() < end:
        data = s.read(4096)
        if not data:
            continue
        buf += data
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            txt = raw.decode("utf-8", "replace").rstrip()
            if not txt:
                continue
            n_lines += 1
            f.write(txt + "\n")
            f.flush()
            if FAULT.search(txt):
                faults.append(txt)
                print("FAULT: " + txt[:160], flush=True)

s.close()

print("=== lines: %d ===" % n_lines)
print("=== faults: %d ===" % len(faults))
for l in faults[:40]:
    print("  " + l[:200])
print("=== written to %s ===" % OUT)
