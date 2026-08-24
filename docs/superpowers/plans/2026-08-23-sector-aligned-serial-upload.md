# Sector-Aligned Serial Upload Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prevent serial uploads from erasing adjacent firmware partitions and expose App_A/App_B in the VS Code environment selector.

**Architecture:** Align all internal-flash partitions to STM32F407 erase sectors. Pass an explicit address and partition length to `stm32flash`, while keeping Bootloader and App as independent PlatformIO projects inside one multi-root workspace.

**Tech Stack:** PlatformIO, STM32Cube HAL, STM32F407ZGT6 internal Flash, stm32flash, Python unittest, VS Code multi-root workspace.

---

### Task 1: Add regression coverage

**Files:**
- Modify: `tools/tests/test_serial_upload_config.py`
- Modify: `tools/tests/test_app_slot_layout.py`

- [x] Assert the workspace contains both `.` and `app` project folders.
- [x] Assert each upload environment has the correct sector-aligned address and bounded length.
- [x] Assert App_A vectors use `0x08020000` with a 384 KiB slot.
- [x] Run the tests and confirm they fail against the old layout.

### Task 2: Align partitions and bound serial erases

**Files:**
- Modify: `src/partition.h`
- Modify: `STM32F407ZGTx_BOOTLOADER.ld`
- Modify: `app/STM32F407ZGTx_APP.ld`
- Modify: `app/platformio.ini`
- Modify: `platformio.ini`
- Modify: `tools/stm32_serial_offset.py`
- Modify: `app/src/main.c`
- Modify: `bootloader.code-workspace`

- [x] Move App_A to sector 5 at `0x08020000` and make Bootloader/App_A sizes 128/384 KiB.
- [x] Add explicit partition lengths to all serial upload commands.
- [x] Add Bootloader and App folders to the workspace.
- [x] Run the regression tests and confirm they pass.

### Task 3: Documentation and full verification

**Files:**
- Modify: `README.md`

- [x] Update the documented layout and upload safety behavior.
- [x] Build Bootloader, App_A, and App_B.
- [x] Validate both application images against their slots.
- [x] Print and inspect all resolved serial upload commands.
