#!/usr/bin/env python3
"""Format project C++ sources, or reject formatting drift with --check."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="check without modifying files")
    args = parser.parse_args()

    executable = os.environ.get("CLANG_FORMAT") or shutil.which("clang-format")
    if not executable:
        parser.error("Install clang-format 18+ or set CLANG_FORMAT to its executable")

    project = Path(__file__).resolve().parents[1]
    directories = ("common", "mimicd/src", "mimic/src", "tests", "tools")
    sources = sorted(
        path
        for directory in directories
        for path in (project / "software" / directory).rglob("*")
        if path.suffix in {".cpp", ".h", ".hpp"}
    )
    options = ["--dry-run", "--Werror"] if args.check else ["-i"]
    result = subprocess.run([executable, "--style=file", *options, *map(str, sources)], check=False)
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
