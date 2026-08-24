"""Ensure internal Flash partitions match STM32F407 erase-sector boundaries."""

from pathlib import Path
import re
import unittest


PROJECT_DIR = Path(__file__).resolve().parents[2]
SECTOR_BOUNDARIES = {
    0x08000000,
    0x08004000,
    0x08008000,
    0x0800C000,
    0x08010000,
    0x08020000,
    0x08040000,
    0x08060000,
    0x08080000,
    0x080A0000,
    0x080C0000,
    0x080E0000,
    0x08100000,
}


def _hex_define(text: str, name: str) -> int:
    match = re.search(rf"#define\s+{name}\s+(0x[0-9A-Fa-f]+)", text)
    if not match:
        raise AssertionError(f"Missing hexadecimal define {name}")
    return int(match.group(1), 16)


def _kib_define(text: str, name: str) -> int:
    match = re.search(
        rf"#define\s+{name}\s+\((\d+)(?:UL)?\s*\*\s*1024(?:UL)?", text
    )
    if not match:
        raise AssertionError(f"Missing KiB define {name}")
    return int(match.group(1)) * 1024


class FlashPartitionLayoutTests(unittest.TestCase):
    def test_all_partitions_are_disjoint_and_sector_aligned(self):
        text = (PROJECT_DIR / "src" / "partition.h").read_text(encoding="utf-8")
        partitions = [
            (
                _hex_define(text, "ADDR_BOOTLOADER"),
                _kib_define(text, "SIZE_BOOTLOADER"),
            ),
            (_hex_define(text, "ADDR_APP_A"), _kib_define(text, "SIZE_APP_A")),
            (_hex_define(text, "ADDR_APP_B"), _kib_define(text, "SIZE_APP_B")),
            (
                _hex_define(text, "ADDR_CONFIG_JOURNAL"),
                _kib_define(text, "SIZE_CONFIG_JOURNAL"),
            ),
        ]

        self.assertEqual(
            [
                (0x08000000, 128 * 1024),
                (0x08020000, 384 * 1024),
                (0x08080000, 384 * 1024),
                (0x080E0000, 128 * 1024),
            ],
            partitions,
        )
        for index, (start, size) in enumerate(partitions):
            end = start + size
            self.assertIn(start, SECTOR_BOUNDARIES)
            self.assertIn(end, SECTOR_BOUNDARIES)
            if index:
                previous_start, previous_size = partitions[index - 1]
                self.assertEqual(previous_start + previous_size, start)

    def test_linker_scripts_match_runtime_partition_addresses(self):
        boot_ld = (PROJECT_DIR / "STM32F407ZGTx_BOOTLOADER.ld").read_text(
            encoding="utf-8"
        )
        app_a_ld = (PROJECT_DIR / "app" / "STM32F407ZGTx_APP.ld").read_text(
            encoding="utf-8"
        )
        app_b_ld = (PROJECT_DIR / "app" / "STM32F407ZGTx_APP_B.ld").read_text(
            encoding="utf-8"
        )

        self.assertIn("ORIGIN = 0x08000000, LENGTH = 128K", boot_ld)
        self.assertIn("ORIGIN = 0x08020000, LENGTH = 384K", app_a_ld)
        self.assertIn("ORIGIN = 0x08080000, LENGTH = 384K", app_b_ld)


if __name__ == "__main__":
    unittest.main()
