# Mimic UI hardware validation

Date: 2026-10-26. The first Rust interface was built with the existing Buildroot
ARMv7 musl toolchain and installed over SSH on the running SD system.

- Visible framebuffer: 1024 × 600, 32 bits per pixel, stride 4096 bytes.
  Virtual framebuffer height is 768; it must not be used as the LCD height.
- Color channels reported by `fbset`: red 8/16, green 8/8, blue 8/0, alpha 8/24.
- Touchscreen: `Goodix Capacitive TouchScreen`, `/dev/input/event0`, with
  multitouch X range 0–1024 and Y range 0–600.
- The existing kernel already exposes the touchscreen; no replacement driver,
  kernel rebuild or device-tree change was required.
- The UI opens the framebuffer and evdev device successfully. The live log
  recorded 25 initial touch-down events, including positions inside all four
  button regions. The owner subsequently confirmed physical alignment.
- A framebuffer capture showed the home screen, connected SSID `LYRA`, current
  IPv4 address `192.168.31.215/24`, buttons and the touch status footer.
- Wi-Fi status uses the existing `/tmp/wpa_supplicant` control socket.
- Local unit tests for axis scaling and Clippy passed. The ARM binary runs on
  the original Pearl kernel. The Buildroot package includes the application.

Only the SD userspace was updated. The SSD stays mounted read-only. The
calibration, trigger-monitor and audio buttons remain placeholders. No ML,
trigger processing or audio engine was introduced by this change.

The console wrapper starts the interface at boot. A `NOUI` file on the FAT
partition bypasses it; application errors return to the local shell. The first
live session was started over SSH without rebooting the instrument.

## C++ / JUCE migration

The owner confirmed the original Rust prototype's display and touch alignment.
The application was then migrated to C++17 / JUCE 9.0.4, running on Xorg fbdev
with evdev input. A framebuffer capture confirmed the JUCE layout and live
Wi-Fi status. Both ADC layouts and the offline JUCE DSP check passed on-device;
a one-second C++ capture was accepted by the existing OMADC002 inspection tool.

The first live deployment kept the new console wrapper staged, so a reboot
ran the old direct-framebuffer launcher. The active wrapper was corrected to
start `open-mimic-display`. The obsolete mdev init service was removed when
switching to eudev. The USB keyboard receiver also enumerated with a truncated
configuration descriptor on that boot; reconnecting it restored both input
interfaces in the kernel and Xorg.

The first Xorg build registered the touchscreen but did not process touches.
A target ABI probe reported a 24-byte `struct input_event`: Linux 4.1 headers
embedded a libc `timeval` that is wider in current musl. A raw capture of three
taps contained 36 valid 16-byte kernel records. The compatibility fix, now maintained as a native Buildroot source patch,
backports the Linux time64-safe input event layout to the target headers and
checks the ARM32 structure size at build time. libevdev, mtdev, Xorg and its
evdev driver were rebuilt together. See [the ABI backport](../platform/buildroot/patches/).

Xorg explicitly preloads `fbdevhw` and `shadow` so musl can resolve the fbdev
module's symbols. InputClass rules match evdev event nodes, avoiding duplicate
attempts to open the legacy mouse nodes. The OMAP framebuffer rejects palette
ioctls during Xorg startup; the true-colour display renders correctly despite
those log messages.

After installing the rebuilt input libraries, XInput2 reported live raw touch
events and the JUCE screen displayed `Touch input received` with a touch count
of 50. The owner confirmed that everything works. The corrected display wrapper
also started the application through BusyBox init after restarting the local
session. These runtime fixes are included in the generated Buildroot SD image.
