"""Protect the Bootloader-to-App stack-pointer handoff contract."""

from pathlib import Path
import struct
import unittest


PROJECT_DIR = Path(__file__).resolve().parents[2]
RAM_TOP = 0x20020000


class AppJumpContractTests(unittest.TestCase):
    def test_valid_initial_stack_pointer_at_ram_top_is_accepted(self):
        app_image = PROJECT_DIR / "app" / ".pio" / "build" / "app_a" / "firmware.bin"
        initial_sp = struct.unpack("<I", app_image.read_bytes()[:4])[0]
        jump_header = (PROJECT_DIR / "src" / "jump.h").read_text(encoding="utf-8")
        jump_source = (PROJECT_DIR / "src" / "jump.c").read_text(encoding="utf-8")

        self.assertEqual(RAM_TOP, initial_sp)
        self.assertIn("#define RAM_TOP_ADDR    0x20020000", jump_header)
        self.assertIn("sp > RAM_TOP_ADDR", jump_source)


if __name__ == "__main__":
    unittest.main()
