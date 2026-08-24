"""Static contracts for W25Q64 program and erase completion waits."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class W25Q64ContractTests(unittest.TestCase):
    def test_busy_wait_uses_elapsed_time_not_an_arbitrary_spin_count(self):
        source = (ROOT / "src" / "drivers" / "w25q64.c").read_text(encoding="utf-8")

        self.assertIn("static bool W25Q64_WaitBusy(uint32_t timeout_ms)", source)
        self.assertIn("uint32_t start = HAL_GetTick();", source)
        self.assertIn("(HAL_GetTick() - start) >= timeout_ms", source)
        self.assertNotIn("timeout < 100000", source)

    def test_erase_waits_long_enough_before_any_page_program_can_start(self):
        source = (ROOT / "src" / "drivers" / "w25q64.c").read_text(encoding="utf-8")

        self.assertIn("#define W25Q64_BLOCK_ERASE_TIMEOUT_MS", source)
        self.assertIn("W25Q64_WaitBusy(W25Q64_BLOCK_ERASE_TIMEOUT_MS)", source)
        self.assertIn("W25Q64_WaitBusy(W25Q64_PAGE_PROGRAM_TIMEOUT_MS)", source)

    def test_page_program_uses_software_spi_to_avoid_hal_full_duplex_overrun(self):
        source = (ROOT / "src" / "drivers" / "w25q64.c").read_text(encoding="utf-8")
        header = (ROOT / "src" / "drivers" / "w25q64.h").read_text(encoding="utf-8")

        self.assertIn("static bool W25Q64_WriteEnable(void)", source)
        self.assertIn("flash_status & W25Q64_STATUS_WEL", source)
        self.assertIn("static uint8_t W25Q64_TransferByte(uint8_t tx)", source)
        self.assertIn("HAL_SPI_DeInit(&hspi3)", source)
        self.assertNotIn("HAL_SPI_Transmit(", source)
        self.assertNotIn("HAL_SPI_Receive(", source)
        self.assertIn("HAL_StatusTypeDef W25Q64_Write(", header)
        self.assertIn("HAL_StatusTypeDef W25Q64_EraseBlock64K(", header)

    def test_spi3_uses_a_conservative_clock_for_external_flash_over_jumpers(self):
        source = (ROOT / "src" / "main.c").read_text(encoding="utf-8")

        self.assertIn("hspi3.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;", source)


if __name__ == "__main__":
    unittest.main()
