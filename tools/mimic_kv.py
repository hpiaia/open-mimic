#!/usr/bin/env python3
"""Decode Pearl Mimic Pro ``mimic_storage`` and library checksum files.

This is a reverse-engineering aid for the native Mimic instrument library
format. It parses the small ``*.bin`` instrument descriptors and ``*.kit``
presets found inside a Mimic ``*.lib`` folder. It does not execute or modify
anything.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Any


STORAGE_MAGIC = b"mimic_storage\x00"
CHECKSUM_MAGIC = b"mimic_checksum_list\x00"
PNG_MAGIC = b"\x89PNG\r\n\x1a\n"


class ParseError(ValueError):
    pass


@dataclass
class SectionInfo:
    name: str
    count: int
    start: int
    end: int


@dataclass
class MimicStorage:
    path: Path
    version: int
    expected_size: int
    payload: bytes
    compressed_size: int
    trailing_data: bytes
    u32: dict[str, int]
    u32_arrays: dict[str, list[int]]
    f32: dict[str, float]
    f32_arrays: dict[str, list[float]]
    bytes1: dict[str, int]
    byte_arrays: dict[str, bytes]
    strings: dict[str, str]
    blob: bytes
    tail: bytes
    sections: list[SectionInfo]

    @property
    def kind(self) -> str:
        suffix = self.path.suffix.lower()
        if suffix in (".bin", ".mci"):
            return "instrument"
        if suffix == ".kit":
            return "kit"
        return "mimic_storage"


def read_u32(data: bytes, offset: int) -> int:
    if offset + 4 > len(data):
        raise ParseError(f"short u32 at 0x{offset:x}")
    return struct.unpack_from("<I", data, offset)[0]


def read_f32(data: bytes, offset: int) -> float:
    if offset + 4 > len(data):
        raise ParseError(f"short f32 at 0x{offset:x}")
    return struct.unpack_from("<f", data, offset)[0]


def read_key(data: bytes, offset: int) -> tuple[str, int]:
    key_len = read_u32(data, offset)
    offset += 4
    if key_len == 0 or offset + key_len > len(data):
        raise ParseError(f"bad key length {key_len} at 0x{offset - 4:x}")
    raw = data[offset : offset + key_len]
    offset += key_len
    if not raw.endswith(b"\x00"):
        raise ParseError(f"key missing NUL at 0x{offset - key_len:x}")
    try:
        return raw[:-1].decode("ascii"), offset
    except UnicodeDecodeError:
        return raw[:-1].decode("latin1"), offset


def load_storage(path: Path) -> MimicStorage:
    data = path.read_bytes()
    if not data.startswith(STORAGE_MAGIC):
        raise ParseError(f"{path}: not a mimic_storage container")
    version = read_u32(data, len(STORAGE_MAGIC))
    expected_size = read_u32(data, len(STORAGE_MAGIC) + 4)
    compressed = data[len(STORAGE_MAGIC) + 8 :]
    try:
        decompressor = zlib.decompressobj()
        payload = decompressor.decompress(compressed)
        payload += decompressor.flush()
    except zlib.error as exc:
        raise ParseError(f"{path}: zlib decompression failed: {exc}") from exc
    if not decompressor.eof:
        raise ParseError(f"{path}: unterminated zlib stream")
    compressed_size = len(compressed) - len(decompressor.unused_data)
    if len(payload) != expected_size:
        raise ParseError(
            f"{path}: decompressed {len(payload)} bytes, expected {expected_size}"
        )

    parsed = parse_payload(payload)
    return MimicStorage(
        path=path,
        version=version,
        expected_size=expected_size,
        payload=payload,
        compressed_size=compressed_size,
        trailing_data=decompressor.unused_data,
        **parsed,
    )


def parse_payload(payload: bytes) -> dict[str, Any]:
    offset = 0
    sections: list[SectionInfo] = []

    def section_start(name: str) -> tuple[int, int]:
        nonlocal offset
        start = offset
        count = read_u32(payload, offset)
        offset += 4
        return start, count

    def section_end(name: str, count: int, start: int) -> None:
        sections.append(SectionInfo(name=name, count=count, start=start, end=offset))

    u32_values: dict[str, int] = {}
    start, count = section_start("u32")
    for _ in range(count):
        key, offset = read_key(payload, offset)
        u32_values[key] = read_u32(payload, offset)
        offset += 4
    section_end("u32", count, start)

    u32_arrays: dict[str, list[int]] = {}
    start, count = section_start("u32_arrays")
    for _ in range(count):
        key, offset = read_key(payload, offset)
        n_items = read_u32(payload, offset)
        offset += 4
        end = offset + (n_items * 4)
        if end > len(payload):
            raise ParseError(f"{key}: u32 array overruns payload")
        u32_arrays[key] = list(struct.unpack_from(f"<{n_items}I", payload, offset))
        offset = end
    section_end("u32_arrays", count, start)

    f32_values: dict[str, float] = {}
    start, count = section_start("f32")
    for _ in range(count):
        key, offset = read_key(payload, offset)
        f32_values[key] = read_f32(payload, offset)
        offset += 4
    section_end("f32", count, start)

    f32_arrays: dict[str, list[float]] = {}
    start, count = section_start("f32_arrays")
    for _ in range(count):
        key, offset = read_key(payload, offset)
        n_items = read_u32(payload, offset)
        offset += 4
        end = offset + (n_items * 4)
        if end > len(payload):
            raise ParseError(f"{key}: f32 array overruns payload")
        f32_arrays[key] = list(struct.unpack_from(f"<{n_items}f", payload, offset))
        offset = end
    section_end("f32_arrays", count, start)

    byte_values: dict[str, int] = {}
    start, count = section_start("byte")
    for _ in range(count):
        key, offset = read_key(payload, offset)
        if offset >= len(payload):
            raise ParseError(f"{key}: missing byte value")
        byte_values[key] = payload[offset]
        offset += 1
    section_end("byte", count, start)

    byte_arrays: dict[str, bytes] = {}
    start, count = section_start("byte_arrays")
    for _ in range(count):
        key, offset = read_key(payload, offset)
        n_items = read_u32(payload, offset)
        offset += 4
        end = offset + n_items
        if end > len(payload):
            raise ParseError(f"{key}: byte array overruns payload")
        byte_arrays[key] = payload[offset:end]
        offset = end
    section_end("byte_arrays", count, start)

    strings: dict[str, str] = {}
    start, count = section_start("strings")
    for _ in range(count):
        key, offset = read_key(payload, offset)
        n_bytes = read_u32(payload, offset)
        offset += 4
        end = offset + n_bytes
        if end > len(payload):
            raise ParseError(f"{key}: string overruns payload")
        raw = payload[offset:end]
        offset = end
        strings[key] = raw.rstrip(b"\x00").decode("latin1", "replace")
    section_end("strings", count, start)

    blob = b""
    tail = b""
    if offset + 4 <= len(payload):
        blob_start = offset
        blob_len = read_u32(payload, offset)
        offset += 4
        blob_end = offset + blob_len
        if blob_end <= len(payload):
            blob = payload[offset:blob_end]
            offset = blob_end
            sections.append(
                SectionInfo(name="blob", count=blob_len, start=blob_start, end=offset)
            )
    if offset < len(payload):
        tail = payload[offset:]
        sections.append(
            SectionInfo(name="tail", count=len(tail), start=offset, end=len(payload))
        )

    return {
        "u32": u32_values,
        "u32_arrays": u32_arrays,
        "f32": f32_values,
        "f32_arrays": f32_arrays,
        "bytes1": byte_values,
        "byte_arrays": byte_arrays,
        "strings": strings,
        "blob": blob,
        "tail": tail,
        "sections": sections,
    }


def parse_checksum(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    if not data.startswith(CHECKSUM_MAGIC):
        raise ParseError(f"{path}: not a mimic_checksum_list file")
    offset = len(CHECKSUM_MAGIC)
    version = read_u32(data, offset)
    offset += 4
    header_len = read_u32(data, offset)
    offset += 4
    header_bytes = data[offset : offset + header_len]
    offset += header_len
    declared_count = read_u32(data, offset)
    offset += 4
    decompressed = zlib.decompress(data[offset:])

    entries = []
    cursor = 0
    while cursor < len(decompressed):
        end = decompressed.index(b"\x00", cursor)
        name = decompressed[cursor:end].decode("latin1")
        cursor = end + 1
        digest = decompressed[cursor : cursor + 16]
        if len(digest) != 16:
            raise ParseError(f"{path}: truncated digest for {name}")
        cursor += 16
        entries.append({"name": name, "md5": digest.hex()})

    return {
        "path": str(path),
        "version": version,
        "header_len": header_len,
        "header_sha256": hashlib.sha256(header_bytes).hexdigest(),
        "declared_count": declared_count,
        "entries": entries,
    }


def library_paths(path: Path) -> list[Path]:
    if path.is_dir():
        return (
            sorted(path.glob("instruments/*.bin"))
            + sorted(path.glob("instruments/*.mci"))
            + sorted(path.glob("kits/*.kit"))
        )
    return [path]


def matching_library_file(lib_dir: Path, name: str) -> Path | None:
    for candidate in (
        lib_dir / name,
        lib_dir / "instruments" / name,
        lib_dir / "kits" / name,
    ):
        if candidate.exists():
            return candidate
    return None


def verify_checksum(lib_dir: Path, checksum: dict[str, Any]) -> dict[str, Any]:
    ok = []
    missing = []
    mismatched = []
    for entry in checksum["entries"]:
        file_path = matching_library_file(lib_dir, entry["name"])
        if file_path is None:
            missing.append(entry["name"])
            continue
        digest = hashlib.md5(file_path.read_bytes()).hexdigest()
        if digest == entry["md5"]:
            ok.append(entry["name"])
        else:
            mismatched.append(
                {"name": entry["name"], "expected": entry["md5"], "actual": digest}
            )
    return {"ok": ok, "missing": missing, "mismatched": mismatched}


def section_counts(storage: MimicStorage) -> dict[str, int]:
    return {section.name: section.count for section in storage.sections}


def mic_type_name(mict: int | None) -> str:
    if mict == 0:
        return "close/direct"
    if mict == 1:
        return "ambient"
    return "unknown"


def instrument_summary(storage: MimicStorage) -> dict[str, Any]:
    u32 = storage.u32
    u32a = storage.u32_arrays
    strings = storage.strings
    bytes1 = storage.bytes1
    f32 = storage.f32
    prefix = "INST0"

    mic_count = u32.get(f"{prefix}micnt", 0)
    mics = []
    for idx in range(mic_count):
        mic_prefix = f"{prefix}micinf{idx}"
        mict = u32.get(f"{mic_prefix}mict")
        mics.append(
            {
                "index": idx,
                "name": strings.get(f"{mic_prefix}micn", ""),
                "micpos": u32.get(f"{mic_prefix}micpos"),
                "mict": mict,
                "mict_name": mic_type_name(mict),
                "stereo": bytes1.get(f"{mic_prefix}isst"),
                "enabled": bytes1.get(f"{mic_prefix}micen"),
                "volume": f32.get(f"{mic_prefix}micv"),
            }
        )

    art_count = u32.get(f"{prefix}numart", 0)
    articulations = []
    for idx in range(art_count):
        art_prefix = f"{prefix}artic{idx}"
        articulations.append(
            {
                "index": idx,
                "name": strings.get(f"{art_prefix}artn", ""),
                "artid": u32.get(f"{art_prefix}artid"),
                "noteon": u32.get(f"{art_prefix}noteon"),
                "velocity_layers": u32.get(f"{art_prefix}numlay"),
            }
        )

    pool_prefix = f"{prefix}pool0"
    dofs = u32a.get(f"{pool_prefix}dofs", [])
    dlen = u32a.get(f"{pool_prefix}dlen", [])
    fcnt = u32a.get(f"{pool_prefix}fcnt", [])
    nchn = u32a.get(f"{pool_prefix}nchn", [])
    max_end = max((offset + length for offset, length in zip(dofs, dlen)), default=0)
    drd_path = storage.path.with_suffix(".drd")
    audio_source = "none"
    audio_size = None
    audio_reader = None
    footer = b""
    footer_md5_ok = None
    if drd_path.exists():
        audio_source = "separate_drd"
        audio_size = drd_path.stat().st_size
        audio_reader = drd_path.open("rb")
    elif storage.path.suffix.lower() == ".mci" and storage.trailing_data:
        audio_source = "embedded_mci"
        audio_size = max_end
        audio_reader = io.BytesIO(storage.trailing_data[:max_end])
        footer = storage.trailing_data[max_end:]
        if len(footer) == 23 and footer.startswith(b"MMKCSM\x00"):
            stored_md5 = footer[7:]
            file_data = storage.path.read_bytes()
            footer_md5_ok = hashlib.md5(file_data[:-23]).digest() == stored_md5

    chunk_headers = []
    if audio_reader is not None:
        with audio_reader as fh:
            for idx, offset in enumerate(dofs[:5]):
                fh.seek(offset)
                raw = fh.read(16)
                if len(raw) == 16:
                    chunk_headers.append(
                        {
                            "index": idx,
                            "offset": offset,
                            "words_le": list(struct.unpack("<4I", raw)),
                            "hex": raw.hex(),
                        }
                    )

    return {
        "path": str(storage.path),
        "kind": storage.kind,
        "version": storage.version,
        "payload_size": len(storage.payload),
        "sections": section_counts(storage),
        "name": strings.get(f"{prefix}insnam", storage.path.stem),
        "instrument_type": strings.get(f"{prefix}instyp", ""),
        "library_name": strings.get(f"{prefix}libnam", ""),
        "image_source": strings.get(f"{prefix}insimj", ""),
        "blob": {
            "length": len(storage.blob),
            "type": "png" if storage.blob.startswith(PNG_MAGIC) else "unknown",
            "sha256": hashlib.sha256(storage.blob).hexdigest() if storage.blob else "",
        },
        "mic_count": mic_count,
        "mics": mics,
        "articulation_count": art_count,
        "articulations": articulations,
        "zone_count": u32.get(f"{prefix}zonecnt"),
        "pool": {
            "sample_count": u32.get(f"{pool_prefix}psz"),
            "dofs_count": len(dofs),
            "dlen_count": len(dlen),
            "fcnt_count": len(fcnt),
            "nchn_unique": sorted(set(nchn)),
            "audio_source": audio_source,
            "audio_size": audio_size,
            "drd_path": str(drd_path) if drd_path.exists() else "",
            "drd_size": drd_path.stat().st_size if drd_path.exists() else None,
            "max_drd_end": max_end,
            "spans_drd": (audio_size == max_end) if audio_size is not None else None,
            "mci_footer_magic": footer[:7].decode("latin1") if footer else "",
            "mci_footer_md5": footer[7:].hex() if len(footer) == 23 else "",
            "mci_footer_md5_ok": footer_md5_ok,
            "first_chunk_headers": chunk_headers,
            "raw_pcm_byte_match_count": sum(
                1
                for frames, channels, length in zip(fcnt, nchn, dlen)
                if frames * channels * 4 == length
            ),
        },
    }


def kit_summary(storage: MimicStorage) -> dict[str, Any]:
    kit_instruments = {}
    for key, value in storage.strings.items():
        if key.startswith("insstCS") and key.endswith("lsin") and value:
            slot = key.removeprefix("insstCS").removesuffix("lsin")
            kit_instruments[slot] = value
    return {
        "path": str(storage.path),
        "kind": storage.kind,
        "version": storage.version,
        "payload_size": len(storage.payload),
        "sections": section_counts(storage),
        "name": storage.path.stem,
        "instrument_slots": kit_instruments,
        "blob_length": len(storage.blob),
        "tail_length": len(storage.tail),
    }


def summarize_storage(storage: MimicStorage) -> dict[str, Any]:
    if storage.kind == "instrument":
        return instrument_summary(storage)
    if storage.kind == "kit":
        return kit_summary(storage)
    return {
        "path": str(storage.path),
        "kind": storage.kind,
        "version": storage.version,
        "payload_size": len(storage.payload),
        "sections": section_counts(storage),
        "blob_length": len(storage.blob),
        "tail_length": len(storage.tail),
    }


def shortened(value: Any, max_items: int = 12) -> str:
    if isinstance(value, float):
        return f"{value:.7g}"
    if isinstance(value, bytes):
        return value[:32].hex() + (f"... ({len(value)} bytes)" if len(value) > 32 else "")
    if isinstance(value, list):
        if len(value) > max_items:
            shown = ", ".join(shortened(v) for v in value[:max_items])
            return f"[{shown}, ...] ({len(value)} items)"
        return "[" + ", ".join(shortened(v) for v in value) + "]"
    return str(value)


def iter_key_values(storage: MimicStorage) -> list[tuple[str, str, Any]]:
    items: list[tuple[str, str, Any]] = []
    for section_name, mapping in (
        ("u32", storage.u32),
        ("u32_arrays", storage.u32_arrays),
        ("f32", storage.f32),
        ("f32_arrays", storage.f32_arrays),
        ("byte", storage.bytes1),
        ("byte_arrays", storage.byte_arrays),
        ("strings", storage.strings),
    ):
        for key in sorted(mapping):
            items.append((section_name, key, mapping[key]))
    return items


def print_key_dump(storage: MimicStorage, filters: list[str]) -> None:
    print(f"# {storage.path}")
    print(
        f"# kind={storage.kind} version={storage.version} payload={len(storage.payload)} "
        f"sections={section_counts(storage)}"
    )
    for section_name, key, value in iter_key_values(storage):
        if filters and not any(token in key for token in filters):
            continue
        print(f"{section_name:10s} {key:48s} {shortened(value)}")
    if storage.blob:
        blob_type = "png" if storage.blob.startswith(PNG_MAGIC) else "unknown"
        print(f"blob       length={len(storage.blob)} type={blob_type}")
    if storage.tail:
        print(f"tail       length={len(storage.tail)} hex={storage.tail[:32].hex()}")


def print_instrument_summary(summary: dict[str, Any]) -> None:
    pool = summary["pool"]
    print(
        f"- {summary['name']} ({summary['instrument_type']}): "
        f"{summary['mic_count']} mics, {summary['articulation_count']} articulations, "
        f"pool={pool['sample_count']} chunks, nchn={pool['nchn_unique']}, "
        f"drd_span={'ok' if pool['spans_drd'] else 'unknown/bad'}"
    )
    for mic in summary["mics"]:
        stereo = "stereo" if mic["stereo"] else "mono"
        print(
            f"    mic{mic['index']}: {mic['name'] or '?'} "
            f"pos={mic['micpos']} type={mic['mict']}:{mic['mict_name']} {stereo}"
        )


def print_kit_summary(summary: dict[str, Any]) -> None:
    slots = summary["instrument_slots"]
    preview = ", ".join(f"{slot}={name}" for slot, name in sorted(slots.items())[:6])
    suffix = " ..." if len(slots) > 6 else ""
    print(f"- {summary['name']}: {len(slots)} instrument slot refs")
    if preview:
        print(f"    {preview}{suffix}")


def print_summary(summary: dict[str, Any]) -> None:
    if summary["kind"] == "instrument":
        print_instrument_summary(summary)
    elif summary["kind"] == "kit":
        print_kit_summary(summary)
    else:
        print(f"- {summary['path']}: {summary['kind']}")


def analyze_path(path: Path, args: argparse.Namespace) -> list[dict[str, Any]]:
    if path.name == "checksum.dat":
        checksum = parse_checksum(path)
        if args.json:
            print(json.dumps(checksum, indent=2, sort_keys=True))
        else:
            print(
                f"# {path}\n"
                f"version={checksum['version']} declared_entries={checksum['declared_count']} "
                f"decoded_entries={len(checksum['entries'])}"
            )
            for entry in checksum["entries"]:
                print(f"{entry['md5']}  {entry['name']}")
        return []

    summaries = []
    for candidate in library_paths(path):
        storage = load_storage(candidate)
        if args.keys is not None:
            print_key_dump(storage, args.keys)
            continue
        summary = summarize_storage(storage)
        summaries.append(summary)
        if not args.json:
            print_summary(summary)
    return summaries


def analyze_library(path: Path, args: argparse.Namespace) -> dict[str, Any]:
    instruments = []
    kits = []
    for file_path in sorted(path.glob("instruments/*.bin")):
        instruments.append(summarize_storage(load_storage(file_path)))
    for file_path in sorted(path.glob("instruments/*.mci")):
        instruments.append(summarize_storage(load_storage(file_path)))
    for file_path in sorted(path.glob("kits/*.kit")):
        kits.append(summarize_storage(load_storage(file_path)))

    checksum_info = None
    checksum_path = path / "checksum.dat"
    if checksum_path.exists():
        checksum_info = parse_checksum(checksum_path)
        if args.verify_md5:
            checksum_info["verification"] = verify_checksum(path, checksum_info)

    libver_path = path / "libver.mimicinfo"
    libver = libver_path.read_text("latin1").strip() if libver_path.exists() else ""
    result = {
        "path": str(path),
        "libver": libver,
        "checksum": checksum_info,
        "instrument_count": len(instruments),
        "kit_count": len(kits),
        "instruments": instruments,
        "kits": kits,
    }
    if not args.json:
        print(f"# Library: {path}")
        if libver:
            print(f"libver={libver}")
        if checksum_info:
            line = (
                f"checksum entries={len(checksum_info['entries'])} "
                f"declared={checksum_info['declared_count']}"
            )
            verification = checksum_info.get("verification")
            if verification:
                line += (
                    f" md5_ok={len(verification['ok'])} "
                    f"missing={len(verification['missing'])} "
                    f"mismatched={len(verification['mismatched'])}"
                )
            print(line)
        print(f"\n## Instruments ({len(instruments)})")
        for summary in instruments:
            print_instrument_summary(summary)
        print(f"\n## Kits ({len(kits)})")
        for summary in kits:
            print_kit_summary(summary)
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", type=Path, help=".bin/.kit/checksum.dat or .lib dir")
    parser.add_argument("--json", action="store_true", help="emit JSON summaries")
    parser.add_argument(
        "--keys",
        nargs="*",
        default=None,
        metavar="SUBSTRING",
        help="dump decoded key/value rows, optionally filtered by substring",
    )
    parser.add_argument(
        "--verify-md5",
        action="store_true",
        help="when analyzing a .lib directory, verify checksum.dat MD5 entries",
    )
    args = parser.parse_args(argv)

    json_results = []
    for path in args.paths:
        try:
            if path.is_dir() and (path / "instruments").is_dir() and args.keys is None:
                json_results.append(analyze_library(path, args))
            else:
                summaries = analyze_path(path, args)
                json_results.extend(summaries)
        except (OSError, ParseError, zlib.error) as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 1

    if args.json and json_results:
        if len(json_results) == 1:
            print(json.dumps(json_results[0], indent=2, sort_keys=True))
        else:
            print(json.dumps(json_results, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
