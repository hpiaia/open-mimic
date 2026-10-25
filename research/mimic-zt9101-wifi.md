# ZT9101 USB Wi-Fi adapter on the Mimic Pro

## Hardware evidence

During SD boot, the Pearl kernel `4.1.13-cl-som-am57x-ti-3.0` detected the
`214b:7260` hub and the `350b:9101` USB adapter (`ZTopInc`, `802.11n NIC`).
Without an additional driver, `/sys/class/net` contained only `lo`. The
external RJ45 port was not used: CPSW is disabled in the original DTB, and
the Mimic board shown by its owner lacks the populated Ethernet components.

## Experimental build

The driver source came from
[ZT9101_vendor_linux_driver](https://github.com/yjun123/ZT9101_vendor_linux_driver),
subdirectory `ZT9101_android_V1.2.1800.20241115`. Its USB ID table includes
`350b:9101`. The local build omitted the `usb_driver.shutdown` callback,
which does not exist in Linux 4.1.13. The source and patch were removed from
this repository after the tested binary was archived.

The kernel configuration was captured from `/proc/config.gz` on the Mimic.
The source baseline is the TI `processor-sdk-linux-02.00.01` tree cited by
[CompuLab for Linux 4.1.13](https://mediawiki.compulab.com/w/index.php?title=CL-SOM-AM57x%3A_Linux%3A_Kernel&oldid=2968).
The build used ARM hard-float GCC 5 on Linux. Because the kernel was supplied
with `CONFIG_MODVERSIONS=y` but without `Module.symvers`, the table was
reconstructed from CRCs in original modules and symbols exported by the TI
tree built with the captured configuration.

The first `.ko` was built with:

- `vermagic`: `4.1.13-cl-som-am57x-ti-3.0 SMP preempt mod_unload modversions ARMv7 p2v8`;
- USB alias: `350b:9101`;
- dependencies: `cfg80211`, `usbcore`;
- 149 imported symbols, each with a CRC in the reconstructed table;
- no CRC mismatches among 2,237 comparable symbols from the TI tree and
  original modules.

REA 6.8.0 `inspect_kernel_module` independently inspected the archived
`zt9101_ztopmac_usb.ko` (SHA-256
`fb684d1ece87ffab0a4b2b93d55a28099b91b4e12588d3e04eb3fe6fdd5866c1`).
Its `.modinfo` reports the same kernel version, `cfg80211,usbcore`
dependencies, and USB aliases, including `350b:9101`. All 149 `__versions`
CRCs match `platform/kernel/pearl-4.1.13/Module.symvers` by symbol name
(REA Evidence `ev_a100a684038a2253466faf504c7f1abd3cc26cf727974aa02b86064ea707be7b`).
This metadata check establishes build compatibility evidence; the hardware
test below establishes that this module actually loaded and created `wlan0`.

The hardware test confirmed that `modprobe cfg80211` and `insmod ZT9101.KO`
returned zero, the firmware loaded, and `wlan0` appeared. The log is
`WIFIPROB.TXT` on the SD FAT partition. The next boot confirmed WPA
association to LYRA, DHCP, and a Dropbear server on port 22. A Mac
authenticated as `root` with a public key and ran SSH commands; a PTY
session was also tested. All four original SSD partitions remained mounted
read-only (`ro,norecovery`).

`WPA.CONF` on the SD contains a derived PSK and must be treated as a
credential; it does not belong in Git. The boot environment reads
`AUTHKEYS` from FAT at startup. The private key remains on the Mac.

The adapter firmware, kernel configuration and symbols, and compiled module
are under `platform/`. Build logs and test captures remain outside Git in
`../open-mimic-artifacts/`.
