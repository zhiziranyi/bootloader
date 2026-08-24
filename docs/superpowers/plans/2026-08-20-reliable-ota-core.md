# Reliable OTA Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the existing STM32F407/W25Q64 OTA bootloader safely boot either A/B slot, recover persisted state after ordinary power loss, and commit a new firmware version only after app confirmation.

**Architecture:** Replace the false two-copy 4 KB configuration scheme with an append-only journal inside physical flash sector 11. Build the shared application twice, each image linked for one execution slot; bind the intended slot in the signed package header. The bootloader validates the staged package, verifies the copied target body against the staged signed metadata, starts the pending image once, and promotes it only after the app appends a confirmation record.

**Tech Stack:** C11, STM32Cube HAL, PlatformIO, mbedTLS ECDSA P-256/SHA-256, Python 3 standard library plus `ecdsa`, existing STM32F407 board and W25Q64.

---

## File map

| Path | Responsibility |
|---|---|
| `src/partition.h` | Slot constants, signed package target-slot field, journal record schema. |
| `src/config.c`, `src/config.h` | Journal scan, append, compaction, and state-update API. |
| `src/upgrade.c`, `src/upgrade.h` | Target-slot checks, copy verification, trial selection, confirmation. |
| `src/ecdsa_verify.c`, `src/ecdsa_verify.h` | Stream a signed external header with a selected source body. |
| `app/platformio.ini`, `app/STM32F407ZGTx_APP_B.ld` | Produce two independently linked application artifacts. |
| `app/src/main.c`, `app/include/boot_confirm.h` | Confirm a successful trial without external hardware. |
| `tools/pack_firmware.py`, `tools/verify_pkg.py` | Target-slot-aware package creation and host verification. |
| `tools/tests/` | Host-runnable package, journal-contract, and vector-location regression tests. |
| `README.md` | Build commands, state machine, confirmation API, test commands, and HTTP threat boundary. |

### Task 1: Add test harness and package target-slot contract

**Files:**
- Create: `tools/tests/__init__.py`
- Create: `tools/tests/test_package_contract.py`
- Modify: `tools/pack_firmware.py`
- Modify: `tools/verify_pkg.py`
- Modify: `src/partition.h:77-89`

- [ ] **Step 1: Write the failing package-contract tests**

```python
class PackageContractTests(unittest.TestCase):
    def test_target_slot_is_stored_in_signed_header(self):
        package = pack_module.build_package(body=b"abc", target_slot=1)
        header = verify_module.parse_header(package)
        self.assertEqual(header.target_slot, 1)

    def test_verifier_rejects_unknown_target_slot(self):
        package = bytearray(pack_module.build_package(body=b"abc", target_slot=0))
        package[10:12] = (2).to_bytes(2, "little")
        self.assertFalse(verify_module.verify_bytes(bytes(package), self.public_key))
```

- [ ] **Step 2: Run the test and verify RED**

Run: `python -m unittest tools.tests.test_package_contract -v`

Expected: import or attribute failure because `build_package`, `parse_header`, and `verify_bytes` do not yet exist.

- [ ] **Step 3: Implement the minimum stable tooling API**

```python
TARGET_SLOT_A = 0
TARGET_SLOT_B = 1

def validate_target_slot(value: int) -> int:
    if value not in (TARGET_SLOT_A, TARGET_SLOT_B):
        raise ValueError("target slot must be A (0) or B (1)")
    return value
```

Use the existing 16-bit `reserved` field at header offset 10 as `target_slot`, include it in the existing header CRC and ECDSA digest, add `--target-slot {A,B}` to the packer, and add `--expected-slot {A,B}` to the verifier. Refactor file-based functions around byte-oriented `build_package`, `parse_header`, and `verify_bytes` helpers so tests do not need a device.

- [ ] **Step 4: Mirror the field in firmware headers**

