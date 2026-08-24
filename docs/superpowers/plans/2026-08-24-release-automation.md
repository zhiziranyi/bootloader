# Release Automation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add one command that produces verified A/B OTA release packages and document the complete demonstrated workflow.

**Architecture:** `app/include/app_version.h` is a generated, compile-time version source used by the shared App. `tools/release_firmware.py` coordinates the existing PlatformIO, packer, and verifier tools, then writes an auditable JSON release manifest. README explains the workflow without duplicating implementation details.

**Tech Stack:** Python 3 standard library, PlatformIO, STM32Cube HAL, ECDSA P-256.

---

### Task 1: Make the App banner use a generated release version

**Files:**
- Create: `app/include/app_version.h`
- Modify: `app/src/main.c`
- Test: `tools/tests/test_release_workflow.py`

- [ ] **Step 1: Write failing source-contract tests**

```python
def test_app_includes_generated_version_header(self):
    self.assertIn('#include "app_version.h"', app_source)
    self.assertIn('FlashSafe Pro App v%s', app_source)
    self.assertIn('APP_VERSION_STRING', app_source)
```

- [ ] **Step 2: Run the test**

Run: `python -m unittest tools.tests.test_release_workflow.ReleaseWorkflowTests.test_app_includes_generated_version_header`

Expected: failure because App currently embeds a release string directly.

- [ ] **Step 3: Add the generated header and wire it to the banner**

```c
#ifndef APP_VERSION_H
#define APP_VERSION_H
#define APP_VERSION_STRING "1.2.2"
#endif
```

```c
#include "app_version.h"
printf("  FlashSafe Pro App v%s\r\n", APP_VERSION_STRING);
```

- [ ] **Step 4: Re-run the test**

Run: `python -m unittest tools.tests.test_release_workflow.ReleaseWorkflowTests.test_app_includes_generated_version_header`

Expected: `OK`.

### Task 2: Implement the release orchestrator

**Files:**
- Create: `tools/release_firmware.py`
- Test: `tools/tests/test_release_workflow.py`

- [ ] **Step 1: Add failing metadata tests**

```python
def test_version_header_and_manifest_are_deterministic(self):
    module.write_app_version_header(tmp_path / "app_version.h", "2.3.4")
    self.assertIn('"2.3.4"', header.read_text())
    manifest = module.make_manifest("2.3.4", [package_a, package_b])
    self.assertEqual([p["target_slot"] for p in manifest["packages"]], ["A", "B"])
```

- [ ] **Step 2: Run the metadata tests**

Run: `python -m unittest tools.tests.test_release_workflow.ReleaseWorkflowTests.test_version_header_and_manifest_are_deterministic`

Expected: failure because the module does not exist.

- [ ] **Step 3: Implement minimal release functions and CLI**

```python
def write_app_version_header(path, version): ...
def make_manifest(version, package_paths): ...
def main():
    # validate X.Y.Z, write version header, build app_a/app_b,
    # pack/verify each matching target, then write release-vX.Y.Z.json
```

- [ ] **Step 4: Re-run metadata tests**

Run: `python -m unittest tools.tests.test_release_workflow.ReleaseWorkflowTests.test_version_header_and_manifest_are_deterministic`

Expected: `OK`.

### Task 3: Exercise the release command and refresh documentation

**Files:**
- Modify: `README.md`
- Create: `firmware/release-v1.2.2.json`

- [ ] **Step 1: Run a real no-upload release**

Run:

```powershell
python .\tools\release_firmware.py --version 1.2.2
```

Expected: `firmware_v1.2.2_slotA.pkg`, `firmware_v1.2.2_slotB.pkg`, and `release-v1.2.2.json`, with both packages host-verified.

- [ ] **Step 2: Rewrite README around demonstrated operation**

Document partition addresses, wiring, serial ROM upload, local HTTP server, both OTA directions, package release command, rollback behavior, HTML workbench role, and resume-ready feature summary.

- [ ] **Step 3: Run all regression tests**

Run: `python -m unittest discover -s tools\tests -p "test_*.py"`

Expected: all tests pass.
