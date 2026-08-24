"""Static contracts for STM32F407 RMII MAC initialization."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class EthernetInitContractTests(unittest.TestCase):
    def test_eth_msp_enables_mac_clocks_before_hal_initialization(self):
        source = (ROOT / "network" / "ethernetif.c").read_text(encoding="utf-8")

        self.assertIn("void HAL_ETH_MspInit(ETH_HandleTypeDef *heth)", source)
        self.assertIn("__HAL_RCC_ETH_CLK_ENABLE();", source)

    def test_hal_errors_are_reported_and_stop_netif_creation(self):
        source = (ROOT / "network" / "ethernetif.c").read_text(encoding="utf-8")

        self.assertIn("if (HAL_ETH_Init(&heth) != HAL_OK)", source)
        self.assertIn('printf("[ETH] HAL init failed', source)
        self.assertIn("if (HAL_ETH_Start(&heth) != HAL_OK)", source)
        self.assertIn('printf("[ETH] MAC start failed', source)
        self.assertIn("return ERR_IF;", source)

    def test_rx_callbacks_return_distinct_dma_buffers_and_link_received_data(self):
        source = (ROOT / "network" / "ethernetif.c").read_text(encoding="utf-8")

        self.assertIn("static uint32_t rx_alloc_index", source)
        self.assertIn("Rx_Buff[rx_alloc_index]", source)
        self.assertIn("rx_alloc_index = (rx_alloc_index + 1U) % ETH_RX_DESC_CNT", source)
        self.assertIn("if (*pStart == NULL)", source)
        self.assertIn("*pStart = buff;", source)
        self.assertIn("*pEnd = buff;", source)

    def test_rx_callbacks_override_the_hal_weak_symbols_when_registration_is_disabled(self):
        source = (ROOT / "network" / "ethernetif.c").read_text(encoding="utf-8")

        self.assertIn("void HAL_ETH_RxAllocateCallback(uint8_t **buff)", source)
        self.assertIn("void HAL_ETH_RxLinkCallback(void **pStart, void **pEnd", source)
        self.assertIn("void HAL_ETH_TxFreeCallback(uint32_t *buff)", source)
        self.assertNotIn("HAL_ETH_RegisterRxAllocateCallback", source)
        self.assertNotIn("HAL_ETH_RegisterRxLinkCallback", source)
        self.assertNotIn("HAL_ETH_RegisterTxFreeCallback", source)

    def test_dhcp_path_reports_phy_link_and_tx_result(self):
        source = (ROOT / "network" / "ethernetif.c").read_text(encoding="utf-8")

        self.assertIn('printf("[ETH] PHY link', source)
        self.assertIn('printf("[ETH] PHY read failed', source)
        self.assertIn('printf("[ETH] PHY scan found no response', source)
        self.assertIn("bsr == 0xFFFFU", source)
        self.assertIn('printf("[ETH] TX queued', source)
        self.assertIn('printf("[ETH] TX failed', source)
        self.assertIn("heth.DMAErrorCode", source)
        self.assertIn("heth.Instance->DMASR", source)

    def test_tx_path_uses_a_bounded_timeout_so_dhcp_cannot_deadlock(self):
        source = (ROOT / "network" / "ethernetif.c").read_text(encoding="utf-8")

        self.assertIn("#define ETH_TX_TIMEOUT_MS", source)
        self.assertIn("HAL_ETH_Transmit(&heth, &txConfig, ETH_TX_TIMEOUT_MS)", source)
        self.assertNotIn("HAL_ETH_Transmit(&heth, &txConfig, HAL_MAX_DELAY)", source)

    def test_tx_uses_lwip_software_checksums_without_mac_overwrite(self):
        ethernetif = (ROOT / "network" / "ethernetif.c").read_text(encoding="utf-8")
        lwipopts = (ROOT / "include" / "lwipopts.h").read_text(encoding="utf-8")

        self.assertIn("#define CHECKSUM_GEN_IP 1", lwipopts)
        self.assertIn("#define CHECKSUM_GEN_UDP 1", lwipopts)
        self.assertIn("txConfig.ChecksumCtrl = ETH_CHECKSUM_DISABLE;", ethernetif)
        self.assertNotIn("txConfig.ChecksumCtrl = ETH_CHECKSUM_BY_HARDWARE;", ethernetif)

    def test_lwip_heap_aligns_dhcp_allocations_for_cortex_m_doubleword_access(self):
        lwipopts = (ROOT / "lib" / "lwip" / "src" / "include" / "lwip" / "lwipopts.h").read_text(encoding="utf-8")

        self.assertIn("#define MEM_ALIGNMENT 8", lwipopts)

    def test_phy_address_is_scanned_when_the_default_address_does_not_reply(self):
        source = (ROOT / "network" / "ethernetif.c").read_text(encoding="utf-8")

        self.assertIn("static uint32_t phy_address", source)
        self.assertIn("for (uint32_t address = 0U; address < 32U; address++)", source)
        self.assertIn('printf("[ETH] PHY found at addr %lu', source)
        self.assertIn('printf("[ETH] PHY scan found no response', source)
        self.assertIn("HAL_ETH_ReadPHYRegister(&heth, phy_address", source)

    def test_dma_fault_log_includes_the_active_rx_descriptor_and_list_base(self):
        source = (ROOT / "network" / "ethernetif.c").read_text(encoding="utf-8")

        self.assertIn("static void eth_report_dma_fault(void)", source)
        self.assertIn("heth.Instance->DMARDLAR", source)
        self.assertIn("heth.Instance->DMACHRDR", source)
        self.assertIn('printf("[ETH] RX desc base=', source)
        self.assertIn("DMARxDscrTab[0].DESC0", source)

    def test_hardfault_reports_a_bounded_uart_marker(self):
        source = (ROOT / "src" / "stm32f4xx_it.c").read_text(encoding="utf-8")

        self.assertIn('"\\r\\n[FAULT] HardFault PC=0x%08lX LR=0x%08lX "', source)
        self.assertIn("HAL_UART_Transmit(&huart1", source)
        self.assertNotIn("while(1);\n}\n\n/* ETH interrupt", source)

    def test_hardfault_decodes_stacked_pc_and_fault_status_registers(self):
        source = (ROOT / "src" / "stm32f4xx_it.c").read_text(encoding="utf-8")

        self.assertIn("void HardFault_Decode(uint32_t *stack)", source)
        self.assertIn("stack[6]", source)
        self.assertIn("SCB->CFSR", source)
        self.assertIn("SCB->HFSR", source)
        self.assertIn("SCB->BFAR", source)
        self.assertIn("HardFault_Decode", source)


if __name__ == "__main__":
    unittest.main()