```c
typedef enum {
    FW_TARGET_SLOT_A = 0,
    FW_TARGET_SLOT_B = 1
} fw_target_slot_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t ver_major;
    uint16_t ver_minor;
    uint16_t ver_patch;
    uint16_t target_slot;
    /* remaining fields stay byte-for-byte compatible in size */
} fw_header_t;
```

- [ ] **Step 5: Run package tests and existing package verification**

Run: `python -m unittest tools.tests.test_package_contract -v; python tools/verify_pkg.py --pkg firmware/firmware_v1.1.0.pkg --pubkey include/public_key.h`

Expected: new tests pass; the legacy package remains valid when interpreted as target slot A.

### Task 2: Specify and test the append-only configuration journal

**Files:**
- Create: `tools/journal_contract.py`
- Create: `tools/tests/test_journal_contract.py`
- Modify: `src/partition.h:46-75`
- Modify: `src/config.h:12-23`

- [ ] **Step 1: Write failing recovery tests**

```python
class JournalContractTests(unittest.TestCase):
    def test_newest_complete_crc_valid_record_wins(self):
        journal = [record(1, active=0), record(2, active=1)]
        self.assertEqual(select_latest(journal).sequence, 2)

    def test_torn_last_record_does_not_replace_previous_record(self):
        journal = [record(4, active=0), record(5, active=1, commit=False)]
        self.assertEqual(select_latest(journal).sequence, 4)

    def test_crc_corruption_does_not_replace_previous_record(self):
        journal = [record(7, active=0), record(8, active=1, crc_ok=False)]
        self.assertEqual(select_latest(journal).sequence, 7)
```

- [ ] **Step 2: Run the test and verify RED**

Run: `python -m unittest tools.tests.test_journal_contract -v`

Expected: fail because `journal_contract` does not exist.

- [ ] **Step 3: Implement the executable journal contract**

```python
COMMIT_ERASED = 0xFFFFFFFF
COMMIT_VALID = 0x00000000

def is_valid(record):
    return (record.commit == COMMIT_VALID and
            record.magic == CONFIG_MAGIC and
            record.crc == crc32(record.payload_without_crc_and_commit))

def select_latest(records):
    valid = [item for item in records if is_valid(item)]
    return max(valid, key=lambda item: item.sequence) if valid else None
```

Define the record byte layout once in comments and use the same field order in C. A fixed record must contain magic, schema, sequence, `boot_config_t` payload, CRC32, and final 32-bit commit marker. The commit marker is programmed only after all preceding words are written successfully.

- [ ] **Step 4: Extend the state schema and API declarations**

```c
typedef enum {
    UPGRADE_IDLE = 0,
    UPGRADE_DOWNLOADING,
    UPGRADE_DOWNLOADED,
    UPGRADE_VERIFYING,
    UPGRADE_VERIFIED,
    UPGRADE_INSTALLING,
    UPGRADE_TRIAL,
    UPGRADE_CONFIRMED,
    UPGRADE_FAILED
} upgrade_state_t;

HAL_StatusTypeDef config_mark_trial(active_slot_t slot,
                                    uint32_t major, uint32_t minor,
                                    uint32_t patch);
HAL_StatusTypeDef config_confirm_running_slot(active_slot_t slot);
```

Add `pending_slot`, `pending_version_*`, and `trial_boot_count` to `boot_config_t`. `active_slot` always names the last confirmed slot.

- [ ] **Step 5: Run the contract suite**

Run: `python -m unittest tools.tests.test_journal_contract -v`

Expected: all journal selection and torn-write tests pass.

### Task 3: Replace false redundancy with the STM32 sector-11 journal

**Files:**
- Modify: `src/partition.h:21-26`
- Modify: `src/config.c`
- Modify: `src/config.h`
- Test: `tools/tests/test_journal_contract.py`

- [ ] **Step 1: Add the failing journal-full and transition tests**

```python
def test_append_uses_next_erased_record_without_erasing_previous_record(self):
    journal = erased_journal(record_count=3)
    journal = append(journal, record(1, active=0))
    journal = append(journal, record(2, active=1))
    self.assertEqual(select_latest(journal).sequence, 2)
    self.assertTrue(is_valid(journal[0]))

def test_invalid_transition_is_rejected(self):
    self.assertFalse(can_transition("IDLE", "TRIAL"))
    self.assertTrue(can_transition("VERIFIED", "INSTALLING"))
```

