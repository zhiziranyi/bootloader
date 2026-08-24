import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class ClockConfigurationTests(unittest.TestCase):
    def test_all_images_use_the_board_8mhz_hse(self):
        for path in (ROOT / "platformio.ini", ROOT / "app" / "platformio.ini"):
            self.assertIn("-DHSE_VALUE=8000000", path.read_text(encoding="utf-8"))

    def test_all_system_clock_setups_use_pllm_8(self):
        for path in (ROOT / "src" / "main.c", ROOT / "app" / "src" / "main.c"):
            self.assertIn("RCC_OscInitStruct.PLL.PLLM = 8;", path.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
