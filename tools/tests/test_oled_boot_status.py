"""Static contract tests for the SSD1306 boot status display."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class OledBootStatusTests(unittest.TestCase):
    def test_ssd1306_driver_matches_the_working_pb6_pb7_software_i2c(self):
        header = (ROOT / "src" / "oled_display.h").read_text(encoding="utf-8")
        driver = (ROOT / "src" / "oled_display.c").read_text(encoding="utf-8")

        self.assertIn("GPIO_PIN_6", header)
        self.assertIn("GPIO_PIN_7", header)
        self.assertIn("OLED_ADDRESS_3C", header)
        self.assertIn("0x78U", header)
        self.assertIn("oled_i2c_start", driver)
        self.assertIn("oled_i2c_send_byte", driver)
        self.assertIn("HAL_Delay(100U)", driver)
        self.assertIn("0xAE", driver)
        self.assertIn("0xAF", driver)

    def test_driver_retries_both_common_ssd1306_addresses(self):
        header = (ROOT / "src" / "oled_display.h").read_text(encoding="utf-8")
        driver = (ROOT / "src" / "oled_display.c").read_text(encoding="utf-8")

        self.assertIn("OLED_ADDRESS_3D", header)
        self.assertIn("0x7AU", header)
        self.assertIn("OLED_PROBE_RETRIES", driver)
        self.assertIn("OLED_ADDRESS_3C", driver)
        self.assertIn("OLED_ADDRESS_3D", driver)

    def test_main_uses_gpio_software_i2c_without_i2c1_dependency(self):
        main = (ROOT / "src" / "main.c").read_text(encoding="utf-8")
        hal_conf = (ROOT / "include" / "stm32f4xx_hal_conf.h").read_text(encoding="utf-8")

        self.assertNotIn("MX_I2C1_Init", main)
        self.assertNotIn("I2C_HandleTypeDef hi2c1", main)
        self.assertNotIn("HAL_I2C_MODULE_ENABLED", hal_conf)
        self.assertIn("boot_display_init()", main)

    def test_boot_status_screen_covers_upgrade_lifecycle(self):
        status = (ROOT / "src" / "boot_display.c").read_text(encoding="utf-8")
        main = (ROOT / "src" / "main.c").read_text(encoding="utf-8")

        for state in (
            "UPGRADE_IDLE", "UPGRADE_DOWNLOADING", "UPGRADE_VERIFYING",
            "UPGRADE_SWAPPING", "UPGRADE_TRIAL", "UPGRADE_FAILED",
        ):
            self.assertIn(state, status)
        self.assertIn("boot_display_init", main)
        self.assertIn("boot_display_show", main)


if __name__ == "__main__":
    unittest.main()
