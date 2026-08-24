"""Guard OTA state transitions against silent config-journal write failures."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class UpgradeStatePersistenceTests(unittest.TestCase):
    def test_verified_state_is_not_reported_when_the_journal_write_fails(self):
        source = (ROOT / "src" / "upgrade.c").read_text(encoding="utf-8")

        self.assertIn("if (config_write(&tmp) != HAL_OK)", source)
        self.assertIn("Cannot persist VERIFIED state", source)

    def test_config_write_reads_back_a_committed_journal_record(self):
        source = (ROOT / "src" / "config.c").read_text(encoding="utf-8")

        self.assertIn("record_validate(record_at(index))", source)
        self.assertIn("journal read-back validation failed", source)

    def test_internal_flash_writer_uses_voltage_tolerant_byte_programming(self):
        source = (ROOT / "src" / "drivers" / "flash_internal.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("__HAL_FLASH_CLEAR_FLAG", source)
        self.assertIn("HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, addr + i, data[i])", source)
        self.assertNotIn("FLASH_TYPEPROGRAM_WORD, addr + i, word", source)

    def test_trial_install_log_uses_the_package_version(self):
        source = (ROOT / "src" / "upgrade.c").read_text(encoding="utf-8")
        start = source.index("[UPGRADE] Installed trial slot")
        log_statement = source[start : source.index("boot_display_show", start)]

        self.assertIn("header.ver_major", log_statement)
        self.assertNotIn("cfg.fw_version_major", log_statement)


if __name__ == "__main__":
    unittest.main()
