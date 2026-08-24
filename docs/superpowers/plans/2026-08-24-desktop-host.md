# Desktop Host Workbench Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a standalone Windows EXE host application for all FlashSafe Pro PC-side operations.

**Architecture:** Tkinter renders the local controls and reuses the validated serial/process backend from `tools/webui.py`. A root-discovery helper allows the frozen executable to operate on the adjacent project rather than its temporary extraction folder.

**Tech Stack:** Python 3, Tkinter, PySerial, PlatformIO, PyInstaller.

---

### Task 1: Portable desktop entry point

**Files:**
- Create: `tools/desktop_app.py`
- Create: `tools/tests/test_desktop_app.py`
- Modify: `tools/webui.py`

- [ ] Write a failing root-discovery test for a frozen EXE located in `dist/` below a project root.
- [ ] Add the pure root helper and make `webui.py` honor `FLASHSAFE_PROJECT_ROOT`.
- [ ] Run the focused test.

### Task 2: Native controls

**Files:**
- Modify: `tools/desktop_app.py`

- [ ] Build controls for serial CLI, OTA, server, release, key rotation and fixed-target flash operations.
- [ ] Poll backend state and render device/task logs in the Tkinter UI.
- [ ] Run Python syntax checks and focused tests.

### Task 3: Package and desktop shortcut

**Files:**
- Modify: `README.md`

- [ ] Package with PyInstaller as `dist/FlashSafeProHost.exe` including backend imports.
- [ ] Create `C:\Users\13957\Desktop\FlashSafe Pro 上位机.lnk` with project root as StartIn.
- [ ] Run full tests and confirm the EXE and shortcut targets exist.
