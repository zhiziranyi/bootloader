"""Contracts for factory-image provisioning and power-loss validation hooks."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class FactoryRecoveryContractTests(unittest.TestCase):
    def test_factory_provision_uses_isolated_external_flash_region(self):
        upgrade = (ROOT / "src" / "upgrade.c").read_text(encoding="utf-8")
        download = (ROOT / "network" / "download.c").read_text(encoding="utf-8")

        self.assertIn("int upgrade_factory_provision(const char *url)", upgrade)
        self.assertIn("download_start_to(url, 0, (uint32_t)size,", upgrade)
        self.assertIn("EXT_FACTORY_BASE, EXT_FACTORY_SIZE, false", upgrade)
        self.assertIn("int download_start_to(", download)
        self.assertIn("s_dl.flash_base", download)
        self.assertIn("bool download_has_failed(void)", download)
        self.assertIn("!download_has_failed()", upgrade)

    def test_factory_restore_only_accepts_slot_a_signed_image(self):
        source = (ROOT / "src" / "upgrade.c").read_text(encoding="utf-8")
        start = source.index("int upgrade_factory_restore(void)")
        body = source[start:source.index("bool upgrade_slot_has_valid_app", start)]

        self.assertIn("header.target_slot != FW_TARGET_SLOT_A", body)
        self.assertIn("Factory package must target slot A", body)

    def test_cli_exposes_factory_provision_without_overloading_restore(self):
        source = (ROOT / "src" / "cli.c").read_text(encoding="utf-8")

        self.assertIn("factory net <url>", source)
        self.assertIn("cmd_factory_net", source)
        start = source.index("static void process_line(void)")
        body = source[start:]
        self.assertLess(body.index('"factory net "'), body.index('"factory"'))

    def test_install_power_cut_hook_holds_after_swapping_is_persisted(self):
        source = (ROOT / "src" / "upgrade.c").read_text(encoding="utf-8")

        self.assertIn("void upgrade_arm_install_power_cut_test(void)", source)
        self.assertIn("config_update_state(UPGRADE_SWAPPING);", source)
        self.assertIn("[TEST] INSTALL HOLD", source)
        self.assertGreater(source.index("[TEST] INSTALL HOLD"),
                           source.index("config_update_state(UPGRADE_SWAPPING);"))

    def test_trial_power_cut_package_can_delay_confirmation(self):
        release = (ROOT / "tools" / "release_firmware.py").read_text(encoding="utf-8")
        app = (ROOT / "app" / "src" / "main.c").read_text(encoding="utf-8")

        self.assertIn("--trial-confirm-delay-ms", release)
        self.assertIn("APP_TRIAL_CONFIRM_DELAY_MS", release)
        self.assertIn("APP_TRIAL_CONFIRM_DELAY_MS", app)
        self.assertIn("[APP] Trial confirmation delayed", app)

    def test_desktop_exposes_factory_and_power_cut_workflows(self):
        source = (ROOT / "tools" / "desktop_app.py").read_text(encoding="utf-8")

        self.assertIn("factory net", source)
        self.assertIn("发送 factory net", source)
        self.assertIn("trial power-cut", source)
        self.assertIn("test power install", source)


if __name__ == "__main__":
    unittest.main()
