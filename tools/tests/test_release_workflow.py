"""Release automation contracts that do not require a connected board."""

from pathlib import Path
import importlib.util
import json
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
RELEASE_SCRIPT = ROOT / "tools" / "release_firmware.py"
ROTATE_SCRIPT = ROOT / "tools" / "regenerate_keys.py"
WEBUI_SCRIPT = ROOT / "tools" / "webui.py"


def load_release_module():
    spec = importlib.util.spec_from_file_location("release_firmware", RELEASE_SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_webui_module():
    spec = importlib.util.spec_from_file_location("flashsafe_webui", WEBUI_SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ReleaseWorkflowTests(unittest.TestCase):
    def test_key_rotation_rebuilds_bootloader_and_publishes_ab_release(self):
        source = ROTATE_SCRIPT.read_text(encoding="utf-8")

        self.assertIn('"-e", args.env', source)
        self.assertIn('"release_firmware.py"', source)
        self.assertIn('"--version", args.version', source)
        self.assertIn('private_key.pem.bak-', source)
        self.assertNotIn('firmware_v{args.version}.pkg', source)

    def test_app_includes_generated_version_header(self):
        source = (ROOT / "app" / "src" / "main.c").read_text(encoding="utf-8")

        self.assertIn('#include "app_version.h"', source)
        self.assertIn("FlashSafe Pro App v%s", source)
        self.assertIn("APP_VERSION_STRING", source)

    def test_release_script_writes_version_and_ordered_package_manifest(self):
        self.assertTrue(RELEASE_SCRIPT.is_file())
        release = load_release_module()

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            header = root / "app_version.h"
            package_a = root / "firmware_v2.3.4_slotA.pkg"
            package_b = root / "firmware_v2.3.4_slotB.pkg"
            package_a.write_bytes(b"slot-a")
            package_b.write_bytes(b"slot-b")

            release.write_app_version_header(header, "2.3.4")
            manifest = release.make_manifest("2.3.4", [
                {"path": package_b, "target_slot": "B"},
                {"path": package_a, "target_slot": "A"},
            ])

            self.assertIn('APP_VERSION_STRING "2.3.4"', header.read_text())
            self.assertEqual(manifest["version"], "2.3.4")
            self.assertEqual(
                [p["target_slot"] for p in manifest["packages"]], ["A", "B"]
            )
            self.assertEqual(
                manifest["packages"][0]["sha256"],
                "e1780476ffb3d1c15918e5b1d5a6892f4e6336d742610f9803c2b4108a3f3f3a",
            )

    def test_webui_lists_valid_release_manifests_newest_first(self):
        webui = load_webui_module()
        original_dir = webui.PKG_DIR
        try:
            with tempfile.TemporaryDirectory() as tmp:
                webui.PKG_DIR = Path(tmp)
                (webui.PKG_DIR / "release-v1.2.2.json").write_text(
                    json.dumps({"version": "1.2.2", "packages": []}),
                    encoding="utf-8",
                )
                (webui.PKG_DIR / "release-v1.2.3.json").write_text(
                    json.dumps({"version": "1.2.3", "packages": []}),
                    encoding="utf-8",
                )
                (webui.PKG_DIR / "release-bad.json").write_text("not json",
                                                                  encoding="utf-8")

                releases = webui.release_list()

                self.assertEqual([item["version"] for item in releases],
                                 ["1.2.3", "1.2.2"])
        finally:
            webui.PKG_DIR = original_dir


if __name__ == "__main__":
    unittest.main()
