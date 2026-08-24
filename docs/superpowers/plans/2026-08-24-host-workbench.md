# Local Host Workbench Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the existing HTML workbench into a localhost-only FlashSafe Pro host application.

**Architecture:** `tools/webui.py` serves `html/` and owns serial ports plus safe subprocesses. The browser polls status/log APIs and invokes limited JSON operations.

**Tech Stack:** Python 3, stdlib HTTP server, PySerial 3.5, PlatformIO, HTML/CSS/vanilla JavaScript.

---

### Task 1: Backend contracts

**Files:**
- Modify: `tools/webui.py`
- Create: `tools/tests/test_host_workbench.py`

- [ ] Add failing tests for COM port and flash-target validation.
- [ ] Run `python -m unittest tools.tests.test_host_workbench` and observe missing helper failure.
- [ ] Add validation helpers, serial session management, firmware-server/release/flash runners and JSON endpoints.
- [ ] Re-run the focused test.

### Task 2: Host UI

**Files:**
- Modify: `html/index.html`
- Modify: `html/js/main.js`
- Modify: `html/css/style.css`

- [ ] Add a failing static test for the device-console and controls.
- [ ] Add device, OTA, release, firmware-server and serial-flash panels to the existing HTML workbench.
- [ ] Make the JavaScript poll status and call the JSON endpoints; retain package/key downloads and rotation controls.
- [ ] Re-run the focused test.

### Task 3: Documentation and verification

**Files:**
- Modify: `README.md`

- [ ] Document startup, serial-port exclusivity and hardware-only BOOT0 requirement.
- [ ] Run all Python tests.
- [ ] Build `black_f407zg` to ensure the host-tool additions do not affect firmware build inputs.
