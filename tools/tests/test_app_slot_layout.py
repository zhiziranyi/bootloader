import pathlib
import sys
import unittest


TOOLS_DIR = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIR))

import check_app_slot


ROOT = pathlib.Path(__file__).resolve().parents[2]


class AppSlotLayoutTests(unittest.TestCase):
    def test_app_a_vectors_stay_in_slot_a(self):
        image = ROOT / "app" / ".pio" / "build" / "app_a" / "firmware.bin"
        self.assertTrue(check_app_slot.check_slot(image, 0x08020000, 384 * 1024))

    def test_app_b_vectors_stay_in_slot_b(self):
        image = ROOT / "app" / ".pio" / "build" / "app_b" / "firmware.bin"
        self.assertTrue(check_app_slot.check_slot(image, 0x08080000, 384 * 1024))


if __name__ == "__main__":
    unittest.main()
