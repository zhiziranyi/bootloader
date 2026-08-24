"""Host workbench contracts that do not need a USB device."""

from pathlib import Path
import importlib.util
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
WEBUI_SCRIPT = ROOT / "tools" / "webui.py"


def load_webui_module():
    spec = importlib.util.spec_from_file_location("flashsafe_host_workbench",
                                                  WEBUI_SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class HostWorkbenchTests(unittest.TestCase):
    def test_serial_port_validation_accepts_windows_com_port_only(self):
        webui = load_webui_module()

        self.assertEqual(webui.validate_serial_port("COM3"), "COM3")
        self.assertEqual(webui.validate_serial_port("com12"), "COM12")
        with self.assertRaises(ValueError):
            webui.validate_serial_port("COM3 & del *")

    def test_upload_target_is_limited_to_project_environments(self):
        webui = load_webui_module()

        self.assertEqual(webui.upload_environment("bootloader"),
                         "black_f407zg")
        self.assertEqual(webui.upload_environment("app_a"), "app_a")
        self.assertEqual(webui.upload_environment("app_b"), "app_b")
        with self.assertRaises(ValueError):
            webui.upload_environment("clean")

    def test_frozen_desktop_uses_configured_python_for_tool_processes(self):
        webui = load_webui_module()

        with mock.patch.dict(webui.os.environ,
                             {"FLASHSAFE_PYTHON": r"D:\python.exe"}, clear=False), \
             mock.patch.object(webui.sys, "frozen", True, create=True):
            self.assertEqual(webui.tool_python_executable(), r"D:\python.exe")

    def test_host_serial_sender_uses_one_carriage_return_terminator(self):
        source = WEBUI_SCRIPT.read_text(encoding="utf-8")
        start = source.index("def send_serial(")
        body = source[start:source.index("def platformio_executable", start)]

        self.assertIn('("\\r" if append_newline else "")', body)
        self.assertNotIn('("\\r\\n" if append_newline else "")', body)

    def test_html_contains_device_controls(self):
        html = (ROOT / "html" / "index.html").read_text(encoding="utf-8")

        for control in ("serial-port", "btn-serial-connect", "device-console",
                        "ota-url", "btn-fw-server", "btn-flash"):
            self.assertIn(f'id="{control}"', html)


if __name__ == "__main__":
    unittest.main()
