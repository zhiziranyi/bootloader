import pathlib
import struct
import sys
import unittest


TOOLS_DIR = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIR))

import pack_firmware
import verify_pkg


class PackageContractTests(unittest.TestCase):
    def test_packer_places_target_slot_in_signed_header(self):
        header_without_crc = pack_firmware.create_firmware_header(
            version="1.2.3",
            image_size=3,
            pubkey_bytes=b"\x01" * 64,
            signature_offset=63,
            target_slot=1,
        )

        self.assertEqual(
            struct.unpack_from("<H", header_without_crc, 10)[0], 1
        )

    def test_verifier_rejects_unknown_target_slot(self):
        self.assertTrue(verify_pkg.is_valid_target_slot(0))
        self.assertTrue(verify_pkg.is_valid_target_slot(1))
        self.assertFalse(verify_pkg.is_valid_target_slot(2))


if __name__ == "__main__":
    unittest.main()
