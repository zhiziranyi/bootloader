"""Regression test for entering the bootloader CLI during its start window."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class CliEntryWindowTests(unittest.TestCase):
    def test_received_uart_byte_is_checked_before_cli_process_consumes_it(self):
        main = (ROOT / "src" / "main.c").read_text(encoding="utf-8")
        window_start = main.index("while ((HAL_GetTick() - start) < CLI_TIMEOUT_MS)")
        window_end = main.index("    if (cli_mode)", window_start)
        window = main[window_start:window_end]

        self.assertIn("if (cli_has_input())", window)
        self.assertNotIn("cli_process();", window)

    def test_cli_entry_keeps_a_command_sent_during_the_start_window(self):
        main = (ROOT / "src" / "main.c").read_text(encoding="utf-8")
        header = (ROOT / "src" / "cli.h").read_text(encoding="utf-8")
        source = (ROOT / "src" / "cli.c").read_text(encoding="utf-8")

        cli_entry = main[main.index("if (cli_mode)"):main.index("/* Upgrade state machine */")]
        self.assertNotIn("cli_discard_pending_input();", cli_entry)
        self.assertIn("line_len == 1 && line_buf[0] == 'x'", source)
        self.assertNotIn("cli_discard_pending_input", header)

    def test_usart1_receive_interrupt_is_enabled_for_cli_input(self):
        main = (ROOT / "src" / "main.c").read_text(encoding="utf-8")
        handlers = (ROOT / "src" / "stm32f4xx_it.c").read_text(encoding="utf-8")

        self.assertIn("HAL_NVIC_SetPriority(USART1_IRQn", main)
        self.assertIn("HAL_NVIC_EnableIRQ(USART1_IRQn);", main)
        self.assertIn("void USART1_IRQHandler(void)", handlers)
        self.assertIn("HAL_UART_IRQHandler(&huart1);", handlers)


if __name__ == "__main__":
    unittest.main()
