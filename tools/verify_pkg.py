#!/usr/bin/env python3
"""
FlashSafe Pro - Firmware Package Verifier

Re-implements the bootloader's verification algorithm in Python so the
packed .pkg can be validated on the host before deploying:

  1. header_crc  == crc32(header[0:56])
  2. image_crc   == crc32(body)
  3. pubkey_hash == sha256(embedded public key)
  4. ECDSA-P256  == verify(sha256(header+body), signature)

Usage:
  python tools/verify_pkg.py --pkg firmware/firmware_v1.0.0.pkg \
      --pubkey include/public_key.h

Exit code 0 = package valid, 1 = invalid.
"""

import os
import sys
import re
import struct
import argparse
import hashlib
import zlib

FW_MAGIC = 0x464C4153
HEADER_SIZE = 60
SIG_BLOCK_SIZE = 72
TARGET_SLOT_A = 0
TARGET_SLOT_B = 1


def is_valid_target_slot(value):
    return value in (TARGET_SLOT_A, TARGET_SLOT_B)


def target_slot_from_name(value):
    normalized = value.upper()
    if normalized == 'A':
        return TARGET_SLOT_A
    if normalized == 'B':
        return TARGET_SLOT_B
    raise ValueError("target slot must be A or B")


def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def parse_public_key(pubkey_path):
    with open(pubkey_path, 'r', encoding='utf-8') as f:
        content = f.read()
    match = re.search(r'PUBLIC_KEY\[64\]\s*=\s*\{([^}]+)\}', content)
    if not match:
        raise ValueError("PUBLIC_KEY not found")
    values = re.findall(r'0x([0-9A-Fa-f]{2})', match.group(1))
    if len(values) != 64:
        raise ValueError(f"Expected 64 bytes, got {len(values)}")
    return bytes(int(v, 16) for v in values)


def verify_pkg(pkg_path, pubkey_path, expected_slot=None):
    with open(pkg_path, 'rb') as f:
        data = f.read()

    if len(data) < HEADER_SIZE + SIG_BLOCK_SIZE:
        print(f"FAIL: package too small ({len(data)} bytes)")
        return False

    magic, v_major, v_minor, v_patch, target_slot, image_size, image_crc, \
        pubkey_hash, sig_offset, header_crc = struct.unpack_from(
            '<IHHHHII32sII', data, 0)

    if magic != FW_MAGIC:
        print(f"FAIL: bad magic 0x{magic:08X}")
        return False

    if not is_valid_target_slot(target_slot):
        print(f"FAIL: invalid target slot {target_slot}")
        return False

    if expected_slot is not None and target_slot != expected_slot:
        print(f"FAIL: package targets slot {target_slot}, expected {expected_slot}")
        return False

    header = data[:HEADER_SIZE]
    if crc32(header[:HEADER_SIZE - 4]) != header_crc:
        print("FAIL: header CRC mismatch")
        return False

    body = data[HEADER_SIZE:HEADER_SIZE + image_size]
    if len(body) != image_size:
        print(f"FAIL: truncated body ({len(body)} != {image_size})")
        return False

    if crc32(body) != image_crc:
        print("FAIL: image CRC mismatch")
        return False

    pubkey = parse_public_key(pubkey_path)
    if hashlib.sha256(pubkey).digest() != pubkey_hash:
        print("FAIL: pubkey hash does not match embedded key")
        return False

    if sig_offset != HEADER_SIZE + image_size:
        print(f"FAIL: unexpected signature offset {sig_offset}")
        return False

    sig_len = struct.unpack_from('<I', data, sig_offset)[0]
    if sig_len != 64:
        print(f"FAIL: unexpected signature length {sig_len}")
        return False

    sig = data[sig_offset + 4:sig_offset + 4 + 64]
    sig_block = data[sig_offset:sig_offset + SIG_BLOCK_SIZE - 4]
    if crc32(sig_block) != struct.unpack_from('<I', data, sig_offset + 68)[0]:
        print("FAIL: signature block CRC mismatch")
        return False

    try:
        from ecdsa import VerifyingKey, NIST256p, BadSignatureError
    except ImportError:
        print("Error: ecdsa library not installed")
        sys.exit(1)

    r = int.from_bytes(sig[:32], 'big')
    s = int.from_bytes(sig[32:], 'big')

    # Low-S check (mirrors the bootloader hardening)
    order = NIST256p.order
    if r <= 0 or s <= 0 or s > order // 2:
        print("FAIL: signature scalar out of range or high-S")
        return False

    vk = VerifyingKey.from_string(pubkey, curve=NIST256p)
    digest = hashlib.sha256(header + body).digest()
    try:
        ok = vk.verify_digest(
            sig, digest,
            sigdecode=lambda raw, ordr: (
                int.from_bytes(raw[:32], 'big'),
                int.from_bytes(raw[32:], 'big')))
    except BadSignatureError:
        ok = False

    if not ok:
        print("FAIL: ECDSA signature invalid")
        return False

    print(f"OK: v{v_major}.{v_minor}.{v_patch}, target={'A' if target_slot == 0 else 'B'}, "
          f"image={image_size} bytes, signature valid (low-S)")
    return True


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Verify FlashSafe .pkg")
    parser.add_argument("--pkg", required=True, help="Firmware package (.pkg)")
    parser.add_argument("--pubkey", required=True,
                        help="Bootloader public key header (public_key.h)")
    parser.add_argument("--expected-slot", choices=("A", "B", "a", "b"),
                        help="Require the package to target this slot")
    args = parser.parse_args()

    expected_slot = (target_slot_from_name(args.expected_slot)
                     if args.expected_slot else None)
    if verify_pkg(args.pkg, args.pubkey, expected_slot):
        print(f"OK: {args.pkg} is a valid FlashSafe firmware package")
        sys.exit(0)
    print(f"INVALID: {args.pkg}")
    sys.exit(1)
