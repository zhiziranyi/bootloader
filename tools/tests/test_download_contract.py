"""Static contracts for OTA download integrity diagnostics."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class DownloadContractTests(unittest.TestCase):
    def test_first_chunk_reports_received_and_flash_header_crc(self):
        source = (ROOT / "network" / "download.c").read_text(encoding="utf-8")

        self.assertIn("static void download_report_header_crc", source)
        self.assertIn('download_report_header_crc("RX", data, len);', source)
        self.assertIn('download_report_header_crc("FLASH", header, sizeof(header));', source)
        self.assertIn("sizeof(fw_header_t) - sizeof(uint32_t)", source)

    def test_header_diagnostic_dumps_exact_bytes_at_both_boundaries(self):
        source = (ROOT / "network" / "download.c").read_text(encoding="utf-8")

        self.assertIn('printf("[DL] %s %02lu:', source)
        self.assertIn('printf(" %02X", data[offset + i]);', source)
        self.assertIn("for (uint32_t offset = 0; offset < sizeof(fw_header_t); offset += 16U)", source)


if __name__ == "__main__":
    unittest.main()
