#!/usr/bin/env python3
"""Verify an SD target and optionally flash the Buildroot image on macOS."""

from __future__ import annotations

import argparse
import hashlib
import plistlib
import re
import subprocess
import sys
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
IMAGE = REPO / "build/buildroot-image/open-mimic-buildroot.img"
HASH = REPO / "build/buildroot-image/open-mimic-buildroot.sha256"


def disk_info(disk: str) -> dict:
    return plistlib.loads(subprocess.check_output(["diskutil", "info", "-plist", disk]))


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("disk", help="whole disk, for example /dev/disk4")
    parser.add_argument("--write", action="store_true", help="flash after typed confirmation")
    args = parser.parse_args()
    if not re.fullmatch(r"/dev/disk[0-9]+", args.disk):
        raise ValueError("Pass a whole disk path such as /dev/disk4")
    if not IMAGE.is_file() or not HASH.is_file():
        raise FileNotFoundError("Build the SD image first with scripts/build_sd_image.py")
    expected = HASH.read_text().split()[0]
    actual = sha256(IMAGE)
    if expected != actual:
        raise ValueError("SD image SHA-256 does not match its manifest")
    info = disk_info(args.disk)
    if not (
        info.get("BusProtocol") == "Secure Digital"
        and info.get("RemovableMedia") is True
        and info.get("OSInternalMedia") is False
        and info.get("WritableMedia") is True
        and info.get("WholeDisk") is True
        and info.get("TotalSize", 0) >= IMAGE.stat().st_size
    ):
        raise ValueError("Target must be a writable removable Secure Digital whole disk of sufficient size")
    identifier = info["DeviceIdentifier"]
    print(f"Image: {IMAGE} ({IMAGE.stat().st_size} bytes, SHA-256 {actual})")
    print(f"Target: {args.disk} ({info['TotalSize']} bytes, {info.get('MediaName', 'SD card')})")
    if not args.write:
        print("Dry run complete. Add --write to flash this SD card.")
        return 0
    if input(f"All data on {args.disk} will be overwritten. Type FLASH {identifier}: ") != f"FLASH {identifier}":
        print("Cancelled.")
        return 1
    subprocess.run(["diskutil", "unmountDisk", args.disk], check=True)
    subprocess.run([
        "sudo", "dd", f"if={IMAGE}", f"of=/dev/r{identifier}", "bs=1m",
    ], check=True)
    subprocess.run(["sync"], check=True)
    subprocess.run(["diskutil", "eject", args.disk], check=True)
    print("SD card flashed and ejected.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        raise SystemExit(1)
