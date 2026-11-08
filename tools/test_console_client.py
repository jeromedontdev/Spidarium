"""Host tests for serial reply parsing and boot readiness without hardware."""
import sys
import runpy
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run_tests import run, wait_ready


class Connection:
    def __init__(self, chunks):
        self.chunks = iter(chunks)
        self.writes = []

    def reset_input_buffer(self):
        pass

    def write(self, value):
        self.writes.append(value)

    def flush(self):
        pass

    def read(self, size):
        return next(self.chunks, b"")


class ConsoleTests(unittest.TestCase):
    def test_partial_success_reply(self):
        connection = Connection([b"detail\nO", b"K scan found 2\n"])
        lines, verdict = run(connection, "scan", 1)
        self.assertEqual(verdict, "OK")
        self.assertEqual(lines, ["detail", "OK scan found 2"])
        self.assertEqual(connection.writes, [b"scan\n"])

    def test_error_is_not_success(self):
        connection = Connection([b"ERR scan failed\n"])
        self.assertEqual(run(connection, "scan", 1)[1], "ERR")

    def test_timeout_is_not_success(self):
        self.assertIsNone(run(Connection([]), "scan", 0)[1])

    def test_long_boot_readiness(self):
        connection = Connection([b"boot\n"] * 10 + [b"OK console ready (type help)\n"])
        with patch("run_tests.time.monotonic", side_effect=range(100)):
            self.assertTrue(wait_ready(connection, 45))
        self.assertEqual(connection.writes, [b"\nhelp\n"])

    def test_live_device_help_readiness(self):
        connection = Connection([b"OK commands:\n"])
        self.assertTrue(wait_ready(connection))

    def test_silent_device_rejected(self):
        self.assertFalse(wait_ready(Connection([]), 0))

    def test_render_client_default_timeout(self):
        main = runpy.run_path(str(Path(__file__).resolve().parents[1] / "cmd.py"))["main"]
        runner = Mock(return_value=([], "OK"))
        with patch("serial.Serial"), patch.object(sys, "argv", ["cmd.py", "COM7", "render 2000"]), \
             patch.dict(main.__globals__, {"run": runner, "wait_ready": Mock(return_value=True)}):
            self.assertEqual(main(), 0)
        self.assertEqual(runner.call_args.args[2], 150.0)

    def test_render_client_explicit_timeout(self):
        main = runpy.run_path(str(Path(__file__).resolve().parents[1] / "cmd.py"))["main"]
        runner = Mock(return_value=([], "OK"))
        with patch("serial.Serial"), patch.object(sys, "argv", ["cmd.py", "COM7", "render 2000", "2.5"]), \
             patch.dict(main.__globals__, {"run": runner, "wait_ready": Mock(return_value=True)}):
            self.assertEqual(main(), 0)
        self.assertEqual(runner.call_args.args[2], 2.5)


if __name__ == "__main__":
    unittest.main()
