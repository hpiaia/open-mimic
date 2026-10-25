# Mimic Pro SD boot and storage evidence

On 2026-10-25, the Mimic BOOT button selected the SD card. A CompuLab
bootloader script wrote `BOOTSEEN.TXT` to its FAT partition. An ARM Linux
initramfs subsequently wrote `LINUXSEEN.TXT`, independently confirming
kernel execution without a serial connection.

The original Pearl `4.1.13-cl-som-am57x-ti-3.0` kernel with
`am5718-edrums-v3.dtb`, both loaded from SD, lit the 1024×600 LCD and
created `/dev/fb0` and DRM `card0`. A generic CompuLab kernel and carrier
device tree did not provide the Mimic LCD. The Pearl boot image appears even
without the `mimic_pro` application; its exact origin is still unknown.

The internal KingFast SSD contains four partitions: `sda1` OS (1 GiB),
`sda2` application (200 MiB), `sda3` settings, and `sda4` sound samples.
The first two were copied through an SD-booted environment, with SHA-256
comparisons against the source devices. The copies are stored outside Git
under `../open-mimic-artifacts/original-ssd-2026-10-25/`.

| Image | Bytes | SHA-256 |
| --- | ---: | --- |
| `MIMIC1.IMG` | 1,073,741,824 | `bcef21c4a4c71d336e6c7afb86e20566f2f224d7dee4e66940119909b83713da` |
| `MIMIC2.IMG` | 209,715,200 | `37f9ae7764014bebb8fd5c043d9115741da9897ba12a15359b98873f36e688be` |

The SSD filesystems were inspected read-only. Their original root system
identifies itself as Buildroot 2015.11.1. An SD userspace with the Pearl
kernel reached an interactive LCD shell; USB keyboard input worked after
loading `usbhid`, `hid-generic`, and `usbkbd`. The terminal also needed sane
TTY settings for line wrapping and carriage returns.

For a root filesystem with this kernel, disable modern ext4 features such as
`metadata_csum` and `orphan_file`. An early boot attempt also showed that
the MMC root device may appear only several seconds after startup, so the
boot script uses `rootwait`.

Original source references: [CompuLab SD boot guide](https://mediawiki.compulab.com/w/index.php?title=CL-SOM-AM57x%3A_U-Boot%3A_Creating_a_bootable_SD_card),
[TI AM57x boot sequence](https://software-dl.ti.com/processor-sdk-linux/esd/AM57X/09_03_06_05/exports/docs/linux/How_to_Guides/Host/Boot_Sequence.html).