- [ ] **Step 2: Run the test and verify RED**

Run: `python -m unittest tools.tests.test_journal_contract.JournalContractTests -v`

Expected: fail because `append` and `can_transition` are not implemented.

- [ ] **Step 3: Implement journal scan and append in C**

```c
#define ADDR_CONFIG_JOURNAL  0x080E0000UL
#define SIZE_CONFIG_JOURNAL  (128UL * 1024UL)
#define CONFIG_COMMIT_VALID  0x00000000UL

static HAL_StatusTypeDef config_append(const boot_config_t *next)
{
    config_record_t record = make_record(next, s_sequence + 1U);
    uint32_t address = find_first_erased_record();
    if (address == 0U) {
        return config_compact_and_append(next);
    }
    if (Flash_Internal_Write(address, (const uint8_t *)&record,
                             offsetof(config_record_t, commit)) != HAL_OK) {
        return HAL_ERROR;
    }
    return Flash_Internal_Write(address + offsetof(config_record_t, commit),
                                (const uint8_t *)&record.commit,
                                sizeof(record.commit));
}
```

`config_read()` scans every record in sector 11 and loads only the highest valid sequence. `config_write()` validates legal state transitions, writes one record, and updates `s_config` only after the final commit write succeeds. During full-sector compaction, erase sector 11, write the newest cached record first, then append the requested record; on restart with no valid record, default to slot A and require normal slot validation.

- [ ] **Step 4: Update all existing state writers to use the journal**

Replace direct `config_write` copies in download, upgrade, rollback, and factory restore paths with the journal-backed API. Remove `ADDR_CONFIG_SECTOR_0`, `ADDR_CONFIG_SECTOR_1`, and 4 KB pseudo-sector terminology.

- [ ] **Step 5: Build the bootloader and run host recovery tests**

Run: `python -m unittest tools.tests.test_journal_contract -v; C:\Users\13957\.platformio\penv\Scripts\platformio.exe run`

Expected: journal tests pass and the bootloader builds within the 96 KB linker region.

### Task 4: Build and verify slot-specific application artifacts

**Files:**
- Create: `app/STM32F407ZGTx_APP_B.ld`
- Modify: `app/STM32F407ZGTx_APP.ld`
- Modify: `app/platformio.ini`
- Create: `tools/check_app_slot.py`
- Create: `tools/tests/test_app_slot_layout.py`

- [ ] **Step 1: Write failing vector-location tests**

```python
class AppSlotLayoutTests(unittest.TestCase):
    def test_app_a_vectors_stay_in_slot_a(self):
        self.assertTrue(check_slot("app/.pio/build/app_a/firmware.bin", 0x08018000, 416 * 1024))

    def test_app_b_vectors_stay_in_slot_b(self):
        self.assertTrue(check_slot("app/.pio/build/app_b/firmware.bin", 0x08080000, 448 * 1024))
```

- [ ] **Step 2: Run the test and verify RED**

Run: `python -m unittest tools.tests.test_app_slot_layout -v`

Expected: fail because `app_a`, `app_b`, and `check_slot` do not exist.

- [ ] **Step 3: Add the two PlatformIO environments and B linker script**

```ini
[env:app_a]
extends = env:base
board_build.ldscript = $PROJECT_DIR/STM32F407ZGTx_APP.ld
build_flags = ${env:base.build_flags} -DAPP_BASE_ADDR=0x08018000

[env:app_b]
extends = env:base
board_build.ldscript = $PROJECT_DIR/STM32F407ZGTx_APP_B.ld
build_flags = ${env:base.build_flags} -DAPP_BASE_ADDR=0x08080000
```

