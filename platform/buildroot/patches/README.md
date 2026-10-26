# Upstream source patches

`BR2_GLOBAL_PATCH_DIR` points here. Buildroot applies patches using its standard
`<package>/<version>/*.patch` mechanism before configuration and compilation.
Each correction is tied to the upstream version it was reviewed against.

## Linux headers 4.1.13: evdev time64 ABI

The Pearl ARM32 kernel sends 16-byte `input_event` records. The old UAPI header
embeds libc's `timeval`, making the structure 24 bytes under modern musl.
The patch backports the later Linux timestamp fields and accessors to the
original `include/uapi/linux/input.h`. Normal `headers_install` then exports
the corrected definition before toolchain consumers compile.

The application package compiles a `_Static_assert` for the 16-byte ARM32
layout before its CMake configuration. The kernel running on the Mimic is
unchanged. No extra compatibility package or sysroot-editing script is needed.

Reference: [Linux 5.10 input UAPI](https://github.com/torvalds/linux/blob/v5.10/include/uapi/linux/input.h).

## JUCE 9.0.4: native host-tool bootstrap

The patch clears `PKG_CONFIG_SYSROOT_DIR` beside JUCE's existing environment
resets before building native `juceaide`. This prevents the host compiler from
receiving target include paths. Application builds still use the ARM toolchain.
The patch preserves upstream CRLF context; Git attributes prevent normalization
of patch contents.

## Updating patches

Keep upstream version directories and review patches when upgrading a package.
Buildroot's patch step fails when the expected source context no longer matches.
`scripts/build_buildroot.sh` records a digest of the patch set after a successful
build. A changed digest re-extracts the patched packages and rebuilds their
consumers, avoiding stale Buildroot package stamps in an existing Docker volume.

These patches replace the original `fix_input_header.py` and `fix-host-tools.py`
build hooks. Python remains in use for unrelated build and inspection tools.
