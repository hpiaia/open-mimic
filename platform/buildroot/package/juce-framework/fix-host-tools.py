#!/usr/bin/env python3
"""Keep Buildroot's target sysroot out of JUCE's native juceaide bootstrap."""
import sys
from pathlib import Path

path = Path(sys.argv[1]) / "extras/Build/juceaide/CMakeLists.txt"
source = path.read_text()
marker = "        unset(ENV{PKG_CONFIG_LIBDIR})"
if source.count(marker) != 1:
    raise SystemExit("JUCE host bootstrap changed; review the Buildroot integration")
path.write_text(source.replace(marker, marker + "\n        unset(ENV{PKG_CONFIG_SYSROOT_DIR})"))
