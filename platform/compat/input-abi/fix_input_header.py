#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Backport the time64-safe evdev UAPI layout to the Linux 4.1 headers."""
import sys
from pathlib import Path

path = Path(sys.argv[1])
source = path.read_text()
if "Open Mimic: time64-safe evdev UAPI" in source:
    raise SystemExit(0)
old = "\tstruct timeval time;\n\t__u16 type;"
if source.count(old) != 1:
    raise SystemExit("Unexpected Linux input_event definition; review the ABI backport")
new = """/* Open Mimic: time64-safe evdev UAPI, following Linux 5.10 input.h. */
#if (__BITS_PER_LONG != 32 || !defined(__USE_TIME_BITS64))
\tstruct timeval time;
#define input_event_sec time.tv_sec
#define input_event_usec time.tv_usec
#else
\t__kernel_ulong_t __sec;
\t__kernel_ulong_t __usec;
#define input_event_sec __sec
#define input_event_usec __usec
#endif
\t__u16 type;"""
source = source.replace("#include <linux/types.h>", "#include <linux/types.h>\n#include <asm/bitsperlong.h>")
path.write_text(source.replace(old, new))
