#!/usr/bin/env python3
"""Build a locally testable multi-mic Mimic library from an encoded SCM instrument.

This is the metadata/checksum half of the OPEN MIMIC compiler. It deliberately
reuses an existing, legally obtained `.drd` because arbitrary WAV -> DRD encoding
is not validated yet. The `.bin` is decoded, renamed, rebuilt from typed sections,
and recompressed; `checksum.dat` is regenerated for the new files.

The output is for local reverse-engineering/device validation only. Do not
redistribute the copied third-party audio.
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import struct
import zlib
from pathlib import Path

from mimic_kv import (
    CHECKSUM_MAGIC,
    STORAGE_MAGIC,
    instrument_summary,
    load_storage,
    parse_checksum,
    verify_checksum,
)


def _key(key: str) -> bytes:
    raw = key.encode("latin1") + b"\x00"
    return struct.pack("<I", len(raw)) + raw


def encode_storage(storage) -> bytes:
    """Rebuild a fully parsed mimic_storage payload from its typed maps."""
    payload = bytearray()

    payload += struct.pack("<I", len(storage.u32))
    for key, value in storage.u32.items():
        payload += _key(key) + struct.pack("<I", value)

    payload += struct.pack("<I", len(storage.u32_arrays))
    for key, values in storage.u32_arrays.items():
        payload += _key(key) + struct.pack("<I", len(values))
        payload += struct.pack(f"<{len(values)}I", *values)

    payload += struct.pack("<I", len(storage.f32))
    for key, value in storage.f32.items():
        payload += _key(key) + struct.pack("<f", value)

    payload += struct.pack("<I", len(storage.f32_arrays))
    for key, values in storage.f32_arrays.items():
        payload += _key(key) + struct.pack("<I", len(values))
        payload += struct.pack(f"<{len(values)}f", *values)

    payload += struct.pack("<I", len(storage.bytes1))
    for key, value in storage.bytes1.items():
        payload += _key(key) + bytes([value])

    payload += struct.pack("<I", len(storage.byte_arrays))
    for key, value in storage.byte_arrays.items():
        payload += _key(key) + struct.pack("<I", len(value)) + value

    payload += struct.pack("<I", len(storage.strings))
    for key, value in storage.strings.items():
        raw = value.encode("latin1") + b"\x00"
        payload += _key(key) + struct.pack("<I", len(raw)) + raw

    payload += struct.pack("<I", len(storage.blob)) + storage.blob
    payload += storage.tail

    return (
        STORAGE_MAGIC
        + struct.pack("<II", storage.version, len(payload))
        + zlib.compress(bytes(payload), level=9)
    )


def checksum_header(template: Path) -> bytes:
    data = template.read_bytes()
    offset = len(CHECKSUM_MAGIC) + 4
    header_len = struct.unpack_from("<I", data, offset)[0]
    offset += 4
    return data[offset : offset + header_len]


def encode_checksum(header: bytes, entries: list[tuple[str, bytes]]) -> bytes:
    body = bytearray()
    for name, digest in entries:
        body += name.encode("latin1") + b"\x00" + digest
    return (
        CHECKSUM_MAGIC
        + struct.pack("<II", 1, len(header))
        + header
        + struct.pack("<I", len(entries))
        + zlib.compress(bytes(body), level=9)
    )


def build(source_bin: Path, output_root: Path, name: str, library_name: str) -> Path:
    source_drd = source_bin.with_suffix(".drd")
    source_checksum = source_bin.parent.parent / "checksum.dat"
    if not source_drd.exists():
        raise SystemExit(f"missing matching audio: {source_drd}")
    if not source_checksum.exists():
        raise SystemExit(f"missing checksum template: {source_checksum}")

    storage = load_storage(source_bin)
    summary = instrument_summary(storage)
    if summary["mic_count"] < 2:
        raise SystemExit("source instrument is not multi-mic")

    storage.strings["INST0insnam"] = name
    storage.strings["INST0insdat"] = name
    storage.strings["INST0libnam"] = library_name
    storage.strings["INST0insimj"] = ""

    lib_dir = output_root / f"{library_name}.lib"
    instruments = lib_dir / "instruments"
    if lib_dir.exists():
        shutil.rmtree(lib_dir)
    instruments.mkdir(parents=True)

    out_bin = instruments / f"{name}.bin"
    out_drd = instruments / f"{name}.drd"
    out_bin.write_bytes(encode_storage(storage))
    shutil.copyfile(source_drd, out_drd)
    (lib_dir / "libver.mimicinfo").write_text("10 July 2026", encoding="ascii")

    header = checksum_header(source_checksum)
    entries = [
        (out_bin.name, hashlib.md5(out_bin.read_bytes()).digest()),
        (out_drd.name, hashlib.md5(out_drd.read_bytes()).digest()),
    ]
    (lib_dir / "checksum.dat").write_bytes(encode_checksum(header, entries))

    rebuilt = instrument_summary(load_storage(out_bin))
    checks = parse_checksum(lib_dir / "checksum.dat")
    verification = verify_checksum(lib_dir, checks)
    expected_channels = sum(2 if mic["stereo"] else 1 for mic in rebuilt["mics"])
    if rebuilt["pool"]["nchn_unique"] != [expected_channels]:
        raise SystemExit("rebuilt channel layout failed validation")
    if not rebuilt["pool"]["spans_drd"]:
        raise SystemExit("rebuilt pool does not span copied DRD")
    if verification["missing"] or verification["mismatched"]:
        raise SystemExit(f"checksum validation failed: {verification}")

    print(f"built: {lib_dir}")
    print(f"instrument: {rebuilt['name']} ({rebuilt['instrument_type']})")
    print(f"mics: {rebuilt['mic_count']}  channels/chunk: {expected_channels}")
    for mic in rebuilt["mics"]:
        width = 2 if mic["stereo"] else 1
        print(f"  {mic['index']}: {mic['name']} pos={mic['micpos']} channels={width}")
    print(f"samples: {rebuilt['pool']['sample_count']}")
    print(f"checksum entries verified: {len(verification['ok'])}")
    return lib_dir


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_bin", type=Path)
    parser.add_argument("--output-root", type=Path, default=Path("build"))
    parser.add_argument("--name", default="Open Mimic MultiMic Kick")
    parser.add_argument("--library-name", default="Open Mimic MultiMic Proof")
    args = parser.parse_args()
    build(args.source_bin, args.output_root, args.name, args.library_name)


if __name__ == "__main__":
    main()
