"""Send one command to the USB-JTAG console; exit 0 only for an OK reply."""
import sys

import serial

from run_tests import run, wait_ready


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM7"
    command = sys.argv[2] if len(sys.argv) > 2 else "help"
    timeout = float(sys.argv[3]) if len(sys.argv) > 3 else (
        150.0 if command.split(" ", 1)[0] in ("render", "screenshot") else 30.0
    )
    with serial.Serial(port, 115200, timeout=0.2) as connection:
        connection.dtr = False
        connection.rts = False
        if not wait_ready(connection):
            print("ERR console not ready within 45 seconds")
            return 1
        lines, verdict = run(connection, command, timeout)
    for line in lines:
        print(line)
    if verdict is None:
        print("ERR command timed out")
    return 0 if verdict == "OK" else 1


if __name__ == "__main__":
    sys.exit(main())
