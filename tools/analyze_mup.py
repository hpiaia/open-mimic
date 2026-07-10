#!/usr/bin/env python3
"""Analyze Pearl Mimic Pro .mup software update files.

The script treats firmware as input data:
- parse the observed Mimic software-update container
- verify the CRC32C checksum used by the updater
- optionally extract the decompressed ARM ELF
- optionally write a Markdown inventory for reverse-engineering notes
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import struct
import subprocess
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path


MAGIC = b"mimic_software_update"
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


def c_string(data: bytes, offset: int) -> tuple[bytes, int]:
    end = data.find(b"\0", offset)
    if end < 0:
        raise ValueError("unterminated string in update header")
    return data[offset:end], end + 1


def printable_strings(data: bytes, min_len: int = 4) -> list[str]:
    strings: list[str] = []
    current = bytearray()
    for byte in data:
        if byte in (9, 10, 13) or 32 <= byte <= 126:
            current.append(byte)
        else:
            if len(current) >= min_len:
                strings.append(current.decode("utf-8", "replace"))
            current.clear()
    if len(current) >= min_len:
        strings.append(current.decode("utf-8", "replace"))
    return strings


@dataclass
class MupInfo:
    path: Path
    magic: str
    format_version: int
    expected_crc32c: int
    metadata_len: int
    metadata: bytes
    expected_payload_size: int
    compressed_offset: int
    compressed_size: int
    payload: bytes

    @property
    def actual_crc32c(self) -> int:
        return crc32c(self.payload)

    @property
    def crc_ok(self) -> bool:
        return self.actual_crc32c == self.expected_crc32c

    @property
    def size_ok(self) -> bool:
        return len(self.payload) == self.expected_payload_size


def parse_mup(path: Path) -> MupInfo:
    data = path.read_bytes()
    magic, offset = c_string(data, 0)
    if magic != MAGIC:
        raise ValueError(f"unexpected update magic: {magic!r}")

    if offset + 16 > len(data):
        raise ValueError("truncated update header")

    version, expected_crc, metadata_len = struct.unpack_from("<III", data, offset)
    offset += 12

    if metadata_len > 4096:
        raise ValueError(f"unexpectedly large metadata block: {metadata_len}")

    metadata = data[offset : offset + metadata_len]
    offset += metadata_len
    expected_payload_size = struct.unpack_from("<I", data, offset)[0]
    offset += 4

    payload = zlib.decompress(data[offset:])
    return MupInfo(
        path=path,
        magic=magic.decode("ascii"),
        format_version=version,
        expected_crc32c=expected_crc,
        metadata_len=metadata_len,
        metadata=metadata,
        expected_payload_size=expected_payload_size,
        compressed_offset=offset,
        compressed_size=len(data) - offset,
        payload=payload,
    )


@dataclass
class Section:
    name: str
    sh_type: int
    flags: int
    addr: int
    offset: int
    size: int
    link: int
    info: int
    align: int
    entsize: int


class Elf32:
    def __init__(self, data: bytes) -> None:
        self.data = data
        if data[:4] != b"\x7fELF":
            raise ValueError("payload is not an ELF file")
        if data[4] != 1 or data[5] != 1:
            raise ValueError("expected ELF32 little-endian")

        header = struct.unpack_from("<16sHHIIIIIHHHHHH", data, 0)
        self.machine = header[2]
        self.entry = header[4]
        self.program_header_offset = header[5]
        self.section_header_offset = header[6]
        self.flags = header[7]
        self.program_header_entry_size = header[9]
        self.program_header_count = header[10]
        self.section_header_entry_size = header[11]
        self.section_header_count = header[12]
        self.section_name_index = header[13]
        self.sections = self._read_sections()

    def _read_sections(self) -> list[Section]:
        raw_sections = []
        for idx in range(self.section_header_count):
            offset = self.section_header_offset + idx * self.section_header_entry_size
            raw_sections.append(struct.unpack_from("<IIIIIIIIII", self.data, offset))

        shstr = raw_sections[self.section_name_index]
        names = self.data[shstr[4] : shstr[4] + shstr[5]]
        sections: list[Section] = []
        for raw in raw_sections:
            name_offset = raw[0]
            end = names.find(b"\0", name_offset)
            name = names[name_offset:end].decode("ascii", "replace") if end >= 0 else ""
            sections.append(
                Section(
                    name=name,
                    sh_type=raw[1],
                    flags=raw[2],
                    addr=raw[3],
                    offset=raw[4],
                    size=raw[5],
                    link=raw[6],
                    info=raw[7],
                    align=raw[8],
                    entsize=raw[9],
                )
            )
        return sections

    def section(self, name: str) -> Section | None:
        return next((section for section in self.sections if section.name == name), None)

    def section_data(self, section: Section) -> bytes:
        return self.data[section.offset : section.offset + section.size]

    def dynamic_needed(self) -> list[str]:
        dynamic = self.section(".dynamic")
        dynstr = self.section(".dynstr")
        if not dynamic or not dynstr:
            return []
        dynstr_data = self.section_data(dynstr)
        needed: list[str] = []
        for offset in range(dynamic.offset, dynamic.offset + dynamic.size, 8):
            tag, value = struct.unpack_from("<iI", self.data, offset)
            if tag == 0:
                break
            if tag == 1:
                end = dynstr_data.find(b"\0", value)
                needed.append(dynstr_data[value:end].decode("ascii", "replace"))
        return needed

    def symbols(self) -> list[tuple[int, int, str]]:
        symtab = self.section(".symtab")
        strtab = self.section(".strtab")
        if not symtab or not strtab or symtab.entsize == 0:
            return []
        strings = self.section_data(strtab)
        symbols: list[tuple[int, int, str]] = []
        for offset in range(symtab.offset, symtab.offset + symtab.size, symtab.entsize):
            name_off, value, size, _info, _other, _shndx = struct.unpack_from("<IIIBBH", self.data, offset)
            if not name_off:
                continue
            end = strings.find(b"\0", name_off)
            name = strings[name_off:end].decode("utf-8", "replace")
            symbols.append((value, size, name))
        return symbols


def demangle(names: list[str]) -> dict[str, str]:
    if not names or not shutil.which("c++filt"):
        return {name: name for name in names}
    proc = subprocess.run(
        ["c++filt"],
        input="\n".join(names),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    output = proc.stdout.splitlines()
    if len(output) != len(names):
        return {name: name for name in names}
    return dict(zip(names, output))


def interesting_inventory(payload: bytes) -> dict[str, object]:
    elf = Elf32(payload)
    strings = printable_strings(payload, min_len=4)
    all_symbols = elf.symbols()
    symbol_names = [name for _, _, name in all_symbols]
    demangled = demangle(symbol_names)

    source_files = sorted(
        {
            s
            for s in strings
            if re.search(r"(^|/|[A-Za-z0-9_+-])([A-Za-z0-9_+-]+\.(cpp|c|h|hpp|S))$", s)
            and not s.startswith("_Z")
            and "\n" not in s
            and "\r" not in s
        }
    )

    path_like = sorted({s for s in strings if s.startswith(("/", "./", "~/.config"))})
    commands = sorted(
        {
            s
            for s in strings
            if s.startswith(("umount ", "parted ", "mkdosfs", "mkfs.", "sync", "2>/"))
            or " /dev/" in s
        }
    )
    devices = sorted({s for s in path_like if s.startswith(("/dev/", "/sys/", "/proc/"))})
    mounts = sorted({s for s in path_like if s.startswith("/mnt/")})
    update_strings = sorted(
        {
            s
            for s in strings
            if re.search(r"update|mimic_software|mimic_pro_update|checksum|kexec|restart", s, re.I)
        }
    )
    debug_strings = sorted(
        {
            s
            for s in strings
            if re.search(r"debug|test|capture|dump|ssd|trigger|midi|uart|ext port|gpio", s, re.I)
        }
    )

    wanted_symbol_patterns = re.compile(
        r"(Updater|Update|DirectoryManager|DebugInfo|ExtPort|HWTest|Trigger|Trig|Midi|Audio|Regulator|"
        r"DiskStreamer|Sampler|DataStorage|Settings|FileChecker|FileUtils|InputRegulator|XTalk|"
        r"Capture|Encoder|Record|Player|Instrument|Mixer|EdrumsApp)"
    )
    notable_symbols = sorted(
        {
            demangled[name]
            for _value, _size, name in all_symbols
            if wanted_symbol_patterns.search(demangled[name])
        }
    )

    class_counts: dict[str, int] = {}
    for name in notable_symbols:
        match = re.match(r"([A-Za-z_][A-Za-z0-9_]*)::", name)
        if match:
            class_counts[match.group(1)] = class_counts.get(match.group(1), 0) + 1

    return {
        "entry": elf.entry,
        "machine": elf.machine,
        "sections": [
            {
                "name": section.name,
                "addr": section.addr,
                "offset": section.offset,
                "size": section.size,
            }
            for section in elf.sections
            if section.name
        ],
        "dynamic_needed": elf.dynamic_needed(),
        "source_files": source_files,
        "devices": devices,
        "mounts": mounts,
        "commands": commands,
        "update_strings": update_strings,
        "debug_strings": debug_strings,
        "notable_symbols": notable_symbols,
        "class_counts": sorted(class_counts.items(), key=lambda item: (-item[1], item[0])),
    }


def write_markdown(path: Path, mup: MupInfo, inventory: dict[str, object]) -> None:
    metadata_ascii = "".join(chr(b) if 32 <= b <= 126 else "." for b in mup.metadata)
    sections = inventory["sections"]
    assert isinstance(sections, list)

    def bullet(items: object, limit: int | None = None) -> str:
        assert isinstance(items, list)
        selected = items[:limit] if limit else items
        if not selected:
            return "- none\n"
        return "".join(f"- `{item}`\n" for item in selected)

    def table_sections() -> str:
        lines = ["| Section | VMA | File Offset | Size |", "| --- | ---: | ---: | ---: |"]
        for section in sections:
            assert isinstance(section, dict)
            lines.append(
                f"| `{section['name']}` | `0x{section['addr']:08x}` | "
                f"`0x{section['offset']:08x}` | `0x{section['size']:x}` |"
            )
        return "\n".join(lines) + "\n"

    dynamic_needed = inventory["dynamic_needed"]
    class_counts = inventory["class_counts"]
    assert isinstance(dynamic_needed, list)
    assert isinstance(class_counts, list)

    report = f"""# Mimic Pro Firmware Analysis

