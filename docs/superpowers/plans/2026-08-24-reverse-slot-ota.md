# Reverse Slot OTA Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce a signed v1.2.2 App-A package so the confirmed Slot-B firmware can safely demonstrate the reverse B-to-A OTA path.

**Architecture:** The common App source is compiled twice with `APP_BASE_ADDR` supplied by the PlatformIO environment. The existing `app_a` linker environment targets `0x08020000`; the package header marks the artifact as target slot A. No Bootloader, Ethernet, or signing implementation changes are required.

**Tech Stack:** STM32F407 HAL, PlatformIO, Python firmware packer, ECDSA P-256.

---

### Task 1: Correct the App execution-address status line

**Files:**
- Modify: `app/src/main.c`
- Create: `tools/tests/test_app_status_output.py`

- [ ] **Step 1: Write the failing test**

```python
def test_heartbeat_reports_the_link_time_slot_address(self):
    source = (ROOT / "app" / "src" / "main.c").read_text(encoding="utf-8")
    heartbeat = source[source.index("[APP] Heartbeat") : source.index("counter = 0", source.index("[APP] Heartbeat"))]
    self.assertIn("(unsigned long)APP_BASE_ADDR", heartbeat)
    self.assertNotIn("0x08020000", heartbeat)
```

- [ ] **Step 2: Run the test and verify it fails**

Run: `python -m unittest tools.tests.test_app_status_output`

Expected: failure because the message contains the hard-coded App-A address.

- [ ] **Step 3: Implement the minimal fix**

```c
printf("[APP] Heartbeat #%lu - still alive at 0x%08lX\r\n",
       (unsigned long)counter, (unsigned long)APP_BASE_ADDR);
```

- [ ] **Step 4: Run the test and verify it passes**

Run: `python -m unittest tools.tests.test_app_status_output`

Expected: `OK`.

### Task 2: Build, package, and host-verify the reverse artifact

**Files:**
- Create: `firmware/firmware_v1.2.2_slotA.pkg`

- [ ] **Step 1: Build the App-A artifact**

Run: `C:\Users\13957\.platformio\penv\Scripts\platformio.exe run -d app -e app_a`

Expected: successful `app_a` build linked for `0x08020000`.

- [ ] **Step 2: Package the signed Slot-A image**

Run:

```powershell
python .\tools\pack_firmware.py --input .\app\.pio\build\app_a\firmware.bin --key .\tools\keys\private_key.pem --pubkey .\include\public_key.h --version 1.2.2 --output .\firmware\firmware_v1.2.2_slotA.pkg --target-slot A
```

- [ ] **Step 3: Verify the package using the embedded public key**

Run:

```powershell
python .\tools\verify_pkg.py --pkg .\firmware\firmware_v1.2.2_slotA.pkg --pubkey .\include\public_key.h --expected-slot A
```

Expected: valid low-S ECDSA signature and target slot A.

- [ ] **Step 4: Run the full regression suite**

Run: `python -m unittest discover -s tools\tests -p "test_*.py"`

Expected: all tests pass.
