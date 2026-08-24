# Reliable OTA Core Design

**Status:** Approved for planning

## Goal

Make FlashSafe Pro's OTA upgrade path recoverable across reset or power loss, able to trial a new application before confirmation, and supported by host-runnable regression tests.

## Scope

This first phase changes the upgrade core, the demo application build, and host-side verification. It does not add TLS, a new transport, a new MCU, external hardware, or a web UI feature. Documentation, CI, and portfolio presentation follow as separate phases after the core is verified.

The existing STM32F407 board, W25Q64 external flash, Ethernet interface, and ST-Link workflow remain sufficient for all development and acceptance tests.

## Current A/B execution defect

The application linker script currently produces only an App A image linked at `0x08018000`. The upgrade code writes that same image to the inactive slot, including App B at `0x08080000`. Its vector table therefore contains absolute handler addresses for App A, so it cannot be safely launched from App B. A correct direct-execute-in-place A/B design must produce an image specifically linked for each slot.

## Current constraint and defect

STM32F407ZG flash sectors are not 4 KB erasable units in the final 128 KB region. `ADDR_CONFIG_SECTOR_0` (`0x080F0000`) and `ADDR_CONFIG_SECTOR_1` (`0x080F4000`) both map to physical sector 11 (`0x080E0000` to `0x080FFFFF`). The current primary-then-backup write therefore erases the primary copy while writing the backup. It is not redundant and cannot preserve a valid state through interruption.

## Selected approach

Use a single physical configuration sector as an append-only journal of fixed-size, CRC-protected records. A record is written only once; a newer record supersedes older records by monotonically increasing sequence number. Erasure happens only when the journal is full, after the newest valid record has been retained in RAM and with an explicitly documented limitation: power loss during a full-sector compaction falls back to the last bootable application rather than trusting a partially written state.

This approach reserves physical sector 11 for the journal, so App B ends at `0x080E0000` and has 384 KB. This is simpler and safer than pretending distinct 4 KB regions exist inside one erase sector; the compaction boundary is observable and testable.

Use one shared application source tree with two PlatformIO environments: `app_a` linked to `0x08018000` and `app_b` linked to `0x08080000`. The packer requires the target slot and writes that slot identifier into the signed firmware header. The bootloader accepts an update only when the package target is the current inactive slot. The release process produces one package per slot for each firmware version; the package file name makes the slot explicit.

## State and data model

Replace the implicit two-copy layout with journal records containing:

- immutable record magic and schema version;
- sequence number;
- active and pending slot;
- current and pending firmware versions;
- upgrade state, download URL, progress, totals, and failure counters;
- trial boot counter and confirmation state;
- CRC32 over all preceding fields; and
- a commit marker programmed last.

Only records with a valid magic, schema, CRC, and commit marker are considered. At boot, the loader scans the journal and selects the highest valid sequence number. If none exists, it creates defaults.

The state machine is limited to `IDLE`, `DOWNLOADING`, `DOWNLOADED`, `VERIFYING`, `VERIFIED`, `INSTALLING`, `TRIAL`, `CONFIRMED`, and `FAILED`. Each externally visible transition is persisted before the next destructive operation. `TRIAL` contains the pending slot and a bounded boot-attempt counter; the current active slot is not replaced until confirmation.

The app and bootloader share a small, hardware-neutral journal interface. After app self-test, the running app calls `boot_confirm_running_image()`. That function determines its running slot from the vector-table address and appends a confirmed state record. It does not erase or modify application slots.

## Upgrade flow

1. Build the shared app twice and package the image linked for the intended inactive slot. The signed header contains its target-slot identifier.
2. Download the signed package to external flash, persist progress at the existing throttled interval, and validate header, target slot, key hash, body CRC, signature, size, and anti-rollback version.
3. Copy the validated image to the matching inactive internal-flash slot. Persist `INSTALLING` before any erase and record copy progress at a sector boundary so an interrupted installation can be safely restarted from validation rather than assumed complete.
4. Re-read the destination body and cryptographically verify `signed external header + destination body` against the signature block retained in external flash. Only then persist that slot as `pending` and enter `TRIAL`.
5. On the first boot of `TRIAL`, launch the pending slot. The application calls the shared confirmation API only after its self-test succeeds.
6. A confirmed trial makes the pending slot active and clears trial metadata. A reset before confirmation, an invalid pending slot, or exhausted trial attempts clears the pending slot and boots the previously active valid slot.

## Boundaries and failure behavior

- No package, header, target-slot, signature, version, slot-size, or destination-image validation failure may change the active slot.
- The loader never jumps to a slot until its vector table passes sanity checks and its packaged image validation succeeds.
- Flash erase/write errors leave the state in `FAILED` or a restart-safe pre-install state; the loader selects the last valid active slot.
- The design makes firmware authenticity independent of HTTP integrity. HTTP remains vulnerable to availability and downgrade-attempt traffic, but signatures and version policy reject unauthorized images.

## Test strategy

Host-side Python tests will cover package parsing and verification, target-slot binding, tampering, high-S signatures, version comparisons, journal record selection, torn-record recovery, journal-full behavior, transition legality, and slot-selection scenarios. Build checks will verify that each app image's vector table and reset handler fall inside its own slot. Pure C helpers for journal parsing and state decisions will be kept hardware-free where practical so PlatformIO native tests can cover the same rules. Existing-board acceptance tests will demonstrate normal upgrade, interrupted download, interrupted install, invalid signature rejection, unconfirmed trial rollback, and confirmed trial persistence.

## Acceptance criteria

- No two logical configuration copies rely on different offsets in the same STM32 erase sector.
- Every valid persisted state remains readable after simulated truncation of any single journal write.
- The A and B application artifacts are independently linked, and each artifact's vector table and reset handler are inside its own slot.
- The signed target-slot identifier binds every package to its intended inactive slot.
- A package is verified both before copy and after placement into its target slot.
- A new image does not become permanent until the application confirms its trial boot.
- Existing bootloader and app builds pass, and all new host tests pass.
- README documents the new state diagram, the confirmation API, test commands, and residual HTTP limitations.

## Deferred work

Phase 2 adds CI, release artifact hygiene, and broader test automation. Phase 3 adds diagrams, demo evidence, resume text, and interview material. TLS, key revocation, debug-port lifecycle controls, and a migration to MCUboot are out of scope for this iteration.
