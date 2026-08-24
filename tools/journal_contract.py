"""Reference model for the on-device append-only configuration journal."""

from dataclasses import dataclass
import zlib


RECORD_MAGIC = 0x4A4E4C31  # JNL1
SCHEMA_VERSION = 1
COMMIT_ERASED = 0xFFFFFFFF
COMMIT_VALID = 0x00000000


@dataclass(frozen=True)
class Record:
    sequence: int
    active_slot: int
    crc: int
    commit: int

    def payload(self):
        return (RECORD_MAGIC.to_bytes(4, "little") +
                SCHEMA_VERSION.to_bytes(4, "little") +
                self.sequence.to_bytes(4, "little") +
                self.active_slot.to_bytes(4, "little"))


def make_record(sequence, active_slot, commit=True, crc_ok=True):
    seed = Record(sequence, active_slot, 0, COMMIT_VALID if commit else COMMIT_ERASED)
    crc = zlib.crc32(seed.payload()) & 0xFFFFFFFF
    if not crc_ok:
        crc ^= 0xFFFFFFFF
    return Record(sequence, active_slot, crc, seed.commit)


def is_valid(record):
    return (record.commit == COMMIT_VALID and
            record.active_slot in (0, 1) and
            record.crc == (zlib.crc32(record.payload()) & 0xFFFFFFFF))


def select_latest(records):
    valid = [record for record in records if is_valid(record)]
    return max(valid, key=lambda record: record.sequence) if valid else None


@dataclass(frozen=True)
class BootState:
    active_slot: int
    pending_slot: int
    trial_boot_count: int
    confirmed: bool = False


def select_boot_slot(state):
    if state.pending_slot in (0, 1) and state.trial_boot_count == 0:
        return state.pending_slot, BootState(state.active_slot, state.pending_slot, 1)
    return state.active_slot, state


def confirm_trial(state, running_slot):
    if running_slot != state.pending_slot:
        return state
    return BootState(running_slot, -1, 0, confirmed=True)
