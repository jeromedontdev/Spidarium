import serial, time, sys

port = sys.argv[1] if len(sys.argv) > 1 else "COM7"
s = serial.Serial(port=port, baudrate=115200, timeout=1)
# Opening the port usually toggles DTR/RTS and resets the ESP32-S3,
# re-triggering boot + the periodic 1 Hz "legs" diagnostic.
time.sleep(3.5)
buf = b""
t_end = time.time() + 6.0
while time.time() < t_end:
    d = s.read(4096)
    if d:
        buf += d
s.close()
sys.stdout.write(buf.decode("utf-8", "replace"))
