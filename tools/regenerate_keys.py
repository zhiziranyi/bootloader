#!/usr/bin/env python3
"""
FlashSafe Pro - One-click key rotation + A/B release rebuild

Automates the production key rotation flow:
  1. generate a brand new secp256r1 key pair (tools/keys/)
  2. back up the old signing private key and include/public_key.h
  3. install the new public key into the bootloader source
  4. rebuild the bootloader with the replacement public key
  5. build, sign, and verify matching Slot-A and Slot-B release packages
  6. print a summary; bootloader flashing is done manually via UART ROM mode

Usage:
  python tools/regenerate_keys.py [--version X.Y.Z] [--yes]

Examples:
  python tools/regenerate_keys.py --version 1.1.0          # keys + bootloader + A/B release
"""

import os
import sys
import shutil
import subprocess
import argparse
import datetime
import hashlib
import re
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
TOOLS_DIR = PROJECT_ROOT / "tools"
KEYS_DIR = TOOLS_DIR / "keys"
INCLUDE_DIR = PROJECT_ROOT / "include"
PUBKEY_HEADER = INCLUDE_DIR / "public_key.h"


def log(msg):
    print(f"[rotate] {msg}")


def run(cmd, cwd=None):
    """Run a command and propagate failures."""
    log(" ".join(str(c) for c in cmd))
    try:
        subprocess.run([str(c) for c in cmd], cwd=str(cwd or PROJECT_ROOT),
                       check=True)
    except subprocess.CalledProcessError as e:
        print(f"\n[ERROR] Command failed with exit code {e.returncode}:\n"
              f"  {' '.join(str(c) for c in cmd)}", file=sys.stderr)
        sys.exit(1)


def find_pio():
    """Locate the PlatformIO executable."""
    for name in ("platformio", "pio"):
        exe = shutil.which(name)
        if exe:
            return exe
    candidates = [
        Path.home() / ".platformio" / "penv" / "Scripts" / "platformio.exe",
        Path.home() / ".platformio" / "penv" / "Scripts" / "pio.exe",
    ]
    for c in candidates:
        if c.exists():
            return str(c)
    return None


def ask_yes_no(question, default_no=True):
    suffix = "[y/N]" if default_no else "[Y/n]"
    answer = input(f"{question} {suffix} ").strip().lower()
    if not answer:
        return not default_no
    return answer in ("y", "yes")


def pubkey_hash_from_header(path):
    """Extract PUBLIC_KEY from the C header and return its SHA-256 hex."""
    content = Path(path).read_text(encoding="utf-8")
    match = re.search(r'PUBLIC_KEY\[64\]\s*=\s*\{([^}]+)\}', content)
    if not match:
        return None
    values = re.findall(r'0x([0-9A-Fa-f]{2})', match.group(1))
    if len(values) != 64:
        return None
    return hashlib.sha256(bytes(int(v, 16) for v in values)).hexdigest()


def main():
    parser = argparse.ArgumentParser(
        description="One-click key rotation for FlashSafe Pro")
    parser.add_argument("--yes", action="store_true",
                        help="Answer yes to all prompts (no questions)")
    parser.add_argument("--version", "-v", default="1.0.0",
                        help="version for the replacement A/B release (X.Y.Z)")
    parser.add_argument("--env", default="black_f407zg",
                        help="PlatformIO environment name (default: black_f407zg)")
    args = parser.parse_args()

    pio = find_pio()
    if pio is None:
        print("[ERROR] PlatformIO not found in PATH or ~/.platformio/penv",
              file=sys.stderr)
        sys.exit(1)
    log(f"PlatformIO: {pio}")

    if not KEYS_DIR.exists():
        KEYS_DIR.mkdir(parents=True)

    print()
    print("=" * 64)
    print("  FlashSafe Pro - Key Rotation")
    print("=" * 64)
    print()
    print("WARNING: this replaces the production signing key.")
    print("  - The OLD public key is backed up, but old bootloaders will")
    print("    reject firmware signed with the NEW key.")
    print("  - You MUST flash the new bootloader through UART ROM mode")
    print("    before installing the replacement A/B packages.")
    print()

    if not args.yes and not ask_yes_no("Continue with key rotation?"):
        print("Aborted.")
        sys.exit(0)

    # 1. Back up both halves of the old trust chain before generation replaces
    # the key files.  The private backup remains local-only and is never used
    # by the production flow automatically.
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    backup_dir = KEYS_DIR / "backups"
    backup_dir.mkdir(exist_ok=True)
    old_private = KEYS_DIR / "private_key.pem"
    if old_private.exists():
        private_backup = backup_dir / f"private_key.pem.bak-{stamp}"
        shutil.copy2(old_private, private_backup)
        log(f"Old private key backed up to: {private_backup}")
    if PUBKEY_HEADER.exists():
        public_backup = backup_dir / f"public_key.h.bak-{stamp}"
        shutil.copy2(PUBKEY_HEADER, public_backup)
        log(f"Old public key backed up to: {public_backup}")

    # 2. Generate a new key pair
    log("Generating new secp256r1 key pair...")
    run([sys.executable, TOOLS_DIR / "gen_keypair.py",
         "-o", KEYS_DIR])

    # 3. Install the new public key into the bootloader source
    new_header = KEYS_DIR / "public_key.h"
    if not new_header.exists():
        print(f"[ERROR] {new_header} was not generated", file=sys.stderr)
        sys.exit(1)
    shutil.copy2(new_header, PUBKEY_HEADER)
    log(f"Installed new public key: {PUBKEY_HEADER}")

    # 4. Rebuild the bootloader that embeds the replacement trust anchor.
    log("Rebuilding bootloader...")
    run([pio, "run", "-d", PROJECT_ROOT, "-e", args.env])

    # 5. Reuse the normal release workflow so the generated packages retain
    # the Slot-A/Slot-B address contract used by the OTA installer.
    log(f"Building, signing, and verifying A/B release v{args.version}...")
    run([sys.executable, TOOLS_DIR / "release_firmware.py",
         "--version", args.version])

    # Summary
    print()
    print("=" * 64)
    print("  Key rotation complete")
    print("=" * 64)
    print(f"  New private key : {KEYS_DIR / 'private_key.pem'}")
    print(f"  New public key  : {PUBKEY_HEADER}")
    print(f"  New packages    : firmware_v{args.version}_slotA.pkg / slotB.pkg")
    new_hash = pubkey_hash_from_header(PUBKEY_HEADER)
    if new_hash:
        print(f"  New key SHA-256 : {new_hash}")
    print("  Next: use the desktop app's UART flash target 'bootloader'")
    print("        with BOOT0=3.3V, then OTA the matching inactive-slot package.")
    print("        Do NOT ship tools/keys/private_key.pem.")
    print()


if __name__ == "__main__":
    main()
