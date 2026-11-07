"""Send a command to the Spidarium serial test console and print the reply.

Usage:  python cmd.py COM7 "scan"
        python cmd.py COM7 "scanloop 5"
        python cmd.py COM7 "render 500"

The console prints one line per result, ending with a line starting with OK or
ERR. This script sends the command, then reads until it sees that terminator or
the timeout expires, and prints everything it got.

Exit code is 0 if the reply ended with OK, 1 if it ended with ERR or timed out.
That makes it usable from a test script.
"""
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM7"
CMD = sys.argv[2] if len(sys.argv) > 2 else "help"
TIMEOUT = float(sys.argv[3]) if len(sys.argv) > 3 else 30.0

s = serial.Serial(PORT, 115200, timeout=0.2)

# Release the reset lines.
#
# pyserial asserts DTR and RTS when it opens a port. On this board RTS is wired
# to EN (reset) and DTR to GPIO0 (boot mode), so leaving them asserted holds the
# ESP32-S3 in reset -- the port opens, the write succeeds, and nothing ever
# answers. Both must be deasserted for the device to run.
s.dtr = False
s.rts = False

# Opening the port also RESETS the device, which then takes about 2.6 s to boot.
# Anything written during that window is lost, so wait before sending.
time.sleep(3.0)
s.reset_input_buffer()

s.write((CMD + "\n").encode())
s.flush()

deadline = time.time() + TIMEOUT
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

s.close()

for l in lines:
    print(l)

if verdict is None:
    print("=== TIMEOUT after %.0fs ===" % TIMEOUT)
    sys.exit(1)

sys.exit(0 if verdict == "OK" else 1)
