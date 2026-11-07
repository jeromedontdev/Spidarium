"""One-shot serial log capture for leg-rendering diagnostics (per-leg summary)."""
import re
import serial
import sys
import time

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM7"
DURATION = float(sys.argv[2]) if len(sys.argv) > 2 else 8.0

s = serial.Serial(PORT, 115200, timeout=1)
s.reset_input_buffer()
end = time.time() + DURATION
buf = b""
lines = []
while time.time() < end:
    data = s.read(4096)
    if data:
        buf += data
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            txt = raw.decode("utf-8", "replace").strip()
            if txt:
                lines.append(txt)
s.close()

pat = re.compile(
    r"LEGDIAG-v7 i=(\d+) cx=(-?\d+) cy=(-?\d+) .*?x0=(-?\d+) y0=(-?\d+) x1=(-?\d+) y1=(-?\d+) minx=(-?\d+) miny=(-?\d+)"
)
per_leg = {}
for l in lines:
    m = pat.search(l)
    if not m:
        continue
    i = int(m.group(1))
    d = per_leg.setdefault(i, {"n": 0, "first": l, "last": l})
    d["n"] += 1
    d["last"] = l

errors = [l for l in lines if "E (" in l or "abort" in l.lower() or "assert" in l.lower()]

print("=== PER-LEG SUMMARY (leg: count) ===")
for i in sorted(per_leg):
    print("leg %d: %d updates" % (i, per_leg[i]["n"]))
    print("   first:", per_leg[i]["first"].split("legs:", 1)[-1])
    print("   last: ", per_leg[i]["last"].split("legs:", 1)[-1])
print("=== legs seen: %s ===" % sorted(per_leg.keys()))
print("=== ERRORS (%d) ===" % len(errors))
for l in errors:
    print(l)
print("=== total lines: %d ===" % len(lines))
