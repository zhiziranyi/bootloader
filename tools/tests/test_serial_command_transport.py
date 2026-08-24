"""Regression tests for the desktop serial command transport."""

from pathlib import Path
import sys
from unittest import mock
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import webui


class FakeSerial:
    is_open = True

    def __init__(self):
        self.writes = []
        self.flushed = False

    def write(self, data):
        self.writes.append(bytes(data))
        return len(data)

    def flush(self):
        self.flushed = True


class SerialCommandTransportTests(unittest.TestCase):
    def test_long_command_is_paced_byte_by_byte_and_ends_with_cr(self):
        device = FakeSerial()
        command = "upgrade net 192.168.137.1:8000/firmware_v2.0.11_slotB.pkg"

        with mock.patch.object(webui.time, "sleep") as sleep:
            webui.write_serial_command(device, command)

        self.assertEqual(b"".join(device.writes), (command + "\r").encode("utf-8"))
        self.assertTrue(all(len(part) == 1 for part in device.writes))
        self.assertTrue(device.flushed)
        self.assertEqual(sleep.call_count, len(command))

    def test_trial_delay_requires_a_real_power_cut_window(self):
        with self.assertRaisesRegex(ValueError, "between 5000 and 60000"):
            webui.validate_trial_confirm_delay(150)
        self.assertEqual(webui.validate_trial_confirm_delay(15000), 15000)


if __name__ == "__main__":
    unittest.main()
