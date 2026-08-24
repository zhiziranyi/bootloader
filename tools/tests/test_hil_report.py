"""Tests for extracting Hardware-in-the-Loop evidence from UART logs."""

from pathlib import Path
import sys
import unittest


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import hil_report


class HilReportTests(unittest.TestCase):
    def test_analyzer_marks_trial_rollback_and_factory_recovery(self):
        analysis = hil_report.analyze_hil_log([
            {"t": "10:00:00", "m": "[APP] Trial confirmation delayed for 15000 ms"},
            {"t": "10:00:15", "m": "[BOOT] Trial was not confirmed; reverting to slot A"},
            {"t": "10:00:20", "m": "[UPGRADE] Factory restore OK -> slot A, v2.0.15"},
        ])

        self.assertTrue(analysis["trial_power_rollback"]["passed"])
        self.assertTrue(analysis["factory_restore"]["passed"])
        self.assertFalse(analysis["secure_ota"]["passed"])

    def test_report_contains_passed_evidence_and_unverified_manual_scope(self):
        analysis = hil_report.analyze_hil_log([
            {"t": "11:00:00", "m": "[ECDSA] Firmware signature VALID"},
            {"t": "11:00:01", "m": "[UPGRADE] Verified OK: v2.0.15, size=11292 bytes"},
        ])

        report = hil_report.render_hil_markdown(
            analysis,
            {"board": "STM32F407ZGT6", "serial": "COM3 @ 115200"},
        )

        self.assertIn("# FlashSafe Pro HIL 测试报告", report)
        self.assertIn("安全 OTA 验签", report)
        self.assertIn("通过", report)
        self.assertIn("物理断电由测试人员执行", report)
        self.assertIn("[ECDSA] Firmware signature VALID", report)


if __name__ == "__main__":
    unittest.main()
