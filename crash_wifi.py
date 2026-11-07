"""Open the WiFi menu and capture the crash output to a file.

Usage:  python crash_wifi.py COM7
"""
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM7"
OUT = "wifi_crash.txt"

s = serial.Serial(PORT, 115200, timeout=0.2)
s.dtr = False
s.rts = False
time.sleep(3.0)

s.reset_input_buffer()
s.write(b"open wifi\n")
s.flush()

time.sleep(8.0)
data = s.read(60000)
s.close()

text = data.decode("utf-8", "replace")

with open(OUT, "w", encoding="utf-8") as f:
    f.write(text)

keep = []
for line in text.split("\n"):
    if any(k in line for k in ("Guru", "Backtrace", "spidarium", "assert",
                               "0x4", "panic", "abort")):
        keep.append(line)

print("\n".join(keep) if keep else "(no crash markers found)")
print("=== full log: %s (%d bytes) ===" % (OUT, len(text)))
