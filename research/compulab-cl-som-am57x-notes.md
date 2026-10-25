# Compulab CL-SOM-AM57x Notes for Pearl Mimic Pro RE

Date: 2026-07-08

Scope: primary/official sources only. Pearl-specific conclusions below are implications for the supplied Mimic Pro board context, not official Pearl documentation.

## Confirmed Platform Facts

- **Module family:** Compulab describes CL-SOM-AM57x as a System-on-Module built around the Texas Instruments Sitara AM57x ARM Cortex-A15 SoC family. The product page lists AM5728 as dual-core Cortex-A15 and AM5718 as single-core Cortex-A15, both at 1.5 GHz with NEON/VFPv4; the same page lists C66x DSP, PowerVR SGX544, Vivante GC320, IVA-HD video, Cortex-M4 IPUs, and PRU-ICSS resources.[^compulab-product]
- **Linux software context:** Compulab advertises ready-to-run software packages for Linux and lists Linux kernel, Yocto Project filesystem, U-Boot, and mainline Linux support for CL-SOM-AM57x.[^compulab-product] Its software resources page identifies a CL-SOM-AM57x Linux package based on TI Processor SDK Linux AM57x.[^compulab-sw]
- **Storage and SATA:** The module product page lists SATA-II at 3 Gbps as an always-available I/O feature.[^compulab-product] The evaluation-kit hardware guide says the CL-SOM-AM57x SATA interface is exposed either through a standard SATA connector or through a mini-PCIe slot used as mSATA; it explicitly documents mSATA form-factor drive use.[^compulab-eval-hw]
- **On-module storage:** The reference guide says CL-SOM-AM57x can be configured with eMMC up to 32 GB or SLC NAND up to 1 GB as main onboard storage, and that SPI NOR is used for bootloader/configuration blocks.[^compulab-ref]
- **UART count and electrical type:** The product page lists up to 9 UART ports, 16C750 compatible, up to 12 Mbps.[^compulab-product]
- **Default debug UART:** The April 2016 reference guide states that `UART3_RXD` and `UART3_TXD` are used for the debug UART by Compulab's default software.[^compulab-ref-2016]
- **Evaluation-kit serial behavior:** Compulab's evaluation-kit guide uses a USB serial console at 115200 8N1 with no flow control and expects Linux boot messages followed by a login prompt.[^compulab-getting-started] The USB-console guide identifies the reference carrier's USB console as a CP2104 USB-to-UART bridge at the same serial settings.[^compulab-usb-console]
- **I/O voltage:** The reference guide says CL-SOM-AM57x digital interfaces operate at 3.3 V unless otherwise noted.[^compulab-ref] Compulab's SBC-AM57x page also lists digital I/O voltage as 3.3 V.[^compulab-sbc]
- **AM57x Linux UART driver:** TI's Processor SDK Linux docs say AM57x UART IP is 8250-compliant and uses the Linux 8250 serial driver framework, with driver source at `drivers/tty/serial/8250/8250_omap.c`.[^ti-uart]

## Implications for Mimic Pro Reverse Engineering

- **SSD-first is justified.** The Mimic Pro photos show a removable mSATA-style SSD on the carrier board. Compulab's official docs confirm the CL-SOM-AM57x platform supports SATA/mSATA, while the firmware analysis already points to a Linux userspace. Imaging the SSD read-only is therefore the lowest-risk way to recover partition layout, kernel/init configuration, boot scripts, Pearl app deployment, and any serial/SSH/telnet settings before touching live hardware.
- **Do not assume the Compulab evaluation carrier layout.** The Pearl carrier is custom. The Compulab evaluation kit exposes serial console through known USB/RS-232 connectors, but that does not prove Pearl populated the same path. Treat the eval-kit docs as a reference for likely defaults, not a Pearl wiring diagram.
- **UART probing should be cautious.** Official docs point to UART3 as Compulab's default debug UART, but CL-SOM pins are multiplexed, and the Pearl board may route UART3 differently or not expose it. Because digital I/O is 3.3 V, use a 3.3 V USB-TTL adapter only, connect GND first, identify TX passively, do not connect VCC, and do not attach RS-232 voltage levels to TTL pads.
- **Expected serial settings if a console is found:** start with 115200 8N1, no flow control, because Compulab's evaluation kit and USB-console guide document those defaults. If silent, capture storage and boot config first rather than guessing pin functions.
- **Linux device-tree and boot files are high-value targets.** The official stack uses Linux/U-Boot concepts, and TI's AM57x UART support is standard Linux 8250/OMAP. Once the SSD image is mounted, inspect `/boot`, device-tree blobs, U-Boot environment, `/etc/inittab`, systemd units, `getty` configuration, `dropbear`/`sshd`, and kernel command line for the actual Pearl console path.

## Primary Sources

[^compulab-product]: Compulab, "CL-SOM-AM57x - TI Sitara AM5728 / AM5718 System-on-Module", https://www.compulab.com/products/computer-on-modules/cl-som-am57x-ti-am5728-am5718-system-on-module/
[^compulab-ref]: Compulab, "CL-SOM-AM57x Reference Guide", revised 2016-12-15, https://www.compulab.com/wp-content/uploads/2016/12/cl-som-am57x_reference-guide_2016-12-15.pdf
[^compulab-ref-2016]: Compulab, "CL-SOM-AM57x Reference Guide", revised 2016-04-26, debug UART note, https://www.compulab.com/wp-content/uploads/2016/04/cl-som-am57x_reference-guide_2016-04-26.pdf
[^compulab-eval-hw]: Compulab MediaWiki, "CL-SOM-AM57x: Evaluation Kit: Hardware Guide", SATA/mSATA and serial console sections, https://mediawiki.compulab.com/w/index.php?title=CL-SOM-AM57x:_Evaluation_Kit:_Hardware_Guide
[^compulab-getting-started]: Compulab MediaWiki, "CL-SOM-AM57x: Evaluation Kit: Getting Started", https://mediawiki.compulab.com/index.php/CL-SOM-AM57x:_Evaluation_Kit:_Getting_Started
[^compulab-usb-console]: Compulab MediaWiki, "SB-SOM: HOWTO: USB Console", https://mediawiki.compulab.com/w/index.php?title=SB-SOM:_HOWTO:_USB_Console
[^compulab-sbc]: Compulab, "SBC-AM57x - TI AM5728 / AM5718 Single Board Computer", https://www.compulab.com/products/sbcs/sbc-am57x-ti-am5728-am5718-single-board-computer/
[^compulab-sw]: Compulab MediaWiki, "CL-SOM-AM57x TI AM57x SW Resources", https://mediawiki.compulab.com/index.php/CL-SOM-AM57x_TI_AM57x_SW_Resources
[^ti-uart]: Texas Instruments, "Processor SDK Linux for AM57X Documentation - UART", https://software-dl.ti.com/processor-sdk-linux/esd/AM57X/08_02_01_00/exports/docs/linux/Foundational_Components/Kernel/Kernel_Drivers/UART.html