Input: `{mup.path}`

## MUP Container

- Magic: `{mup.magic}`
- Format version: `{mup.format_version}`
- Header metadata length: `{mup.metadata_len}` bytes
- Expected payload size: `{mup.expected_payload_size}` bytes
- Actual payload size: `{len(mup.payload)}` bytes
- Compressed payload offset: `0x{mup.compressed_offset:x}`
- Compressed payload size: `{mup.compressed_size}` bytes
- Expected CRC32C: `0x{mup.expected_crc32c:08x}`
- Actual CRC32C: `0x{mup.actual_crc32c:08x}`
- CRC valid: `{mup.crc_ok}`
- Size valid: `{mup.size_ok}`

The checksum uses CRC32C/Castagnoli, not legacy IEEE CRC-32. The 100-byte metadata block is read by the updater but does not appear to be used by the software-update extraction path.

Metadata block:

```text
{metadata_ascii}
```

```text
{mup.metadata.hex()}
```

## ELF Summary

- ELF entry: `0x{inventory['entry']:08x}`
- ELF machine: `{inventory['machine']}` (`40` means ARM)
- Dynamic dependencies:
{bullet(dynamic_needed)}
## Sections

{table_sections()}
## Source File Names

These names are embedded in the binary symbol/string tables and are useful for reconstructing module boundaries.

