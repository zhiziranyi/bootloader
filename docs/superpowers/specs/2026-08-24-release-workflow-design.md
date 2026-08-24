# Release Workflow Design

## Goal

Turn the validated FlashSafe Pro OTA project into a repeatable release workflow: one local command produces matching Slot-A and Slot-B signed packages, validates them, and records immutable package metadata for demonstration and handoff.

## Chosen approach

`tools/release_firmware.py --version X.Y.Z` is the single entry point. It writes `app/include/app_version.h`, builds the existing `app_a` and `app_b` PlatformIO environments, packs each binary for its matching target slot, host-verifies both ECDSA packages, and writes `firmware/release-vX.Y.Z.json` with SHA-256, size, target slot, and package name.

The App banner reads `APP_VERSION_STRING` from the generated header, so UART/OLED evidence matches the signed package version. The tool uses the existing private key, packer, and verifier; it does not rotate keys, upload firmware, or modify Bootloader sources.

## Error handling and safety

The command rejects malformed versions, requires both package verifications to pass, and stops on the first failed build/tool invocation. It only writes generated artifacts under `app/include/` and `firmware/`; no hardware is accessed. The output manifest never includes private-key material.

## Documentation scope

README will be rewritten around the demonstrated workflow: architecture and partition table, exact wiring, safe serial upload behavior, LAN OTA operation, A/B trial/rollback evidence, release command, and concise resume-ready project summary. The existing local HTML workbench remains optional and lists release packages automatically.
