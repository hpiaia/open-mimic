#!/usr/bin/env python3
"""Compare the native decoder with one SHA-locked original ARM leaf function.

Requires optional unicorn==2.1.4 on the analysis host. No original process is
launched: only the decoder's instruction range runs in isolated emulated RAM,
with no host I/O hooks and a per-call instruction limit.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import tempfile

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_FPSCR,
                               UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC,
                               UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)
from inspect_mimic_library import parse

SOURCE_SHA256 = "b12895171c9cb8fd7db067c816ba28b4ae4817869a34654175f3e6dfb683fa8c"
ENTRY, END = 0x5c0294, 0x5c036c
SOURCE, DEST, COUNTS, STACK, STOP = 0x2000000, 0x2100000, 0x2200000, 0x6000000, 0x7000000


class OriginalDecoder:
    def __init__(self, application: Path):
        image = application.read_bytes()
        if hashlib.sha256(image).hexdigest() != SOURCE_SHA256:
            raise ValueError("unsupported original executable digest")
        self.cpu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        self.cpu.mem_map(0x10000, 0x1900000)
        offset = struct.unpack_from("<I", image, 28)[0]
        size, count = struct.unpack_from("<HH", image, 42)
        for index in range(count):
            kind, start, address, _, length, _, _, _ = struct.unpack_from("<8I", image, offset + index * size)
            if kind == 1:
                self.cpu.mem_write(address, image[start:start + length])
        for address, size in ((SOURCE, 0x1000), (DEST, 0x10000), (COUNTS, 0x1000),
                              (STACK, 0x10000), (STOP, 0x1000)):
            self.cpu.mem_map(address, size)
        self.cpu.reg_write(UC_ARM_REG_C1_C0_2, 0xf00000)
        self.cpu.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
        self.cpu.hook_add(UC_HOOK_CODE, self.guard)

    @staticmethod
    def guard(cpu, address, size, user):
        if not ENTRY <= address < END:
            raise RuntimeError(f"decoder left its declared instruction range: {address:#x}")

    def decode(self, block: bytes) -> bytes:
        header, count = struct.unpack_from("<II", block)
        width = header & 255
        if not 1 <= width <= 32 or count > 8128 // width:
            raise ValueError("unsafe decoder fixture")
        self.cpu.mem_write(SOURCE, block)
        self.cpu.mem_write(COUNTS, bytes(8))
        self.cpu.reg_write(UC_ARM_REG_FPSCR, 0)
        self.cpu.reg_write(UC_ARM_REG_SP, STACK + 0xff00)
        self.cpu.reg_write(UC_ARM_REG_LR, STOP)
        for register, value in ((UC_ARM_REG_R0, SOURCE), (UC_ARM_REG_R1, DEST),
                                (UC_ARM_REG_R2, COUNTS), (UC_ARM_REG_R3, COUNTS + 4)):
            self.cpu.reg_write(register, value)
        self.cpu.emu_start(ENTRY, STOP, count=1000000)
        if self.cpu.reg_read(UC_ARM_REG_PC) != STOP:
            raise RuntimeError("decoder exceeded its instruction limit")
        actual = struct.unpack("<I", bytes(self.cpu.mem_read(COUNTS, 4)))[0]
        if actual != count:
            raise RuntimeError("original decoder returned a different count")
        return bytes(self.cpu.mem_read(DEST, count * 4))


def synthetic(width: int) -> bytes:
    generator = random.Random(width)
    count = 100
    deltas = [generator.randrange(-(1 << (width - 1)), 1 << (width - 1)) for _ in range(count)]
    mask = (1 << width) - 1
    bits = "".join(format(value & mask, f"0{width}b") for value in deltas)
    bits += "0" * ((32 - len(bits) % 32) % 32)
    payload = b"".join(struct.pack("<I", int(bits[index:index + 32], 2))
                       for index in range(0, len(bits), 32))
    return (struct.pack("<II", ((-12345 & 0xffffff) << 8) | width, count) + payload).ljust(1024, b"\0")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--application", type=Path, required=True)
    parser.add_argument("--decoder", type=Path, required=True)
    parser.add_argument("--metadata", type=Path)
    parser.add_argument("--data", type=Path)
    parser.add_argument("--samples", type=int, nargs="+", default=[0])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    original = OriginalDecoder(args.application)
    fixtures = [(f"synthetic-width-{width}", synthetic(width)) for width in range(1, 33)]
    if args.metadata:
        metadata = parse(args.metadata)
        arrays = metadata["groups"]["int_array"]
        base = metadata["metadata_bytes"] if metadata["groups"]["bool"].get("INSTmnlth", False) else 0
        data = args.data or args.metadata
        with data.open("rb") as stream:
            for index in args.samples:
                length = arrays["INST0pool0dlen"][index]
                for page in sorted({0, 1, length // 2048, length // 1024 - 1}):
                    offset = base + arrays["INST0pool0dofs"][index] + page * 1024
                    stream.seek(offset)
                    block = stream.read(1024)
                    if len(block) != 1024:
                        raise ValueError("truncated real block fixture")
                    fixtures.append((f"sample-{index}-page-{page}", block))
    results = []
    with tempfile.TemporaryDirectory(prefix="mimic-codec-reference-") as folder:
        for index, (name, block) in enumerate(fixtures):
            source, output = Path(folder) / f"{index}.block", Path(folder) / f"{index}.f32"
            source.write_bytes(block)
            subprocess.run([str(args.decoder.resolve()), str(source), "0", "1024", str(output)],
                           check=True, capture_output=True)
            reference = original.decode(block)
            if output.read_bytes() != reference:
                raise RuntimeError(f"native decoder differs from original ARM output: {name}")
            results.append({"fixture": name, "block_sha256": hashlib.sha256(block).hexdigest(),
                            "samples": len(reference) // 4,
                            "decoded_sha256": hashlib.sha256(reference).hexdigest()})
    report = {"source_sha256": SOURCE_SHA256, "function": hex(ENTRY),
              "emulator": "Unicorn", "kind": "bounded-isolated-function-emulation",
              "instruction_limit_per_call": 1000000, "host_io_hooks": False,
              "native_decoder": str(args.decoder), "results": results}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"{len(results)} blocks match original ARM output byte for byte")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
