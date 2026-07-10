#!/usr/bin/env python3
"""Analyze recovered Pearl Mimic Pro .mos OS update files.

This parser is based on the 1.4.18 Mimic application binary:
- CUpdaterDataStorage::saveOSUpdateToFile
- CUpdaterDataStorage::processOSUpdate
- CUpdaterDataStorage::readChunk
- CUpdaterDataStorage::addFileToOSUpdate

It treats .mos files as untrusted data. It lists embedded shell commands but never
executes them.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path


MAGIC = b"mimic_software_update"
CHUNK_MAGIC = b"mimic_software_update_chunk"
CRC32C_POLY_REVERSED = 0x82F63B78


def crc32c(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ CRC32C_POLY_REVERSED
            else:
                crc >>= 1
            crc &= 0xFFFFFFFF
    return crc ^ 0xFFFFFFFF


def read_int(data: bytes, offset: int) -> tuple[int, int]:
    if offset + 4 > len(data):
        raise ValueError("truncated int")
    return struct.unpack_from("<i", data, offset)[0], offset + 4


def read_uint(data: bytes, offset: int) -> tuple[int, int]:
    if offset + 4 > len(data):
        raise ValueError("truncated uint")
    return struct.unpack_from("<I", data, offset)[0], offset + 4


def read_c_string(data: bytes, offset: int) -> tuple[bytes, int]:
    end = data.find(b"\0", offset)
    if end < 0:
        raise ValueError("unterminated string")
    return data[offset:end], end + 1


def decompress_stream(data: bytes) -> bytes:
    attempts = (
        ("zlib", zlib.MAX_WBITS),
        ("gzip", zlib.MAX_WBITS | 16),
        ("raw-deflate", -zlib.MAX_WBITS),
    )
    last_error: Exception | None = None
    for _name, wbits in attempts:
        try:
            return zlib.decompress(data, wbits)
        except zlib.error as exc:
            last_error = exc
    raise ValueError(f"unsupported compressed stream: {last_error}")


def clean_string(data: bytes) -> str:
    if data.endswith(b"\0"):
        data = data[:-1]
    return data.decode("utf-8", "replace")


def safe_output_name(path_hint: str, fallback: str) -> str:
    basename = Path(path_hint.replace("\\", "/")).name
    if not basename:
        basename = fallback
    return re.sub(r"[^A-Za-z0-9._+-]+", "_", basename)


@dataclass
class Record:
    tag: int
    data: bytes

    @property
    def length(self) -> int:
        return len(self.data)

    @property
    def role(self) -> str:
        return {
            1: "target_path",
            2: "command_or_label",
            3: "file_payload",
        }.get(self.tag, "unknown")

    def preview(self) -> str | None:
        if self.tag in (1, 2):
            return clean_string(self.data)
        return None


@dataclass
class Chunk:
    index: int
    magic_tag: int
    magic_data: bytes
    top_tag: int
    records: list[Record]

    @property
    def magic_ok(self) -> bool:
        return self.magic_tag == 0 and self.magic_data.rstrip(b"\0") == CHUNK_MAGIC

    @property
    def target_path(self) -> str | None:
        for record in self.records:
            if record.tag == 1:
                return clean_string(record.data)
        return None

    @property
    def command_or_label(self) -> str | None:
        for record in self.records:
            if record.tag == 2:
                return clean_string(record.data)
        return None

    @property
    def payload_records(self) -> list[Record]:
        return [record for record in self.records if record.tag == 3]


@dataclass
class MosInfo:
    path: Path
    expected_crc32c: int
    actual_crc32c: int
    magic: str
    version: int
    chunk_count: int
    compressed_offset: int
    compressed_size: int
    stream: bytes
    chunks: list[Chunk]

    @property
    def crc_ok(self) -> bool:
        return self.expected_crc32c == self.actual_crc32c

    @property
    def chunk_count_ok(self) -> bool:
        return self.chunk_count == len(self.chunks)


def parse_chunks(stream: bytes, chunk_count: int) -> list[Chunk]:
    offset = 0
    chunks: list[Chunk] = []
    for index in range(chunk_count):
        magic_tag, offset = read_int(stream, offset)
        magic_len, offset = read_int(stream, offset)
        if magic_len < 0 or magic_len > 1024 * 1024:
            raise ValueError(f"chunk {index}: invalid magic length {magic_len}")
        magic_data = stream[offset : offset + magic_len]
        if len(magic_data) != magic_len:
            raise ValueError(f"chunk {index}: truncated magic data")
        offset += magic_len

        top_tag, offset = read_int(stream, offset)
        record_count, offset = read_int(stream, offset)
        if record_count < 0 or record_count > 1024:
            raise ValueError(f"chunk {index}: invalid record count {record_count}")

        records: list[Record] = []
        for record_index in range(record_count):
            tag, offset = read_int(stream, offset)
            length, offset = read_int(stream, offset)
            if length < 0 or length > 128 * 1024 * 1024:
                raise ValueError(f"chunk {index} record {record_index}: invalid length {length}")
            record_data = stream[offset : offset + length]
            if len(record_data) != length:
                raise ValueError(f"chunk {index} record {record_index}: truncated data")
            offset += length
            records.append(Record(tag=tag, data=record_data))

        chunks.append(
            Chunk(
                index=index,
                magic_tag=magic_tag,
                magic_data=magic_data,
                top_tag=top_tag,
                records=records,
            )
        )

    if offset != len(stream):
        raise ValueError(f"{len(stream) - offset} trailing byte(s) after declared chunks")
    return chunks


def parse_mos(path: Path) -> MosInfo:
    data = path.read_bytes()
    if len(data) < 16:
        raise ValueError("file is too small for a .mos update")

    expected_crc, offset = read_uint(data, 0)
    actual_crc = crc32c(data[4:])
    magic_bytes, offset = read_c_string(data, offset)
    if magic_bytes != MAGIC:
        raise ValueError(f"unexpected update magic: {magic_bytes!r}")

    version, offset = read_int(data, offset)
    chunk_count, offset = read_int(data, offset)
    if chunk_count < 0:
        raise ValueError(f"invalid negative chunk count: {chunk_count}")

    compressed = data[offset:]
    stream = decompress_stream(compressed)
    return MosInfo(
        path=path,
        expected_crc32c=expected_crc,
        actual_crc32c=actual_crc,
        magic=magic_bytes.decode("ascii"),
        version=version,
        chunk_count=chunk_count,
        compressed_offset=offset,
        compressed_size=len(compressed),
        stream=stream,
        chunks=parse_chunks(stream, chunk_count),
    )


def mos_summary(mos: MosInfo) -> dict[str, object]:
    chunks: list[dict[str, object]] = []
    for chunk in mos.chunks:
        chunk_info: dict[str, object] = {
            "index": chunk.index,
            "magic_ok": chunk.magic_ok,
            "top_tag": chunk.top_tag,
            "target_path": chunk.target_path,
            "command_or_label": chunk.command_or_label,
            "records": [],
        }
        records: list[dict[str, object]] = []
        for record in chunk.records:
            record_info: dict[str, object] = {
                "tag": record.tag,
                "role": record.role,
                "length": record.length,
                "sha256": hashlib.sha256(record.data).hexdigest(),
            }
            preview = record.preview()
            if preview is not None:
                record_info["preview"] = preview
            records.append(record_info)
        chunk_info["records"] = records
        chunks.append(chunk_info)

    return {
        "path": str(mos.path),
        "expected_crc32c": f"0x{mos.expected_crc32c:08x}",
        "actual_crc32c": f"0x{mos.actual_crc32c:08x}",
        "crc_ok": mos.crc_ok,
        "magic": mos.magic,
        "version": mos.version,
        "chunk_count": mos.chunk_count,
        "parsed_chunks": len(mos.chunks),
        "chunk_count_ok": mos.chunk_count_ok,
        "compressed_offset": mos.compressed_offset,
        "compressed_size": mos.compressed_size,
        "decompressed_stream_size": len(mos.stream),
        "chunks": chunks,
    }


def extract_payloads(mos: MosInfo, output_dir: Path) -> list[dict[str, str]]:
    output_dir.mkdir(parents=True, exist_ok=True)
    written: list[dict[str, str]] = []
    for chunk in mos.chunks:
        target = chunk.target_path or ""
        for payload_index, record in enumerate(chunk.payload_records):
            name = safe_output_name(target, f"chunk-{chunk.index}-payload-{payload_index}.bin")
            if len(chunk.payload_records) > 1:
                name = f"{payload_index}-{name}"
            out = output_dir / f"chunk-{chunk.index}-{name}"
            out.write_bytes(record.data)
            written.append(
                {
                    "chunk": str(chunk.index),
                    "target_path": target,
                    "output": str(out),
                    "sha256": hashlib.sha256(record.data).hexdigest(),
                }
            )
    return written


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("mos", type=Path)
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--extract-dir", type=Path)
    args = parser.parse_args(argv)

    mos = parse_mos(args.mos)
    summary = mos_summary(mos)
    if args.extract_dir:
        summary["extracted_payloads"] = extract_payloads(mos, args.extract_dir)

    if args.json:
        print(json.dumps(summary, indent=2))
    else:
        print(f"path: {summary['path']}")
        print(f"crc_ok: {summary['crc_ok']} ({summary['actual_crc32c']})")
        print(f"magic: {summary['magic']}")
        print(f"version: {summary['version']}")
        print(f"chunks: {summary['parsed_chunks']} / declared {summary['chunk_count']}")
        print(f"compressed_offset: 0x{mos.compressed_offset:x}")
        for chunk in mos.chunks:
            print(f"\nchunk {chunk.index}: top_tag={chunk.top_tag} magic_ok={chunk.magic_ok}")
            if chunk.target_path:
                print(f"  target_path: {chunk.target_path}")
            if chunk.command_or_label:
                print(f"  command_or_label: {chunk.command_or_label}")
            for record in chunk.records:
                preview = record.preview()
                suffix = f" preview={preview!r}" if preview is not None else ""
                print(f"  record tag={record.tag} role={record.role} len={record.length}{suffix}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
