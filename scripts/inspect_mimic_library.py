#!/usr/bin/env python3
"""Read Mimic library metadata without loading or modifying its sample payloads.

Instrument BIN files and integrated MCI files share a mimic_storage v3 prefix.
Unlike the older settings-only inspector, this reader treats the final image
field as a byte-counted blob. It deliberately does not decode audio data.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path

from decode_mimic_storage import MAGIC, Reader

GROUPS = ("int", "int_array", "float", "float_array", "bool", "bool_array", "string")
MAX_METADATA = 64 * 1024 * 1024
MAX_ENCODED_METADATA = MAX_METADATA + 1024 * 1024
READ_CHUNK = 64 * 1024


def metadata_prefix(path: Path) -> tuple[bytes, bytes, int]:
    """Return declared metadata, exact encoded prefix, and its end offset."""
    with path.open("rb") as stream:
        header = stream.read(len(MAGIC) + 8)
        if len(header) != len(MAGIC) + 8 or not header.startswith(MAGIC):
            raise ValueError("missing mimic_storage header")
        version, declared = struct.unpack_from("<II", header, len(MAGIC))
        if version != 3 or declared > MAX_METADATA:
            raise ValueError(f"unsupported version/metadata length: {version}/{declared}")
        inflater = zlib.decompressobj()
        encoded = bytearray(header)
        decoded = bytearray()
        while not inflater.eof:
            chunk = stream.read(READ_CHUNK)
            if not chunk:
                raise ValueError("incomplete compressed metadata prefix")
            decoded.extend(inflater.decompress(chunk, declared + 1 - len(decoded)))
            if len(decoded) > declared or inflater.unconsumed_tail:
                raise ValueError("compressed metadata exceeds declared length")
            used = len(chunk) - len(inflater.unused_data)
            if len(encoded) + used > MAX_ENCODED_METADATA:
                raise ValueError("encoded metadata prefix exceeds size limit")
            encoded.extend(chunk[:used])
        if len(decoded) != declared:
            raise ValueError("metadata length does not match its declaration")
        return bytes(decoded), bytes(encoded), len(encoded)


def parse(path: Path) -> dict:
    data, encoded, metadata_end = metadata_prefix(path)
    reader = Reader(data)
    groups = {}
    for kind in GROUPS:
        count = reader.u32()
        if count > (len(data) - reader.pos) // 5:
            raise ValueError(f"invalid {kind} record count: {count}")
        values = {}
        for _ in range(count):
            key = reader.text()
            if key in values:
                raise ValueError(f"duplicate {kind} key: {key}")
            if kind in ("int", "float"):
                value = struct.unpack("<i" if kind == "int" else "<f", reader.read(4))[0]
            elif kind == "bool":
                byte = reader.read(1)[0]
                if byte not in (0, 1):
                    raise ValueError(f"invalid boolean: {key}")
                value = bool(byte)
            elif kind in ("int_array", "float_array", "bool_array"):
                length = reader.u32()
                raw = reader.read(length * (1 if kind == "bool_array" else 4))
                if kind == "bool_array":
                    if any(byte not in (0, 1) for byte in raw):
                        raise ValueError(f"invalid boolean array: {key}")
                    value = [bool(byte) for byte in raw]
                else:
                    code = "i" if kind == "int_array" else "f"
                    value = list(struct.unpack(f"<{length}{code}", raw))
            else:
                value = reader.text()
            values[key] = value
        groups[kind] = values
    image = reader.read(reader.u32())
    if reader.pos != len(data):
        raise ValueError(f"{len(data) - reader.pos} unparsed metadata bytes")
    return {
        "source_path": str(path),
        "source_bytes": path.stat().st_size,
        "format": "mimic_storage",
        "version": 3,
        "metadata_bytes": metadata_end,
        "metadata_sha256": hashlib.sha256(encoded).hexdigest(),
        "uncompressed_metadata_bytes": len(data),
        "trailing_bytes": path.stat().st_size - metadata_end,
        "groups": groups,
        "image": {
            "bytes": len(image),
            "format": "png" if image.startswith(b"\x89PNG\r\n\x1a\n") else ("none" if not image else "unknown"),
            "sha256": hashlib.sha256(image).hexdigest(),
        },
    }


def main() -> None:
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("input", type=Path, help="An instrument, kit, integrated MCI, or copied metadata directory")
    cli.add_argument("--output-dir", type=Path, help="Write decoded JSON while preserving relative subdirectories")
    args = cli.parse_args()
    paths = sorted(path for path in args.input.rglob("*") if path.is_file() and path.suffix.lower() in (".bin", ".kit", ".mci")) if args.input.is_dir() else [args.input]
    if not paths:
        cli.error("no supported metadata files found")
    for path in paths:
        document = parse(path)
        counts = {kind: len(records) for kind, records in document["groups"].items()}
        print(f"{path.name}: metadata={document['metadata_bytes']} trailing={document['trailing_bytes']} image={document['image']['bytes']} groups={counts}")
        if args.output_dir:
            relative = path.relative_to(args.input) if args.input.is_dir() else Path(path.name)
            output = (args.output_dir / relative).with_suffix(relative.suffix + ".json")
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(json.dumps(document, indent=2, ensure_ascii=False, allow_nan=False) + "\n")


if __name__ == "__main__":
    main()
