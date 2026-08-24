# HIL Evidence Report Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn real FlashSafe serial logs into a reproducible Hardware-in-the-Loop validation report.

**Architecture:** A dependency-free Python module recognizes only explicit Bootloader evidence lines and renders a Markdown report. The Tkinter host adds a report tab that analyzes the current serial session and exports the report; manual power removal remains a human action and is recorded as evidence rather than simulated.

**Tech Stack:** Python 3, `unittest`, Tkinter, existing PySerial log model.

---

### Task 1: Evidence analyzer

**Files:**
- Create: `tools/hil_report.py`
- Create: `tools/tests/test_hil_report.py`

- [ ] **Step 1: Write failing tests for explicit evidence detection**

```python
def test_analyzer_marks_trial_rollback_and_factory_recovery_from_device_lines():
    analysis = analyze_hil_log([
        {"t": "10:00:00", "m": "[BOOT] Trial was not confirmed; reverting to slot A"},
        {"t": "10:00:01", "m": "[UPGRADE] Factory restore OK -> slot A, v2.0.15"},
    ])
    self.assertTrue(analysis["trial_power_rollback"]["passed"])
    self.assertTrue(analysis["factory_restore"]["passed"])
```

- [ ] **Step 2: Run the focused test and confirm it fails because the module is absent**

Run: `python -m unittest tools.tests.test_hil_report -v`

- [ ] **Step 3: Implement the analyzer and Markdown renderer**

```python
CHECKS = {
    "secure_ota": ("[ECDSA] Firmware signature VALID", "[UPGRADE] Verified OK:"),
    "trial_confirm": ("[UPGRADE] Installed trial slot", "[APP] Trial firmware confirmed"),
    "trial_power_rollback": ("[APP] Trial confirmation delayed", "[BOOT] Trial was not confirmed; reverting"),
    "factory_provision": ("[FACTORY] Image ready:", "[OK] Factory image provisioned and signature verified."),
    "factory_restore": ("[UPGRADE] Factory restore OK -> slot A",),
}
```

- [ ] **Step 4: Run focused tests and confirm they pass**

Run: `python -m unittest tools.tests.test_hil_report -v`

### Task 2: Native host report workflow

**Files:**
- Modify: `tools/desktop_app.py`
- Modify: `tools/tests/test_desktop_app.py`

- [ ] **Step 1: Write a failing source contract for the HIL report tab and export action**

```python
self.assertIn("硬件在环报告", source)
self.assertIn("export_hil_report", source)
```

- [ ] **Step 2: Run the focused contract and confirm it fails**

Run: `python -m unittest tools.tests.test_desktop_app -v`

- [ ] **Step 3: Add an HIL tab with evidence status, manual test guidance, and Markdown export**

```python
analysis = hil_report.analyze_hil_log(serial_status["logs"])
markdown = hil_report.render_hil_markdown(analysis, metadata)
output.write_text(markdown, encoding="utf-8")
```

- [ ] **Step 4: Run focused tests and confirm they pass**

Run: `python -m unittest tools.tests.test_desktop_app -v`

### Task 3: Regression and delivery

**Files:**
- Modify: `README.md`
- Modify: `docs/HANDOVER.md`

- [ ] **Step 1: Document that report evidence is collected from real UART output and does not emulate physical power loss**

- [ ] **Step 2: Run full host test suite and syntax compilation**

Run: `python -m unittest discover -s .\tools\tests -p "test_*.py"; python -m compileall -q .\tools`

- [ ] **Step 3: Rebuild the desktop executable and update the desktop shortcut**

Run: `python -m PyInstaller --noconfirm --clean --onefile --name FlashSafeProHost tools\desktop_app.py`

- [ ] **Step 4: Commit only the HIL report source, tests, and documentation**

```bash
git add tools/hil_report.py tools/desktop_app.py tools/tests/test_hil_report.py tools/tests/test_desktop_app.py README.md docs/HANDOVER.md docs/superpowers/plans/2026-08-24-hil-evidence-report.md
git commit -m "feat: add HIL evidence reporting"
```
