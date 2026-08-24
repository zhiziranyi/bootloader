"""Tests for extracting Hardware-in-the-Loop evidence from UART logs."""

from pathlib import Path
import tempfile
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

    def test_persisted_complete_evidence_survives_a_new_desktop_session(self):
        live = hil_report.analyze_hil_log([
            {"t": "12:00:00", "m": "[APP] Trial confirmation delayed for 15000 ms"},
            {"t": "12:00:16", "m": "[BOOT] Trial was not confirmed; reverting to slot A"},
        ])
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "hil-evidence.json"
            saved, changed = hil_report.record_completed_evidence(
                hil_report.load_evidence(path), live, recorded_at="2026-08-24T12:01:00Z"
            )
            self.assertTrue(changed)
            hil_report.save_evidence(path, saved)

            reloaded = hil_report.load_evidence(path)
            later = hil_report.combine_with_saved_evidence(
                hil_report.analyze_hil_log([]), reloaded
            )

        result = later["trial_power_rollback"]
        self.assertTrue(result["passed"])
        self.assertEqual(result["source"], "历史保存证据")
        self.assertEqual(result["recorded_at"], "2026-08-24T12:01:00Z")

    def test_incomplete_evidence_is_not_saved_as_a_pass(self):
        partial = hil_report.analyze_hil_log([
            {"t": "13:00:00", "m": "[APP] Trial confirmation delayed for 15000 ms"},
        ])

        saved, changed = hil_report.record_completed_evidence({}, partial)

        self.assertFalse(changed)
        self.assertNotIn("trial_power_rollback", saved.get("checks", {}))

    def test_legacy_markdown_report_is_migrated_only_when_its_evidence_is_complete(self):
        with tempfile.TemporaryDirectory() as tmp:
            report = Path(tmp) / "hil-report-20260824-120000.md"
            report.write_text(
                "### Trial 未确认自动回退 — 通过\n"
                "```text\n"
                "[12:00:00] [APP] Trial confirmation delayed for 15000 ms\n"
                "[12:00:16] [BOOT] Trial was not confirmed; reverting to slot A\n"
                "```\n",
                encoding="utf-8",
            )

            saved, changed = hil_report.import_legacy_reports(Path(tmp), {})

        self.assertTrue(changed)
        self.assertIn("trial_power_rollback", saved["checks"])


if __name__ == "__main__":
    unittest.main()
