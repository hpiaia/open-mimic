#!/usr/bin/env python3
"""Compare native velocity lookup with isolated original ARM BuildVelocityMap.

Requires optional unicorn==2.1.4 on the analysis host. Uses synthetic layouts
and optionally every distinct layout from copied BIN/MCI metadata. The only
host hook is a bounded memory-only memset; no original process is launched.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from unicorn import Uc, UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_ARM
from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_FPSCR,
                               UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0,
                               UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_SP)

from inspect_mimic_library import parse

DIGEST = "b12895171c9cb8fd7db067c816ba28b4ae4817869a34654175f3e6dfb683fa8c"
ENTRY, END, MEMSET = 0x5c40a8, 0x5c4358, 0x16c88
INSTANCE, STACK, STOP = 0x4000000, 0x6000000, 0x7000000


class OriginalVelocityMap:
    def __init__(self, image: bytes):
        self.cpu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        self.cpu.mem_map(0x10000, 0x1900000)
        offset = struct.unpack_from("<I", image, 28)[0]
        size, total = struct.unpack_from("<HH", image, 42)
        for index in range(total):
            kind, start, address, _, length, _, _, _ = struct.unpack_from("<8I", image, offset + index * size)
            if kind == 1:
                self.cpu.mem_write(address, image[start:start + length])
        for address, size in ((INSTANCE, 0x10000), (STACK, 0x10000), (STOP, 0x1000)):
            self.cpu.mem_map(address, size)
        self.cpu.reg_write(UC_ARM_REG_C1_C0_2, 0xf00000)
        self.cpu.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
        self.cpu.hook_add(UC_HOOK_CODE, self.guard)

    @staticmethod
    def guard(cpu, address, size, context):
        if address == MEMSET:
            destination = cpu.reg_read(UC_ARM_REG_R0)
            value = cpu.reg_read(UC_ARM_REG_R1)
            length = cpu.reg_read(UC_ARM_REG_R2)
            if (destination, value, length) != (INSTANCE + 0x104, 0, 0x200):
                raise RuntimeError("unexpected memset in velocity-map fixture")
            cpu.mem_write(destination, bytes(length))
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))
        elif not ENTRY <= address < END:
            raise RuntimeError(f"velocity builder left declared instruction range: {address:#x}")

    def build(self, minimums: tuple[int, ...]) -> dict:
        if not 1 <= len(minimums) <= 128:
            raise ValueError("fixture needs 1..128 layers")
        self.cpu.mem_write(INSTANCE, bytes(0x10000))
        self.cpu.mem_write(INSTANCE + 0xf304, struct.pack("<I", len(minimums)))
        for index, minimum in enumerate(minimums):
            self.cpu.mem_write(INSTANCE + 0x494 + index * 0x1e0, struct.pack("<i", minimum))
        self.cpu.reg_write(UC_ARM_REG_FPSCR, 0)
        self.cpu.reg_write(UC_ARM_REG_SP, STACK + 0xff00)
        self.cpu.reg_write(UC_ARM_REG_LR, STOP)
        self.cpu.reg_write(UC_ARM_REG_R0, INSTANCE)
        self.cpu.emu_start(ENTRY, STOP, count=100000)
        if self.cpu.reg_read(UC_ARM_REG_PC) != STOP:
            raise RuntimeError("velocity builder exceeded its instruction limit")
        lookup = list(struct.unpack("<128i", bytes(self.cpu.mem_read(INSTANCE + 0x104, 0x200))))
        maximums = [struct.unpack("<i", bytes(self.cpu.mem_read(INSTANCE + 0x498 + index * 0x1e0, 4)))[0]
                    for index in range(len(minimums))]
        return {"lookup": lookup, "maximums": maximums}


def native_build(helper: Path, minimums: tuple[int, ...]) -> dict:
    output = subprocess.check_output([str(helper), "--velocity", *map(str, minimums)], text=True)
    lookup, maximums = [], []
    for line in output.splitlines():
        fields = line.split()
        if fields[0] == "M" and len(fields) == 3 and int(fields[1]) == len(lookup):
            lookup.append(int(fields[2]))
        elif fields[0] == "R" and len(fields) == 4 and int(fields[1]) == len(maximums):
            if int(fields[2]) != minimums[len(maximums)]:
                raise ValueError("native helper changed a stored layer minimum")
            maximums.append(int(fields[3]))
        else:
            raise ValueError("unexpected native velocity trace")
    if len(lookup) != 128 or len(maximums) != len(minimums):
        raise ValueError("incomplete native velocity trace")
    return {"lookup": lookup, "maximums": maximums}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--application", required=True, type=Path)
    parser.add_argument("--native-trace", required=True, type=Path)
    parser.add_argument("--metadata", type=Path, nargs="*", default=[])
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    image = args.application.read_bytes()
    if hashlib.sha256(image).hexdigest() != DIGEST:
        raise ValueError("unsupported original executable digest")
    layouts = {layout: {"synthetic": True, "articulations": 0} for layout in (
        (0,), (30,), (127,), (-1,), (128,), (0, 30, 60, 87, 107),
        (0, 30, 30), (0, 100, 30), (-10, 30, 200), tuple(range(128)))}
    instruments, articulations = 0, 0
    for root in args.metadata:
        files = sorted(path for path in root.rglob("*")
                       if path.is_file() and path.suffix.lower() in (".bin", ".mci")) if root.is_dir() else [root]
        for path in files:
            ints = parse(path)["groups"]["int"]
            instruments += 1
            for art in range(ints["INST0numart"]):
                prefix = f"INST0artic{art}"
                minimums = tuple(ints[f"{prefix}veloLay{layer}minvel"]
                                 for layer in range(ints[prefix + "numlay"]))
                entry = layouts.setdefault(minimums, {"synthetic": False, "articulations": 0})
                entry["articulations"] += 1
                articulations += 1
    original = OriginalVelocityMap(image)
    results = []
    for minimums, counts in sorted(layouts.items()):
        expected = original.build(minimums)
        if native_build(args.native_trace.resolve(), minimums) != expected:
            raise RuntimeError(f"native velocity layout differs from original ARM: {minimums}")
        results.append({"minimums": minimums, **counts, **expected})
    report = {"source_sha256": DIGEST, "function": hex(ENTRY),
              "kind": "bounded-isolated-function-emulation", "emulator": "Unicorn",
              "host_hook": "bounded memory-only memset", "instruction_limit_per_call": 100000,
              "metadata_instruments": instruments, "metadata_articulations": articulations,
              "distinct_layouts": len(results), "results": results}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"{len(results)} velocity layouts match original ARM; "
          f"{instruments} instruments, {articulations} articulations covered")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
