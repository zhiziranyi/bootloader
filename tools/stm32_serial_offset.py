"""Restrict stm32flash writes and erases to one sector-aligned partition."""

Import("env")


slot_address = str(env.BoardConfig().get("upload.offset_address", "")).lower()
slot_length = str(env.GetProjectOption("custom_upload_length", "")).lower()
valid_upload_ranges = {
    ("0x08000000", "131072"),  # Bootloader: sectors 0-4
    ("0x08020000", "393216"),  # App_A: sectors 5-7
    ("0x08080000", "393216"),  # App_B: sectors 8-10
}

if (slot_address, slot_length) not in valid_upload_ranges:
    raise ValueError(
        "Refusing serial upload to unsafe range: "
        + slot_address
        + ":"
        + slot_length
    )

# PlatformIO uses upload.offset_address only for stm32flash -g.  A bare -S
# address makes stm32flash erase from that address through the end of Flash.
# Supplying address:length limits both the write and erase to one partition.
# stm32flash 0.7 on Windows accepts a hexadecimal address but requires the
# length after ':' in decimal.
# -w consumes the next token as its input filename.  Keep -v before the
# PlatformIO-supplied -w so the uploader receives "-v ... -w firmware.bin".
env.Prepend(UPLOADERFLAGS=["-S", slot_address + ":" + slot_length, "-v"])


def show_upload_command(source, target, env):
    del source, target
    print(env.subst('$UPLOADER $UPLOADERFLAGS "$SOURCE" "$UPLOAD_PORT"'))


env.AddCustomTarget(
    name="show_upload_command",
    dependencies=None,
    actions=[show_upload_command],
    title="Resolved serial upload command",
    description="Print stm32flash arguments without accessing the board",
)
