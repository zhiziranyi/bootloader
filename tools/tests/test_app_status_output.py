"""Keep App status text consistent with the linker-selected execution slot."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]


class AppStatusOutputTests(unittest.TestCase):
    def test_app_banner_uses_a_valid_generated_release_version(self):
        source = (ROOT / "app" / "src" / "main.c").read_text(encoding="utf-8")
        version_header = (ROOT / "app" / "include" / "app_version.h").read_text(
            encoding="utf-8"
        )

        self.assertRegex(
            version_header,
            r'#define\s+APP_VERSION_STRING\s+"\d+\.\d+\.\d+"',
        )
        self.assertIn("FlashSafe Pro App v%s", source)
        self.assertIn("APP_VERSION_STRING", source)

    def test_heartbeat_reports_the_link_time_slot_address(self):
        source = (ROOT / "app" / "src" / "main.c").read_text(encoding="utf-8")
        start = source.index("[APP] Heartbeat")
        heartbeat = source[start : source.index("counter = 0", start)]

        self.assertIn("(unsigned long)APP_BASE_ADDR", heartbeat)
        self.assertNotIn("0x08020000", heartbeat)


if __name__ == "__main__":
    unittest.main()
