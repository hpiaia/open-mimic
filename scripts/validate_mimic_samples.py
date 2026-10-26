#!/usr/bin/env python3
"""Compare a native decoder against the original library's float preload data.

Reads local copies only. It does not alter the instrument, original library,
or sound files. Full decoded samples are temporary unless explicitly retained.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

from inspect_mimic_library import parse


def verify_footer(path: Path) -> dict | None:
    with path.open("rb") as stream:
        stream.seek(-23, 2)
        footer = stream.read(23)
        if footer[:7] != b"MMKCSM\0":
            return None
        stream.seek(0)
        remaining = path.stat().st_size - 23
        digest = hashlib.md5()
        while remaining:
            block = stream.read(min(1024 * 1024, remaining))
            if not block:
                raise ValueError("truncated MCI while verifying checksum")
            digest.update(block)
            remaining -= len(block)
        if digest.digest() != footer[7:]:
            raise ValueError("MCI checksum does not match")
        return {"algorithm": "MD5", "hex": digest.hexdigest(), "verified": True}


def validate(metadata: Path, data: Path, decoder: Path, indices: list[int] | None) -> dict:
    instrument = parse(metadata)
    groups = instrument["groups"]
    arrays = groups["int_array"]
    count = groups["int"]["INST0pool0psz"]
    monolithic = groups["bool"].get("INSTmnlth", False)
    base = instrument["metadata_bytes"] if monolithic else 0
    selected = list(range(count)) if indices is None else indices
    for index in selected:
        if index < 0 or index >= count:
            raise ValueError(f"sample index outside pool: {index}")
    records = []
    data_size = data.stat().st_size
    with data.open("rb") as source, tempfile.TemporaryDirectory(prefix="mimic-decode-") as work:
        for index in selected:
            descriptor = {name: arrays["INST0pool0" + name][index]
                          for name in ("dedo", "desc", "dofs", "dlen", "fcnt", "nchn", "nobu")}
            if (any(value < 0 for value in descriptor.values())
                    or descriptor["desc"] < 64 or descriptor["nchn"] < 1
                    or descriptor["fcnt"] % descriptor["nchn"]
                    or descriptor["desc"] - 64 > descriptor["fcnt"]
                    or descriptor["dlen"] % 1024
                    or descriptor["nobu"] > descriptor["dlen"] // 1024):
                raise ValueError(f"unsupported sample descriptor at {index}")
            stream_start = base + descriptor["dofs"]
            if stream_start < 0 or descriptor["dlen"] > data_size - stream_start:
                raise ValueError(f"sample stream outside input file at {index}")
            preload_start = base + descriptor["dedo"]
            if preload_start < 0 or descriptor["desc"] * 4 > data_size - preload_start:
                raise ValueError(f"sample preload outside input file at {index}")
            covered_scalars = 0
            for page in range(descriptor["nobu"]):
                source.seek(stream_start + page * 1024 + 4)
                covered_scalars += struct.unpack("<I", source.read(4))[0]
            if covered_scalars + 64 != descriptor["desc"]:
                raise ValueError(f"nobu does not describe the preloaded pages at {index}")
            output = Path(work) / f"{index}.f32"
            result = subprocess.run([str(decoder), str(data), str(stream_start),
                                     str(descriptor["dlen"]), str(output)],
                                    check=True, capture_output=True, text=True)
            decoded = output.read_bytes()
            if len(decoded) != descriptor["fcnt"] * 4:
                raise ValueError(f"decoded count differs from fcnt at {index}: {result.stdout.strip()}")
            source.seek(preload_start)
            preload = source.read(descriptor["desc"] * 4)
            if len(preload) != descriptor["desc"] * 4 or preload[:256] != bytes(256):
                raise ValueError(f"missing 64-scalar preload guard at {index}")
            reference = preload[256:]
            if reference != decoded[:len(reference)]:
                raise ValueError(f"decoder differs from original preload at {index}")
            records.append({"sample": index, **descriptor,
                            "matched_preload_scalars": len(reference) // 4,
                            "stream_tail_offset": stream_start + descriptor["nobu"] * 1024,
                            "preload_frame_remainder": covered_scalars % descriptor["nchn"],
                            "preload_sha256": hashlib.sha256(reference).hexdigest(),
                            "decoded_sha256": hashlib.sha256(decoded).hexdigest(),
                            "physical_frames": descriptor["fcnt"] // descriptor["nchn"]})
            output.unlink()
    return {"metadata": str(metadata), "data": str(data), "pool_samples": count,
            "validated_samples": len(records), "payload_base": base,
            "mci_checksum": verify_footer(data) if monolithic else None,
            "results": records}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("metadata", type=Path)
    parser.add_argument("--data", type=Path)
    parser.add_argument("--decoder", required=True, type=Path)
    parser.add_argument("--samples", type=int, nargs="+")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    data = args.data or (args.metadata if args.metadata.suffix.lower() == ".mci"
                         else args.metadata.with_suffix(".drd"))
    report = validate(args.metadata, data, args.decoder.resolve(), args.samples)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"{report['validated_samples']}/{report['pool_samples']} samples validated; "
          "decoded lengths and original preload bytes match")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
