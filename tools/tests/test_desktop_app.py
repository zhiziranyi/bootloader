"""Desktop host entry-point contracts without creating a GUI window."""

from pathlib import Path
import importlib.util
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
DESKTOP_APP = ROOT / "tools" / "desktop_app.py"


def load_desktop_module():
    spec = importlib.util.spec_from_file_location("flashsafe_desktop", DESKTOP_APP)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class DesktopHostTests(unittest.TestCase):
    def test_frozen_executable_finds_project_root_from_dist_folder(self):
        desktop = load_desktop_module()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "bootloader"
            dist = root / "dist"
            dist.mkdir(parents=True)
            (root / "platformio.ini").write_text("[env:test]\n", encoding="utf-8")

            found = desktop.find_project_root(dist, dist / "FlashSafeProHost.exe")

            self.assertEqual(found, root)

    def test_desktop_source_exposes_all_operation_sections(self):
        source = DESKTOP_APP.read_text(encoding="utf-8")
        for marker in ("_build_guide_tab", "generate_ota_url", '"info"',
                       "硬件在环报告", "export_hil_report", "analyze_hil_log"):
            self.assertIn(marker, source)

    def test_active_slot_is_read_from_boot_logs(self):
        desktop = load_desktop_module()
        self.assertEqual(desktop.active_slot_from_logs([
            {"t": "12:00:00", "m": "[CONFIG] seq=50 active=A pending=- state=8 trial=0"}
        ]), "A")
        self.assertEqual(desktop.active_slot_from_logs([
            {"t": "12:00:00", "m": "Active Slot: B"}
        ]), "B")


if __name__ == "__main__":
    unittest.main()