Make the B linker script identical to the A linker script except `FLASH ORIGIN = 0x08080000` and `FLASH LENGTH = 448K`. Replace the hard-coded `SCB->VTOR = 0x08018000` in `app/src/main.c` with `SCB->VTOR = APP_BASE_ADDR`; reject a build if `APP_BASE_ADDR` is absent. `check_app_slot.py` reads the first two little-endian words in the binary, verifies the stack pointer is in SRAM, verifies the reset handler's Thumb bit, and verifies `(reset & ~1)` is inside `[slot_base, slot_base + slot_size)`.

- [ ] **Step 4: Build both images and verify GREEN**

Run: `C:\Users\13957\.platformio\penv\Scripts\platformio.exe run -d app -e app_a; C:\Users\13957\.platformio\penv\Scripts\platformio.exe run -d app -e app_b; python -m unittest tools.tests.test_app_slot_layout -v`

Expected: both app builds and both vector-location tests pass.

### Task 5: Implement target binding, destination verification, and trial boot

**Files:**
- Modify: `src/ecdsa_verify.c`, `src/ecdsa_verify.h`
- Modify: `src/upgrade.c`, `src/upgrade.h`
- Modify: `src/main.c`
- Modify: `src/cli.c`
- Modify: `tools/tests/test_package_contract.py`

- [ ] **Step 1: Add failing behavioral tests**

```python
def test_target_slot_must_be_inactive(self):
    self.assertFalse(target_slot_is_allowed(active_slot=0, package_slot=0))
    self.assertTrue(target_slot_is_allowed(active_slot=0, package_slot=1))

def test_unconfirmed_trial_reverts_to_confirmed_slot(self):
    state = BootState(active_slot=0, pending_slot=1, trial_boot_count=1)
    self.assertEqual(select_boot_slot(state), 0)

def test_confirmed_trial_promotes_pending_slot(self):
    state = confirm(BootState(active_slot=0, pending_slot=1, trial_boot_count=0), 1)
    self.assertEqual(state.active_slot, 1)
```

- [ ] **Step 2: Run tests and verify RED**

Run: `python -m unittest tools.tests.test_journal_contract tools.tests.test_package_contract -v`

Expected: fail because slot binding and trial-selection helpers are missing.

- [ ] **Step 3: Add selected-body cryptographic verification**

```c
int ecdsa_verify_firmware_body(uint32_t package_base,
                               const uint8_t *body, uint32_t body_size);
```

The function reads the signed header and signature block from `package_base`, streams SHA-256 over the header then `body_size` bytes from the internal-flash body pointer, checks `body_size == header.image_size`, checks body CRC, checks target-slot range, checks the embedded-key hash, and verifies the existing P-256 signature. `upgrade_swap()` calls it after copying to the target slot and before `config_mark_trial()`.

- [ ] **Step 4: Implement safe trial selection**

```c
if (cfg.pending_slot != SLOT_NONE && cfg.upgrade_state == UPGRADE_TRIAL) {
    if (cfg.trial_boot_count == 0U && upgrade_slot_has_valid_app(cfg.pending_slot)) {
        config_increment_trial_boot_count();
        return cfg.pending_slot;
    }
    config_clear_pending_as_failed();
}
return cfg.active_slot;
```

Reject a package if its signed target slot is not the inactive slot. Preserve `active_slot` through copying and trial. The CLI should print active/pending/trial information and state errors; it must not silently promote a pending image.

- [ ] **Step 5: Build and run regression tests**

Run: `python -m unittest discover -s tools/tests -v; C:\Users\13957\.platformio\penv\Scripts\platformio.exe run`

Expected: all host tests pass and bootloader build succeeds.

### Task 6: Add application trial confirmation and release packaging

**Files:**
- Create: `app/include/boot_confirm.h`
- Create: `app/src/boot_confirm.c`
- Modify: `app/src/main.c`
- Modify: `app/platformio.ini`
- Modify: `tools/regenerate_keys.py`
- Modify: `tools/tests/test_app_slot_layout.py`

- [ ] **Step 1: Add a failing confirmation-contract test**

