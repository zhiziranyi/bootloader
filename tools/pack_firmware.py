#!/usr/bin/env python3
"""
FlashSafe Pro - Firmware Packer

Packs a firmware binary with the FlashSafe header and an ECDSA P-256
signature. The on-device verification algorithm is:

  header_crc   = crc32(header[0:56])
  image_crc    = crc32(firmware_body)
  pubkey_hash  = sha256(public_key_x || public_key_y)
  digest       = sha256(header || firmware_body)
  signature    = ECDSA-P256(digest), raw r || s (64 bytes)

Package layout:
  [fw_header_t 60B] [firmware body] [signature_block 72B]
"""

import os
import sys
import struct
import argparse
import hashlib
import zlib
import re

# Firmware header magic
FW_MAGIC = 0x464C4153

# sizeof(fw_header_t) - MUST match the C struct in src/partition.h
# magic(4) + ver_major(2) + ver_minor(2) + ver_patch(2) + reserved(2)
# + image_size(4) + image_crc(4) + pubkey_hash(32) + signature_offset(4)
# + header_crc(4) = 60 bytes
HEADER_SIZE = 60

# sizeof(signature_block_t): sig_len(4) + signature(64) + sig_crc(4)
SIG_BLOCK_SIZE = 72

TARGET_SLOT_A = 0
TARGET_SLOT_B = 1


def target_slot_from_name(value):
    normalized = value.upper()
    if normalized == 'A':
        return TARGET_SLOT_A
    if normalized == 'B':
        return TARGET_SLOT_B
    raise ValueError("target slot must be A or B")


def compute_crc32(data):
    """Compute CRC32 of data (matches zlib.crc32 / C CRC32_Calculate)."""
    return zlib.crc32(data) & 0xFFFFFFFF


def parse_public_key_header(pubkey_path):
    """Parse public key from C header file."""
    with open(pubkey_path, 'r', encoding='utf-8') as f:
        content = f.read()

    match = re.search(r'PUBLIC_KEY\[64\]\s*=\s*\{([^}]+)\}', content)
    if not match:
        raise ValueError("Could not find PUBLIC_KEY in header file")

    hex_values = re.findall(r'0x([0-9A-Fa-f]{2})', match.group(1))
    if len(hex_values) != 64:
        raise ValueError(f"Expected 64 bytes in PUBLIC_KEY, found {len(hex_values)}")

    return bytes(int(h, 16) for h in hex_values)


def read_private_key(key_path):
    """Read private key from PEM file, return DER bytes."""
    with open(key_path, 'r', encoding='utf-8') as f:
        content = f.read()

    lines = content.strip().split('\n')
    b64_lines = [line for line in lines if not line.startswith('-----')]
    b64_data = ''.join(b64_lines)

    import base64
    return base64.b64decode(b64_data)


def create_firmware_header(version, image_size, pubkey_bytes, signature_offset,
                           target_slot):
    """Create the 60-byte firmware header (without signing)."""
    ver_parts = version.split('.')
    if len(ver_parts) != 3:
        raise ValueError("Version must be in format X.Y.Z")

    ver_major = int(ver_parts[0])
    ver_minor = int(ver_parts[1])
    ver_patch = int(ver_parts[2])
    if target_slot not in (TARGET_SLOT_A, TARGET_SLOT_B):
        raise ValueError("target slot must be A (0) or B (1)")

    if ver_major > 65535 or ver_minor > 65535 or ver_patch > 65535:
        raise ValueError("Version components must fit in 16 bits")

    pubkey_hash = hashlib.sha256(pubkey_bytes).digest()

    # Everything except the trailing header_crc
    header_no_crc = struct.pack('<IHHHHII32sI',
                                FW_MAGIC,
                                ver_major, ver_minor, ver_patch, target_slot,
                                image_size,
                                0,              # image_crc (filled below)
                                pubkey_hash,
                                signature_offset)

    # image_crc is computed on the body only
    # (image_crc placeholder is handled by the caller before packing)
    return header_no_crc


