"""Contracts for the on-device IEEE CRC-32 implementation."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class Crc32ContractTests(unittest.TestCase):
    def test_update_uses_the_complete_reflected_ieee_polynomial_step(self):
        source = (ROOT / "src" / "drivers" / "crc32.c").read_text(encoding="utf-8")

        self.assertIn("for (uint32_t bit = 0U; bit < 8U; bit++)", source)
        self.assertIn("(crc >> 1) ^ 0xEDB88320U", source)
        self.assertNotIn("static const uint32_t crc32_table", source)


if __name__ == "__main__":
    unittest.main()
