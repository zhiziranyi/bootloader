"""Prevent serial-upload configuration regressions for bootloader and A/B apps."""

from pathlib import Path
import json
import unittest


PROJECT_DIR = Path(__file__).resolve().parents[2]


def _section(text: str, name: str) -> str:
    start = text.index(f"[{name}]")
    following = text.find("\n[", start + 1)
    return text[start:] if following == -1 else text[start:following]


class SerialUploadConfigurationTests(unittest.TestCase):
    def test_workspace_exposes_bootloader_and_app_projects(self):
        workspace = json.loads(
            (PROJECT_DIR / "bootloader.code-workspace").read_text(encoding="utf-8")
        )
        folders = {folder["path"] for folder in workspace["folders"]}

        self.assertEqual({".", "app"}, folders)

    def test_bootloader_uses_canopen_style_serial_upload(self):
        text = (PROJECT_DIR / "platformio.ini").read_text(encoding="utf-8")

        self.assertIn("upload_protocol = serial", text)
        self.assertIn("upload_speed = 115200", text)
        self.assertIn("upload_port = COM3", text)
        self.assertNotIn("upload_protocol = stlink", text)
        self.assertIn("board_upload.offset_address = 0x08000000", text)
        self.assertIn("custom_upload_length = 131072", text)
        self.assertIn(
            "extra_scripts = post:$PROJECT_DIR/tools/stm32_serial_offset.py", text
        )

    def test_app_slots_use_serial_upload_and_preserve_their_flash_addresses(self):
        text = (PROJECT_DIR / "app" / "platformio.ini").read_text(encoding="utf-8")
        upload_script = (PROJECT_DIR / "tools" / "stm32_serial_offset.py")

        base = _section(text, "env:base")
        app_a = _section(text, "env:app_a")
        app_b = _section(text, "env:app_b")

        self.assertIn("upload_protocol = serial", base)
        self.assertIn("upload_speed = 115200", base)
        self.assertIn("upload_port = COM3", base)
        self.assertNotIn("upload_protocol = stlink", text)
        self.assertIn("board_upload.offset_address = 0x08020000", app_a)
        self.assertIn("custom_upload_length = 393216", app_a)
        self.assertIn("board_upload.offset_address = 0x08080000", app_b)
        self.assertIn("custom_upload_length = 393216", app_b)
        self.assertIn("extra_scripts = post:$PROJECT_DIR/../tools/stm32_serial_offset.py", base)
        self.assertTrue(upload_script.exists())
        script = upload_script.read_text(encoding="utf-8")
        self.assertIn('"-S"', script)
        self.assertIn("custom_upload_length", script)
        self.assertIn('slot_address + ":" + slot_length', script)
        self.assertIn('("0x08000000", "131072")', script)
        self.assertIn('("0x08020000", "393216")', script)
        self.assertIn('("0x08080000", "393216")', script)
        self.assertIn(
            'env.Prepend(UPLOADERFLAGS=["-S", slot_address + ":" + slot_length, "-v"])',
            script,
        )
        self.assertNotIn("env.Append(UPLOADERFLAGS", script)
        self.assertIn("UPLOADERFLAGS", script)
        self.assertIn("upload.offset_address", script)
        self.assertIn("def show_upload_command(source, target, env):", script)


if __name__ == "__main__":
    unittest.main()
