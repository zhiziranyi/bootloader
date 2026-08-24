"""Validate that an STM32 application binary is linked for its intended slot."""
import pathlib
import struct


def check_slot(path, slot_base, slot_size):
    data = pathlib.Path(path).read_bytes()
    if len(data) < 8:
        return False
    stack_pointer, reset_handler = struct.unpack_from("<II", data)
    reset_address = reset_handler & ~1
    # The initial stack pointer is the exclusive top of 128 KB SRAM.
    return (0x20000000 < stack_pointer <= 0x20020000 and
            (reset_handler & 1) == 1 and
            slot_base <= reset_address < slot_base + slot_size)
