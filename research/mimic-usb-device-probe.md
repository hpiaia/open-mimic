# USB device controller probe

> Historical record of a prepared but unexecuted experiment. The scripts
> named below were removed when the repository was rebuilt. This document
> preserves the hypotheses and evidence, not a current procedure.

Goal: determine whether the Pearl kernel can register a USB Device
Controller (UDC) through an alternate SD boot. The test does not create a
MIDI/audio gadget, require a PC connection, or write to the SSD.

**Physical limitation:** the user's module exposes only a USB Type-A port.
Even a registered UDC would not prove that USB1 reaches a port suitable for
connecting the Mimic to a PC as a device. The test was prepared on the Mac
but was not installed on the SD. Do not connect two host ports with a USB
A-to-A cable; first identify the port wiring, VBUS control, and electrical
role of the interface.

## Previous evidence

- The original SD shell showed an empty `/sys/class/udc`.
- The Pearl `am5718-edrums-v3.dtb` configures USB1 and USB2 as `host`; USB3
  is disabled.
- A console photo shows the keyboard on USB2 (`488c0000.omap_dwc3_2`).
- `dwc3.ko` contains gadget code and depends on `udc-core.ko`.
- The original system contains `usb_f_uac2.ko`, `usb_f_midi.ko`, and
  `g_midi.ko`.

## Experimental change

The former `tools/build_mimic_usb_device_probe.py` copied the original DTB
to `USB571.DTB` and changed only USB1 `dr_mode` from `host` to `peripheral`.
The decompiled tree was compared with the original; USB2 remained in `host`
mode for the keyboard. The experimental boot script loaded `USB571.DTB` with
the same kernel and initramfs as the validated shell.

Artifacts are in
`../../open-mimic-artifacts/am57x-sd-build/usb-device-probe/`. The original
`PEARL571.DTB` remained on the SD.

## Planned installation and test (historical)

With the SD in the Mac, identify it using `diskutil list`. For the card
identified as `/dev/disk4`, the former command was:

```sh
./tools/install_mimic_usb_device_probe_macos.sh /dev/disk4
```

That installer checked media size and type, label, backup, and hashes. It
wrote only `USB571.DTB` and `bootscr.img` to FAT, saved the previous boot
script, and ejected the card.

The planned test was to start the Mimic while holding BOOT, with the USB
keyboard connected and **without connecting USB to a PC**, then run:

```sh
clear
ls /sys/class/udc
dmesg | grep -i dwc3 | tail -20
```

A name under `/sys/class/udc` would show that a controller registered in
device mode. It would not prove that a physical Mimic port is connected to
that controller. No UDC would mean that the DTB or driver needs more work.
If the keyboard stopped working, the planned recovery was to power off,
return the SD to the Mac, and restore the previous boot script with the
former command:

```sh
./tools/restore_mimic_keyboard_shell_macos.sh /dev/disk4
```

The former restore script checked the card and hashes before replacing
`bootscr.img`.
