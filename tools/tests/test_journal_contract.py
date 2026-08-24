import pathlib
import sys
import unittest


TOOLS_DIR = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIR))

import journal_contract


class JournalContractTests(unittest.TestCase):
    def test_newest_complete_crc_valid_record_wins(self):
        records = [
            journal_contract.make_record(1, active_slot=0),
            journal_contract.make_record(2, active_slot=1),
        ]

        self.assertEqual(journal_contract.select_latest(records).sequence, 2)

    def test_torn_last_record_does_not_replace_previous_record(self):
        records = [
            journal_contract.make_record(4, active_slot=0),
            journal_contract.make_record(5, active_slot=1, commit=False),
        ]

        self.assertEqual(journal_contract.select_latest(records).sequence, 4)

    def test_crc_corruption_does_not_replace_previous_record(self):
        records = [
            journal_contract.make_record(7, active_slot=0),
            journal_contract.make_record(8, active_slot=1, crc_ok=False),
        ]

        self.assertEqual(journal_contract.select_latest(records).sequence, 7)

    def test_trial_boots_pending_slot_once_then_reverts(self):
        state = journal_contract.BootState(active_slot=0, pending_slot=1,
                                           trial_boot_count=0)
        first_slot, next_state = journal_contract.select_boot_slot(state)
        self.assertEqual(first_slot, 1)
        second_slot, _ = journal_contract.select_boot_slot(next_state)
        self.assertEqual(second_slot, 0)

    def test_confirmation_only_promotes_the_pending_slot(self):
        state = journal_contract.BootState(active_slot=0, pending_slot=1,
                                           trial_boot_count=1)
        self.assertFalse(journal_contract.confirm_trial(state, 0).confirmed)
        promoted = journal_contract.confirm_trial(state, 1)
        self.assertTrue(promoted.confirmed)
        self.assertEqual(promoted.active_slot, 1)


if __name__ == "__main__":
    unittest.main()