```python
def test_confirmation_must_name_the_running_pending_slot(self):
    state = BootState(active_slot=0, pending_slot=1, trial_boot_count=1)
    self.assertFalse(confirm(state, 0).confirmed)
    self.assertTrue(confirm(state, 1).confirmed)
```

- [ ] **Step 2: Run the test and verify RED**

Run: `python -m unittest tools.tests.test_journal_contract.JournalContractTests.test_confirmation_must_name_the_running_pending_slot -v`

Expected: fail because the confirmation contract does not yet reject a non-pending slot.

- [ ] **Step 3: Implement the shared confirmation call**

```c
int boot_confirm_running_image(void)
{
    uint32_t vtor = SCB->VTOR;
    active_slot_t slot = (vtor == ADDR_APP_A) ? SLOT_A :
                         (vtor == ADDR_APP_B) ? SLOT_B : SLOT_NONE;
    return slot == SLOT_NONE ? -1 : config_confirm_running_slot(slot);
}
```

Compile the existing journal implementation into both projects, rather than duplicating the record format: append `../src/config.c`, `../src/drivers/crc32.c`, and `../src/drivers/flash_internal.c` to the app source filter and add `-I $PROJECT_DIR/../src`, `-I $PROJECT_DIR/../src/drivers`, and `-I $PROJECT_DIR/../include` to its build flags. `boot_confirm.c` calls `config_read()` then `config_confirm_running_slot(slot)` so it always scans the same journal as the bootloader. Call `boot_confirm_running_image()` only after the existing clock, GPIO, UART, and basic heartbeat self-check have completed. A failed confirmation leaves the trial unconfirmed so the next reset reverts.

- [ ] **Step 4: Produce both signed release artifacts**

```powershell
python tools/pack_firmware.py -i app/.pio/build/app_a/firmware.bin -k tools/keys/private_key.pem -p include/public_key.h -v 1.2.0 --target-slot A -o firmware/firmware_v1.2.0_slotA.pkg
python tools/pack_firmware.py -i app/.pio/build/app_b/firmware.bin -k tools/keys/private_key.pem -p include/public_key.h -v 1.2.0 --target-slot B -o firmware/firmware_v1.2.0_slotB.pkg
```

Update `regenerate_keys.py` to build both environments, package both target slots, and verify each package with the matching `--expected-slot` value.

- [ ] **Step 5: Run final automated validation**

Run: `python -m unittest discover -s tools/tests -v; C:\Users\13957\.platformio\penv\Scripts\platformio.exe run; C:\Users\13957\.platformio\penv\Scripts\platformio.exe run -d app -e app_a; C:\Users\13957\.platformio\penv\Scripts\platformio.exe run -d app -e app_b`

Expected: host tests pass, bootloader passes, and both application images pass slot-vector checks.

### Task 7: Document and prove the existing-board workflow

**Files:**
- Modify: `README.md`
- Modify: `开发参考.md`

- [ ] **Step 1: Add the acceptance checklist before editing docs**

```markdown
- [ ] Package signed for active slot is rejected.
- [ ] Download interruption resumes after reset.
- [ ] Install interruption retains the confirmed slot.
- [ ] New trial boots once and rolls back if not confirmed.
- [ ] Confirmed trial remains active after reset.
```

- [ ] **Step 2: Write the build and test documentation**

Document `app_a`/`app_b` build commands, per-slot packaging commands, target-slot binding, journal records, state transitions, `boot_confirm_running_image()`, `python -m unittest discover -s tools/tests -v`, and the five existing-board acceptance cases. State precisely that ECDSA protects authenticity/integrity while HTTP provides no confidentiality or availability protection.

- [ ] **Step 3: Run documentation-linked commands**

Run: `python -m unittest discover -s tools/tests -v; C:\Users\13957\.platformio\penv\Scripts\platformio.exe run -d app -e app_a; C:\Users\13957\.platformio\penv\Scripts\platformio.exe run -d app -e app_b`

Expected: each documented automated command passes without editing the instructions.
