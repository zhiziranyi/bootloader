"""Guard the Cortex-M interrupt state across a bootloader-to-app handoff."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class AppHandoffTests(unittest.TestCase):
    def test_global_interrupts_are_reenabled_before_calling_app_reset_handler(self):
        source = (ROOT / "src" / "jump.c").read_text(encoding="utf-8")
        vtor = source.index("SCB->VTOR = app_addr;")
        handoff = source[vtor : source.index("reset_handler();", vtor)]

        self.assertIn("__enable_irq();", handoff)


if __name__ == "__main__":
    unittest.main()
