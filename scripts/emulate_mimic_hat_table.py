#!/usr/bin/env python3
"""Observe one original ARM function in isolation; requires optional unicorn==2.1.4.

This does not launch the original application or communicate with hardware.
Only BuildForZone3 executes, with synthetic control-zone metadata and an
identity stub for its articulation-ID lookup. The resulting table can serve
as an independent regression oracle for HatControlTable.
"""

import argparse
import hashlib
import json
import struct
from pathlib import Path


EXPECTED_SHA256 = "b12895171c9cb8fd7db067c816ba28b4ae4817869a34654175f3e6dfb683fa8c"


def emulate(application: Path) -> dict:
    import unicorn
    from unicorn import Uc, UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_ARM
    from unicorn.arm_const import (
        UC_ARM_REG_C1_C0_2,
        UC_ARM_REG_FPEXC,
        UC_ARM_REG_FPSCR,
        UC_ARM_REG_LR,
        UC_ARM_REG_PC,
        UC_ARM_REG_R0,
        UC_ARM_REG_R1,
        UC_ARM_REG_R2,
        UC_ARM_REG_SP,
    )

    data = application.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != EXPECTED_SHA256:
        raise ValueError("The supplied ELF does not match the investigated Mimic build")
    machine = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    machine.mem_map(0x10000, 0x1900000)
    program_offset = struct.unpack_from("<I", data, 28)[0]
    program_size, program_count = struct.unpack_from("<HH", data, 42)
    for index in range(program_count):
        fields = struct.unpack_from("<8I", data, program_offset + index * program_size)
        segment_type, offset, address, _, file_size, _, _, _ = fields
        if segment_type == 1:
            machine.mem_write(address, data[offset : offset + file_size])

    table, slot, instrument = 0x2000000, 0x3000000, 0x4000000
    stack, stop = 0x6000000, 0x7000000
    for address, size in (
        (table, 0x10000),
        (slot, 0x1000),
        (instrument, 0x300000),
        (stack, 0x10000),
        (stop, 0x1000),
    ):
        machine.mem_map(address, size)

    def put(address: int, value: int) -> None:
        machine.mem_write(address, struct.pack("<I", value))

    put(slot + 4, instrument)
    put(instrument + 0x2691B8, 5)
    for index in range(5):
        put(instrument + (index + 0x9A44C) * 4 + 8, index)

    def identity_lookup(emulator, address, _size, _context):
        if address == 0x5C8000:
            emulator.reg_write(UC_ARM_REG_R0, emulator.reg_read(UC_ARM_REG_R1))
            emulator.reg_write(UC_ARM_REG_PC, emulator.reg_read(UC_ARM_REG_LR))
        elif not 0x5C22D4 <= address < 0x5C2778:
            raise RuntimeError(f"Hi-hat builder left its declared instruction range: {address:#x}")

    machine.hook_add(UC_HOOK_CODE, identity_lookup)
    machine.reg_write(UC_ARM_REG_C1_C0_2, 0xF00000)
    machine.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
    machine.reg_write(UC_ARM_REG_FPSCR, 0)
    machine.reg_write(UC_ARM_REG_SP, stack + 0xFF00)
    machine.reg_write(UC_ARM_REG_LR, stop)
    machine.reg_write(UC_ARM_REG_R0, table)
    machine.reg_write(UC_ARM_REG_R1, 0)
    machine.reg_write(UC_ARM_REG_R2, slot)
    machine.emu_start(0x5C22D4, stop, count=100000)
    if machine.reg_read(UC_ARM_REG_PC) != stop:
        raise RuntimeError("The isolated function did not return within the instruction bound")

    rows = []
    for index in range(100):
        raw = bytes(machine.mem_read(table + index * 0x24 + 0x14, 0x24))
        first, gain1, two, second, gain2, b1, b2, b3, b4, p1, duplicate, p2 = (
            struct.unpack("<if?3xif4Biii", raw)
        )
        rows.append(
            dict(
                bin=index,
                first=first,
                first_gain=gain1,
                two_voice=two,
                second=second,
                second_gain=gain2,
                flags=[b1, b2, b3, b4],
                ordinals=[p1, duplicate, p2],
            )
        )
    return dict(
        source_sha256=digest,
        function="0x5c22d4",
        emulator=f"unicorn{unicorn.__version__}",
        hook="replyArtIdxByArtID returns requested ID; control IDs=0..4",
        entries=rows,
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--application", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    result = emulate(args.application)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(f"Saved {len(result['entries'])} original ARM-emulated entries to {args.output}")


if __name__ == "__main__":
    main()