def create_signature_block(firmware_data, private_key_der):
    """ECDSA-P256 sign sha256(header+body), return (sig_block, signature).

    The signature is canonicalized to low-S (s <= n/2) because the
    bootloader rejects high-S signatures (anti-malleability). The
    python-ecdsa default does NOT guarantee low-S, so we enforce it here.
    """
    try:
        from ecdsa import SigningKey, NIST256p
    except ImportError:
        print("Error: ecdsa library not installed. Install with: pip install ecdsa")
        sys.exit(1)

    sk = SigningKey.from_der(private_key_der)

    def sigencode_low_s(r, s, order):
        if s > order // 2:
            s = order - s
        return r.to_bytes(32, 'big') + s.to_bytes(32, 'big')

    # RFC 6979 deterministic signature, canonical raw r || s (64 bytes)
    signature = sk.sign_digest(
        hashlib.sha256(firmware_data).digest(),
        sigencode=sigencode_low_s)

    sig_len = len(signature)
    if sig_len != 64:
        raise ValueError(f"Unexpected signature length: {sig_len}")

    sig_block = struct.pack('<I', sig_len)
    sig_block += signature
    sig_block += struct.pack('<I', compute_crc32(sig_block))

    return sig_block, signature


def pack_firmware(input_path, key_path, pubkey_path, version, output_path,
                  target_slot):
    """Pack firmware with header and signature."""
    with open(input_path, 'rb') as f:
        firmware_body = f.read()

    image_size = len(firmware_body)
    print(f"Firmware size: {image_size} bytes")

    pubkey_bytes = parse_public_key_header(pubkey_path)
    print(f"Public key loaded: {len(pubkey_bytes)} bytes")

    private_key_der = read_private_key(key_path)
    print("Private key loaded")

    image_crc = compute_crc32(firmware_body)
    signature_offset = HEADER_SIZE + image_size

    header_no_crc = create_firmware_header(version, image_size, pubkey_bytes,
                                           signature_offset, target_slot)

    # Fill image_crc at offset 16 (after magic + 4 version fields)
    header_no_crc = (header_no_crc[:16] +
                     struct.pack('<I', image_crc) +
                     header_no_crc[20:])

    header_crc = compute_crc32(header_no_crc)
    header = header_no_crc + struct.pack('<I', header_crc)

    if len(header) != HEADER_SIZE:
        raise ValueError(f"Header size mismatch: expected {HEADER_SIZE}, "
                         f"got {len(header)}")

    # Sign header + body
    firmware_data = header + firmware_body
    sig_block, signature = create_signature_block(firmware_data, private_key_der)
    print(f"Signature block created: {len(sig_block)} bytes")
    print(f"Signature length: {len(signature)} bytes")

    pkg = header + firmware_body + sig_block

    with open(output_path, 'wb') as f:
        f.write(pkg)

    total_size = len(pkg)
    print(f"\nFirmware package created: {output_path}")
    print(f"Total size: {total_size} bytes")
    print(f"  Header: {len(header)} bytes")
    print(f"  Body: {len(firmware_body)} bytes")
    print(f"  Signature block: {len(sig_block)} bytes")
    print(f"  Header CRC: 0x{header_crc:08X}")
    print(f"  Image CRC:  0x{image_crc:08X}")
    print(f"  Target slot: {'A' if target_slot == TARGET_SLOT_A else 'B'}")
    print(f"  Pubkey SHA-256: {hashlib.sha256(pubkey_bytes).hexdigest()}")
    return True


def main():
    parser = argparse.ArgumentParser(
        description="Pack firmware for FlashSafe Pro bootloader")
    parser.add_argument("--input", "-i", required=True,
                        help="Input firmware binary file (.bin)")
    parser.add_argument("--key", "-k", required=True,
                        help="Private key file (PEM format)")
    parser.add_argument("--pubkey", "-p", required=True,
                        help="Public key header file (public_key.h)")
    parser.add_argument("--version", "-v", required=True,
                        help="Firmware version (format: X.Y.Z)")
    parser.add_argument("--output", "-o", required=True,
                        help="Output firmware package file (.pkg)")
    parser.add_argument("--target-slot", required=True, choices=("A", "B", "a", "b"),
                        help="Slot the application binary was linked for")

    args = parser.parse_args()

    for path in (args.input, args.key, args.pubkey):
        if not os.path.exists(path):
            print(f"Error: File not found: {path}")
            sys.exit(1)

    if not pack_firmware(args.input, args.key, args.pubkey,
                         args.version, args.output,
                         target_slot_from_name(args.target_slot)):
        sys.exit(1)

    print("\nFirmware packing complete!")


if __name__ == "__main__":
    main()
