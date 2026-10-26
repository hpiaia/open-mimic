#!/usr/bin/env python3
"""Stop pad collection, wait for writes, and copy a verified dataset to the Mac.

Creates a new local directory and never removes captures from the device.
Use a private destination outside the source repository.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess
import time


def remote(host: str, command: str) -> str:
    return subprocess.check_output(["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5",
                                    host, command], text=True, timeout=15)


def checksums(host: str, directory: str) -> dict[str, str]:
    command = f"find {shlex.quote(directory)} -type f -exec sha256sum {{}} \\;"
    result = {}
    for line in remote(host, command).splitlines():
        digest, name = line.split(maxsplit=1)
        relative = Path(name).relative_to(directory)
        if not re.fullmatch(r"[0-9a-f]{64}", digest) or relative.is_absolute() or ".." in relative.parts:
            raise ValueError("invalid remote checksum record")
        result[str(relative)] = digest
    return result


def digest(path: Path) -> str:
    checksum = hashlib.sha256()
    with path.open("rb") as source:
        while block := source.read(1024 * 1024):
            checksum.update(block)
    return checksum.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output_directory", type=Path)
    parser.add_argument("--host", default="root@192.168.31.215")
    args = parser.parse_args()
    try:
        if not re.fullmatch(r"(?:[A-Za-z0-9_][A-Za-z0-9_.-]*@)?[A-Za-z0-9_][A-Za-z0-9_.-]*", args.host):
            raise ValueError("use a hostname or IPv4 address, optionally prefixed with a username")
        if args.output_directory.exists():
            raise ValueError("choose a new output directory; existing data is never overwritten")
        remote(args.host, "mimicd pad-command stop")
        state = None
        for _ in range(50):
            state = json.loads(remote(args.host, "cat /run/mimic-pads/status.json"))
            if state.get("running") and state.get("state") != "capturing" and state.get("writerPending") == 0:
                break
            time.sleep(0.2)
        else:
            raise ValueError("pad collection or pending writes did not stop")
        directory = state.get("dataDirectory", "")
        if (not re.fullmatch(r"/(?:[A-Za-z0-9_.-]+/)*[A-Za-z0-9_.-]+", directory)
                or any(part in (".", "..") for part in directory.split("/"))
                or not (directory == "/var/lib/mimic/pads" or directory.startswith("/mnt/sd/")
                        or directory.startswith("/tmp/mimic-pad-"))):
            raise ValueError("service reported an unsupported data directory")
        before = checksums(args.host, directory)
        args.output_directory.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["scp", "-O", "-r", f"{args.host}:{directory}", str(args.output_directory.resolve())],
                       check=True, timeout=120)
        after = checksums(args.host, directory)
        if before != after:
            raise ValueError("dataset changed during transfer; local copy retained but not verified")
        for name, expected in before.items():
            copied = args.output_directory / name
            if copied.is_symlink() or not copied.is_file() or digest(copied) != expected:
                raise ValueError(f"copied data differs: {name}; partial download retained")
        report = {"format": "open-mimic-pad-download-v1", "host": args.host,
                  "source": directory, "utc": datetime.now(timezone.utc).isoformat(),
                  "verified_files": before, "device_captures_removed": False}
        with (args.output_directory / "download.json").open("x") as output:
            output.write(json.dumps(report, indent=2) + "\n")
        print(f"Verified {len(before)} files in {args.output_directory}")
        print("Original captures remain on the device; collection is stopped.")
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, f"error: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
