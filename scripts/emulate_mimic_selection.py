#!/usr/bin/env python3
"""Compare a native selection trace against isolated original GetItemToPlay ARM code."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_FPSCR,
                               UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC,
                               UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)

DIGEST = "b12895171c9cb8fd7db067c816ba28b4ae4817869a34654175f3e6dfb683fa8c"
ENTRY, END, MEMMOVE = 0x5c86e0, 0x5c8b04, 0x16520
INSTANCE, SAMPLES, STACK, STOP = 0x4000000, 0x5000000, 0x6000000, 0x7000000


def emulator(image: bytes, count: int):
    cpu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    cpu.mem_map(0x10000, 0x1900000)
    offset = struct.unpack_from("<I", image, 28)[0]
    size, total = struct.unpack_from("<HH", image, 42)
    for index in range(total):
        kind, start, address, _, length, _, _, _ = struct.unpack_from("<8I", image, offset + index * size)
        if kind == 1:
            cpu.mem_write(address, image[start:start + length])
    for address, size in ((INSTANCE, 0x270000), (SAMPLES, 0x10000),
                          (STACK, 0x10000), (STOP, 0x1000)):
        cpu.mem_map(address, size)

    def put(offset, value):
        cpu.mem_write(INSTANCE + offset, struct.pack("<I", value))

    put(0x40a88, 1)   # one articulation
    put(0x4fd90, 1)   # one velocity layer
    put(0x40dd0, count)
    put(0x40dd8, count)
    put(0x40ddc, count // 2)
    for index in range(count):
        put(index * 4, SAMPLES + index * 0x100)
        cpu.mem_write(SAMPLES + index * 0x100 + 0x3d, b"\1")
        put(0x40d90 + index * 4, index)
        put(0x40de0 + index * 4, index)

    def guard(uc, address, size, user):
        if address == MEMMOVE:
            destination = uc.reg_read(UC_ARM_REG_R0)
            source = uc.reg_read(UC_ARM_REG_R1)
            length = uc.reg_read(UC_ARM_REG_R2)
            if length > 512 or not (INSTANCE <= destination <= INSTANCE + 0x270000 - length
                                    and INSTANCE <= source <= INSTANCE + 0x270000 - length):
                raise RuntimeError("unexpected memmove in bounded fixture")
            uc.mem_write(destination, bytes(uc.mem_read(source, length)))
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
        elif not ENTRY <= address < END:
            raise RuntimeError(f"selection left declared function range: {address:#x}")

    cpu.hook_add(UC_HOOK_CODE, guard)
    cpu.reg_write(UC_ARM_REG_C1_C0_2, 0xf00000)
    cpu.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
    return cpu


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--application", type=Path, required=True)
    parser.add_argument("--native-trace", type=Path, required=True,
                        help="helper accepting alternative count, hit count, forced-first interval")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    image = args.application.read_bytes()
    if hashlib.sha256(image).hexdigest() != DIGEST:
        raise ValueError("unsupported original executable digest")
    records = []
    for count in range(1, 17):
        for force_interval in (0, 13):
            cpu = emulator(image, count)
            expected = []
            for hit in range(200):
                cpu.reg_write(UC_ARM_REG_FPSCR, 0)
                cpu.reg_write(UC_ARM_REG_SP, STACK + 0xff00)
                cpu.reg_write(UC_ARM_REG_LR, STOP)
                for register, value in ((UC_ARM_REG_R0, INSTANCE), (UC_ARM_REG_R1, 64),
                                        (UC_ARM_REG_R2, 0),
                                        (UC_ARM_REG_R3, int(force_interval > 0 and hit % force_interval == 0))):
                    cpu.reg_write(register, value)
                cpu.emu_start(ENTRY, STOP, count=10000)
                if cpu.reg_read(UC_ARM_REG_PC) != STOP:
                    raise RuntimeError("selection exceeded its instruction limit")
                pointer = cpu.reg_read(UC_ARM_REG_R0)
                if pointer < SAMPLES or (pointer - SAMPLES) % 0x100:
                    raise RuntimeError("selection returned an unexpected sample pointer")
                expected.append((pointer - SAMPLES) // 0x100)
            result = subprocess.check_output([str(args.native_trace.resolve()), str(count), "200",
                                              str(force_interval)], text=True)
            actual = [int(value) for value in result.split()]
            if actual != expected:
                mismatch = next((i for i, (x, y) in enumerate(zip(actual, expected)) if x != y),
                                min(len(actual), len(expected)))
                raise RuntimeError(f"native selection differs at count={count}, hit={mismatch}")
            records.append({"alternatives": count, "forced_first_interval": force_interval,
                            "selections": expected})
    report = {"source_sha256": DIGEST, "function": hex(ENTRY), "kind": "isolated-function-emulation",
              "emulator": "Unicorn", "host_hook": "bounded memory-only memmove",
              "fixture": "one articulation, one layer, all samples ready, velocity64",
              "matched_hits": sum(len(record["selections"]) for record in records), "results": records}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"{report['matched_hits']} selections match original ARM results exactly")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