{bullet(inventory['source_files'])}
## Device And Kernel Paths

{bullet(inventory['devices'])}
## Mount Points

{bullet(inventory['mounts'])}
## Shell Commands

{bullet(inventory['commands'])}
## Update-Related Strings

{bullet(inventory['update_strings'])}
## Debug/Test Strings

{bullet(inventory['debug_strings'], limit=250)}
## Most Represented Notable Classes

"""
    for class_name, count in class_counts[:120]:
        report += f"- `{class_name}`: {count} symbols\n"

    report += "\n## Notable Symbols\n\n"
    report += bullet(inventory["notable_symbols"], limit=500)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(report, encoding="utf-8")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("mup", type=Path)
    parser.add_argument("--extract-elf", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)

    mup = parse_mup(args.mup)
    inventory = interesting_inventory(mup.payload)

    if args.extract_elf:
        args.extract_elf.parent.mkdir(parents=True, exist_ok=True)
        args.extract_elf.write_bytes(mup.payload)

    if args.report:
        write_markdown(args.report, mup, inventory)

    summary = {
        "path": str(mup.path),
        "magic": mup.magic,
        "format_version": mup.format_version,
        "metadata_len": mup.metadata_len,
        "compressed_offset": mup.compressed_offset,
        "compressed_size": mup.compressed_size,
        "expected_payload_size": mup.expected_payload_size,
        "actual_payload_size": len(mup.payload),
        "expected_crc32c": f"0x{mup.expected_crc32c:08x}",
        "actual_crc32c": f"0x{mup.actual_crc32c:08x}",
        "crc_ok": mup.crc_ok,
        "size_ok": mup.size_ok,
        "dynamic_needed": inventory["dynamic_needed"],
    }
    if args.json:
        print(json.dumps(summary, indent=2))
    else:
        for key, value in summary.items():
            print(f"{key}: {value}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
