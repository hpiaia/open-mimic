# Linux 4.1 evdev UAPI and musl time64

The Pearl ARM32 kernel sends 16-byte `input_event` records. Its original Linux
4.1 headers embed `struct timeval`, which becomes 16 bytes under modern musl.
Unmodified headers therefore describe a 24-byte record and cause evdev readers
to misinterpret real input. The Rust prototype used explicit 16-byte records.

The Buildroot compatibility package backports the time64-aware event layout
from [Linux 5.10](https://github.com/torvalds/linux/blob/v5.10/include/uapi/linux/input.h)
to the target sysroot. It preserves the 32-bit kernel timestamp representation
and exposes `input_event_sec` / `input_event_usec` aliases for consumers. It does
not change the running kernel or libc's normal time representation.

libevdev, mtdev, Xorg and its evdev driver depend on this staging fix. Existing
build trees must rebuild those packages because their event structure ABI
changes. The integration verifies `sizeof(struct input_event) == 16` on ARM32.
