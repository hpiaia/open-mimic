#!/usr/bin/env python3
"""Prepare Pearl boot files and a Buildroot boot script for the Mimic SD."""

from __future__ import annotations

import shutil
import struct
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
BOOTLOADER = REPO / "platform/boot"
DRIVER = REPO / "platform/drivers/prebuilt"
KERNEL = REPO / "platform/kernel/pearl-4.1.13"


def uboot_script(commands: bytes) -> bytes:
    payload = struct.pack(">II", len(commands), 0) + commands
    fields = (
        0x27051956, 0, 0, len(payload), 0, 0, zlib.crc32(payload),
        5, 2, 6, 0, b"Open Mimic Buildroot".ljust(32, b"\0"),
    )
    header = struct.pack(">7I4B32s", *fields)
    header = header[:4] + struct.pack(">I", zlib.crc32(header)) + header[8:]
    return header + payload


def build(output: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)
    for source, target in (
        (BOOTLOADER / "MLO", "MLO"),
        (BOOTLOADER / "u-boot.img", "u-boot.img"),
        (KERNEL / "zImage", "PEARLKRN.BIN"),
        (KERNEL / "am5718-edrums-v3.dtb", "PEARL571.DTB"),
        (DRIVER / "zt9101_ztopmac_usb.ko", "ZT9101.KO"),
        (DRIVER / "ZT9101.FW", "ZT9101.FW"),
        (DRIVER / "ZT9101V3.FW", "ZT9101V3.FW"),
        (DRIVER / "WIFI.CFG", "WIFI.CFG"),
    ):
        shutil.copy2(source, output / target)
    commands = b"""echo Open Mimic Buildroot
mmc dev 0
mmc rescan
if load mmc 0:1 ${loadaddr} PEARLKRN.BIN && load mmc 0:1 ${fdtaddr} PEARL571.DTB; then
  setenv bootargs console=tty0 consoleblank=0 loglevel=4 root=/dev/mmcblk0p2 rootwait rw init=/sbin/init
  bootz ${loadaddr} - ${fdtaddr}
fi
"""
    (output / "bootscr.cmd").write_bytes(commands)
    (output / "bootscr.img").write_bytes(uboot_script(commands))
    (output / "OPENMIMIC.MARKER").write_text("Open Mimic Buildroot boot files\n")


if __name__ == "__main__":
    target = REPO / "build/buildroot-boot"
    build(target)
    print(f"Buildroot boot files: {target}")
