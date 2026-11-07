"""Reproduce the WiFi menu path and watch memory across repeated opens.

Opening the WiFi page starts a scan. The report was that the first open says
"radio unavailable" and subsequent presses leak memory, so this opens the page
several times and prints the memory after each.

Usage:  python repro_wifi.py COM7
"""
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM7"
REPEATS = int(sys.argv[2]) if len(sys.argv) > 2 else 4


def send(s, cmd, wait=8.0):
    s.reset_input_buffer()
    s.write((cmd + "\n").encode())
    s.flush()

    end = time.time() + wait
    buf = b""
    out = []

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
            out.append(txt)
            if txt.startswith("OK") or txt.startswith("ERR"):
                return out
    return out

def main():
    s = serial.Serial(PORT, 115200, timeout=0.2)
    s.dtr = False
    s.rts = False
    time.sleep(3.0)

    print("=== baseline ===")
    for l in send(s, "mem"):
        print("   ", l)

    for i in range(REPEATS):
        print("=== open wifi #%d ===" % (i + 1))
        for l in send(s, "open wifi"):
            print("   ", l)
        # Give the scan time to run and the menu to rebuild.
        time.sleep(4.0)
        for l in send(s, "mem"):
            print("   ", l)

    print("=== final ===")
    for l in send(s, "mem"):
        print("   ", l)

    s.close()


if __name__ == "__main__":
    main()
