# Buildroot image for the Mimic Pro

The board configuration is in `configs/open_mimic_defconfig`. It targets an
ARM Cortex-A15 hard-float userspace and reuses the original Pearl kernel,
device tree, bootloader and kernel modules. Buildroot 2025.02.19 is pinned in
`scripts/build_buildroot.sh`. Buildroot compiles its own ARM hard-float musl
toolchain using Linux 4.1.13 headers. The container only provides host build
tools; its Debian base is not the target operating system.
The full build runs on a case-sensitive Docker volume named
`open-mimic-buildroot`, because Linux kernel sources include headers whose
names differ only by letter case. Generated images are copied into `build/`.

Build on macOS with Docker:

```sh
./scripts/build_buildroot.sh
```

The generated image is `build/buildroot-image/open-mimic-buildroot.img`.
`scripts/flash_sd_macos.py /dev/diskN` checks a removable Secure Digital
target and its image hash. Add `--write` to flash it.

The SD FAT partition may contain `WPA.CONF` and `AUTHKEYS`, provisioned with
`platform/network/configure_wifi_macos.py`. They are local credentials and
must never be committed. The built-in ZT9101 adapter is the only USB Wi-Fi
model already tested. Bluetooth and other adapters still need hardware tests.

At boot, the init scripts load Pearl USB keyboard modules, mount the original
SSD partitions read-only without replaying their journals, then start Wi-Fi
and key-only SSH when credentials are present. The local `tty1` session starts
Xorg on the original framebuffer and runs the JUCE interface. Touch and keyboard
input use evdev and eudev. Xorg does not listen on TCP; the session uses a private
X authorization cookie. A `NOUI` file on the SD FAT partition bypasses the UI
and opens the local shell. Display failures also return to that shell.
Subsequent kernel warnings remain in `dmesg` without writing over the display.
Interactive shells show the current directory in their prompt.
No original SSD partition is modified by these scripts.

The C++ applications are built with CMake and pinned JUCE 9.0.4 sources. The
container supplies native dependencies for JUCE's `juceaide` build helper;
target libraries and Xorg are cross-compiled by Buildroot. Application targets
use JUCE's official CMake helpers. Browser and OpenGL modules are not required
for the initial software-rendered interface.
