# Sector-Aligned Serial Upload Design

## Goal

Make Bootloader, App_A, App_B, and configuration storage independently erasable
on STM32F407ZGT6, and expose the Bootloader and App projects in one VS Code
workspace.

## Flash layout

The layout follows the STM32F407 physical erase-sector boundaries:

| Partition | Address range | Sectors | Size |
|---|---|---|---|
| Bootloader | `0x08000000-0x0801FFFF` | 0-4 | 128 KiB |
| App_A | `0x08020000-0x0807FFFF` | 5-7 | 384 KiB |
| App_B | `0x08080000-0x080DFFFF` | 8-10 | 384 KiB |
| Config journal | `0x080E0000-0x080FFFFF` | 11 | 128 KiB |

Every serial upload supplies `stm32flash -S address:length`, so it erases only
the selected partition. App linker addresses, runtime partition constants, and
upload addresses must remain identical.

## VS Code workspace

`bootloader.code-workspace` contains the repository root as `Bootloader` and
the `app` subdirectory as `App`. PlatformIO therefore discovers the root
Bootloader environment and the independent App_A/App_B environments without
merging their source trees.

## Verification

Automated tests enforce sector alignment, non-overlap, bounded serial upload
ranges, workspace discovery, and application vector placement. All three
firmware environments must build successfully.
