#!/usr/bin/env python3
"""Combine the Buildroot rootfs and Pearl boot files into an SD image."""

from __future__ import annotations

import hashlib
import os
import struct
import subprocess
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
BOOT = REPO / "build/buildroot-boot"
ROOT = REPO / "build/buildroot-output/images/rootfs.ext4"
OUT = REPO / "build/buildroot-image"
BOOT_MIB = 128
ROOT_MIB = 256
SECTOR_SIZE = 512
BOOT_START = 2048
BOOT_SECTORS = BOOT_MIB * 1024 * 1024 // SECTOR_SIZE
ROOT_START = BOOT_START + BOOT_SECTORS
ROOT_SECTORS = ROOT_MIB * 1024 * 1024 // SECTOR_SIZE
TOTAL_SECTORS = ROOT_START + ROOT_SECTORS


def partition(start: int, count: int, kind: int, active: bool) -> bytes:
    return struct.pack(
        "<B3sB3sII", 0x80 if active else 0, b"\0\x02\0", kind,
        b"\0\x02\0", start, count,
    )


def copy_sparse(source: Path, output, offset: int) -> None:
    output.seek(offset)
    with source.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            if block.count(0) == len(block):
                output.seek(len(block), os.SEEK_CUR)
            else:
                output.write(block)


def main() -> None:
    if ROOT.stat().st_size != ROOT_MIB * 1024 * 1024:
        raise RuntimeError("Unexpected rootfs.ext4 size; rebuild the Buildroot rootfs")
    required = (
        "MLO", "u-boot.img", "bootscr.img", "PEARLKRN.BIN",
        "PEARL571.DTB", "ZT9101.KO", "ZT9101.FW", "ZT9101V3.FW",
        "WIFI.CFG", "OPENMIMIC.MARKER",
    )
    for name in required:
        if not (BOOT / name).is_file():
            raise FileNotFoundError(BOOT / name)

    OUT.mkdir(parents=True, exist_ok=True)
    commands = [
        "truncate -s 128M /out/boot.fat",
        "mkfs.vfat -F 32 -n MIMICBOOT /out/boot.fat",
    ]
    for name in required:
        commands.append(f"mcopy -i /out/boot.fat /boot/{name} ::{name}")
    commands.append("mdir -i /out/boot.fat ::")
    subprocess.run([
        "docker", "run", "--rm", "--platform", "linux/arm64",
        "--mount", f"type=bind,src={BOOT},dst=/boot,readonly",
        "--mount", f"type=bind,src={OUT},dst=/out",
        "open-mimic:buildroot-host", "/bin/sh", "-ec", "\n".join(commands),
    ], check=True)
    fat = OUT / "boot.fat"
    if fat.stat().st_size != BOOT_MIB * 1024 * 1024:
        raise RuntimeError("Unexpected FAT image size")
    image = OUT / "open-mimic-buildroot.img"
    mbr = bytearray(512)
    mbr[446:462] = partition(BOOT_START, BOOT_SECTORS, 0x0C, True)
    mbr[462:478] = partition(ROOT_START, ROOT_SECTORS, 0x83, False)
    mbr[510:512] = b"\x55\xaa"
    with image.open("wb") as output:
        output.truncate(TOTAL_SECTORS * SECTOR_SIZE)
        output.write(mbr)
        copy_sparse(fat, output, BOOT_START * SECTOR_SIZE)
        copy_sparse(ROOT, output, ROOT_START * SECTOR_SIZE)
        output.flush()
        os.fsync(output.fileno())
    digest = hashlib.sha256()
    with image.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    (OUT / "open-mimic-buildroot.sha256").write_text(
        f"{digest.hexdigest()}  {image.name}\n"
    )
    print(f"SD image: {image} ({image.stat().st_size} bytes)")
    print(f"SHA-256: {digest.hexdigest()}")


if __name__ == "__main__":
    main()
