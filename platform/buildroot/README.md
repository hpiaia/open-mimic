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
and key-only SSH when credentials are present. The LCD shell uses `tty1` and
starts without a login prompt after restoring normal terminal settings and
clearing the inherited Pearl splash. Subsequent kernel warnings remain in
`dmesg` without writing over the LCD prompt. Interactive shells show the
current directory in their prompt.
No original SSD partition is modified by these scripts.
