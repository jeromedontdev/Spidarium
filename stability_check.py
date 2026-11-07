"""Reset ESP32-S3 (COM7) into RUN mode and watch the console for a while.

Covers the first ~15 s of runtime: first blink cycle (t~1.5 s), wander
direction redraws (10 Hz), first periodic NVS save (t~5 s), bar/label
invalidation, leg re-anchoring. Reports any abort/assert/fault/brownout.
"""
import re
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM7"
DURATION = float(sys.argv[2]) if len(sys.argv) > 2 else 15.0

s = serial.Serial(PORT, 115200, timeout=1)

# esptool-style reset: hold in download mode, then release into RUN.
s.dtr = False
s.rts = True
time.sleep(0.3)
s.rts = False
s.dtr = False

time.sleep(0.2)
s.reset_input_buffer()

end = time.time() + DURATION
lines = []
while time.time() < end:
    data = s.read(4096)
    if data:
        for raw in data.split(b"\n"):
            txt = raw.decode("utf-8", "replace").strip()
            if txt:
                lines.append(txt)
s.close()

bad = re.compile(
    r"Guru Meditation|abort|assert|Backtrace|Load access|Store access|"
    r"Cache error|IntLoadNoPermit|brownout|Brownout|panic|"
    r"exception|E \(\d",
    re.IGNORECASE,
)
errors = [l for l in lines if bad.search(l)]

print("=== first 12 lines ===")
for l in lines[:12]:
    print(l)
print("=== last 12 lines ===")
for l in lines[-12:]:
    print(l)
print("=== FAULTS/ERRORS: %d ===" % len(errors))
for l in errors:
    print(l)
print("=== total lines: %d ===" % len(lines))
if not lines:
    print("VERDICT: FAIL - no serial output captured (port silent or wrong port)")
elif errors:
    print("VERDICT: FAIL - see faults above")
else:
    print("VERDICT: PASS - clean boot window")
