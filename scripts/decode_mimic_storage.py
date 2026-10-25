#!/usr/bin/env python3
"""Decode read-only copies of Mimic Pro mimic_storage v3 files."""

from __future__ import annotations

import argparse
import base64
import csv
import hashlib
import json
import re
import struct
import zlib
from pathlib import Path

MAGIC = b"mimic_storage\0"
GROUPS = (
    "int", "int_array", "float", "float_array",
    "bool", "bool_array", "string", "image",
)
MAX_PAYLOAD = 2 * 1024 * 1024
XTALK_KEY = re.compile(r"XTCa(\d+)b(\d+)")


class Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0

    def read(self, size: int) -> bytes:
        if size < 0 or size > len(self.data) - self.pos:
            raise ValueError(f"truncated payload at offset {self.pos}")
        value = self.data[self.pos:self.pos + size]
        self.pos += size
        return value

    def u32(self) -> int:
        return struct.unpack("<I", self.read(4))[0]

    def text(self) -> str:
        size = self.u32()
        if not size or size > MAX_PAYLOAD:
            raise ValueError(f"invalid string length {size} at offset {self.pos - 4}")
        raw = self.read(size)
        if raw[-1] != 0:
            raise ValueError(f"missing string terminator at offset {self.pos - size}")
        return raw[:-1].decode("utf-8")


def parse(path: Path) -> dict:
    source = path.read_bytes()
    if not source.startswith(MAGIC) or len(source) < len(MAGIC) + 8:
        raise ValueError("missing mimic_storage header")
    version, expected = struct.unpack_from("<II", source, len(MAGIC))
    if version != 3 or expected > MAX_PAYLOAD:
        raise ValueError(f"unsupported version or payload size: {version}, {expected}")
    compressed = source[len(MAGIC) + 8:]
    inflater = zlib.decompressobj()
    data = inflater.decompress(compressed, expected + 1)
    if len(data) != expected or not inflater.eof or inflater.unused_data or inflater.unconsumed_tail:
        raise ValueError("zlib stream or declared payload size is inconsistent")

    reader = Reader(data)
    groups: dict[str, dict] = {}
    for kind in GROUPS:
        count = reader.u32()
        if count > (len(data) - reader.pos) // 5:
            raise ValueError(f"invalid {kind} record count {count}")
        values: dict = {}
        for _ in range(count):
            key = reader.text()
            if key in values:
                raise ValueError(f"duplicate {kind} key: {key}")
            if kind in ("int", "float"):
                value = struct.unpack("<i" if kind == "int" else "<f", reader.read(4))[0]
            elif kind == "bool":
                byte = reader.read(1)[0]
                if byte not in (0, 1):
                    raise ValueError(f"invalid boolean value for {key}: {byte}")
                value = bool(byte)
            elif kind in ("int_array", "float_array", "bool_array"):
                length = reader.u32()
                element_size = 1 if kind == "bool_array" else 4
                raw = reader.read(length * element_size)
                if kind == "bool_array":
                    if any(byte not in (0, 1) for byte in raw):
                        raise ValueError(f"invalid boolean array: {key}")
                    value = [bool(byte) for byte in raw]
                else:
                    code = "i" if kind == "int_array" else "f"
                    value = list(struct.unpack(f"<{length}{code}", raw))
            elif kind == "string":
                value = reader.text()
            else:
                value = base64.b64encode(reader.read(reader.u32())).decode("ascii")
            values[key] = value
        groups[kind] = values
    if reader.pos != len(data):
        raise ValueError(f"{len(data) - reader.pos} unparsed bytes remain")
    return {
        "source": path.name,
        "sha256": hashlib.sha256(source).hexdigest(),
        "format": "mimic_storage",
        "version": version,
        "uncompressed_bytes": expected,
        "groups": groups,
    }


def xtalk_matrix(document: dict) -> list[list[float]]:
    values = document["groups"]["float"]
    matrix: list[list[float | None]] = [[None] * 32 for _ in range(32)]
    for key, value in values.items():
        match = XTALK_KEY.fullmatch(key)
        if match is None:
            raise ValueError(f"unexpected float key in xtalk settings: {key}")
        row, column = map(int, match.groups())
        if row >= 32 or column >= 32 or matrix[row][column] is not None:
            raise ValueError(f"invalid or repeated xtalk matrix index: {key}")
        matrix[row][column] = value
    if any(value is None for row in matrix for value in row):
        raise ValueError("incomplete 32x32 xtalk matrix")
    return [[float(value) for value in row] for row in matrix]


def main() -> None:
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("input", type=Path, help="A copied .dat file or directory of copied files")
    cli.add_argument("--output-dir", type=Path, help="Write decoded JSON files to this directory")
    cli.add_argument("--xtalk-csv", type=Path, help="Write the 32x32 xtalk matrix as CSV")
    args = cli.parse_args()
    paths = sorted(args.input.glob("*.dat")) if args.input.is_dir() else [args.input]
    if not paths:
        cli.error("no .dat files found")
    if args.output_dir:
        args.output_dir.mkdir(parents=True, exist_ok=True)
    for path in paths:
        document = parse(path)
        counts = {kind: len(values) for kind, values in document["groups"].items()}
        print(f"{path.name}: {document['uncompressed_bytes']} bytes; {counts}")
        if args.output_dir:
            output = args.output_dir / f"{path.stem}.json"
            output.write_text(json.dumps(document, indent=2, ensure_ascii=False) + "\n")
            print(f"  JSON: {output}")
        if args.xtalk_csv and path.name == "xtalk_settings.dat":
            matrix = xtalk_matrix(document)
            args.xtalk_csv.parent.mkdir(parents=True, exist_ok=True)
            with args.xtalk_csv.open("w", newline="") as stream:
                writer = csv.writer(stream)
                writer.writerow(["a\\b", *range(32)])
                for index, row in enumerate(matrix):
                    writer.writerow([index, *row])
            print(f"  Xtalk CSV: {args.xtalk_csv}")


if __name__ == "__main__":
    main()
