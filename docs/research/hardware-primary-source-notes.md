# Hardware Primary-Source Notes

Date: 2026-07-08

Scope: primary-source references for hardware/software identifiers seen in the Pearl Mimic Pro firmware strings, especially `/dev/ads7953.*`, `/dev/mcasp.*`, `/sys/class/gpio`, `/dev/ttyS9`, ARM hard-float Linux, and JUCE/X11 dependencies.

The firmware-string evidence itself is tracked in [pearl-mimic-pro-os-research.md](./pearl-mimic-pro-os-research.md). This file separates source-confirmed facts from Mimic Pro inferences.

## Confirmed Primary-Source Facts

### TI ADS7953 and Linux IIO

- TI identifies the ADS7953 as a "12-Bit, 1-MSPS, 16-Channel, Single-Ended, microPower SAR ADC with Serial I/F." Source: https://www.ti.com/product/ADS7953
- TI's ADS7953EVM-PDK description says the ADS7953 has an easy-to-use serial programming interface, SPI. Source: https://www.ti.com/product/ADS7953
- The upstream Linux devicetree binding `ti,ads7950.yaml` covers "Texas Instruments ADS7950 and similar ADCs" and describes the family as 4-16 channel, 8-12 bit ADCs with SPI interface. Source: https://www.kernel.org/doc/Documentation/devicetree/bindings/iio/adc/ti%2Cads7950.yaml
- The same Linux binding includes `ti,ads7953` in its compatible-string enum and requires `compatible`, `reg`, `vref-supply`, and `#io-channel-cells`. Source: https://www.kernel.org/doc/Documentation/devicetree/bindings/iio/adc/ti%2Cads7950.yaml
- The upstream Linux driver source is `drivers/iio/adc/ti-ads7950.c`; its header identifies it as the Texas Instruments ADS7950 SPI ADC driver. Source: https://github.com/torvalds/linux/blob/master/drivers/iio/adc/ti-ads7950.c
- Linux IIO core documentation says a typical IIO hardware sensor is represented at `/sys/bus/iio/devices/iio:deviceX/` and a buffered/event character-device interface is represented at `/dev/iio:deviceX`. Source: https://docs.kernel.org/driver-api/iio/core.html
- Linux IIO ABI documentation defines ADC raw voltage attributes such as `/sys/bus/iio/devices/iio:deviceX/in_voltageY_raw`. Source: https://www.kernel.org/doc/Documentation/ABI/testing/sysfs-bus-iio

### TI McASP and Linux ALSA ASoC

- TI's McASP design guide identifies McASP as TI's Multichannel Audio Serial Port and says it was designed for multichannel, multi-zone audio. Source: https://www.ti.com/lit/pdf/sprack0
- TI's McASP design guide says McASP is typically used with TDM audio protocols and that I2S is a common TDM configuration. Source: https://www.ti.com/lit/pdf/sprack0
- TI's McASP design guide says McASP may have up to 16 serializers connected to AXR data pins, and each AXR pin can be configured as input or output. Source: https://www.ti.com/lit/pdf/sprack0
- TI's McASP design guide says the device-specific TRM is the best source for architectural and chip-level details because McASP capabilities vary by TI device. Source: https://www.ti.com/lit/pdf/sprack0
- TI Processor SDK Linux audio documentation says the McASP driver supports I2S/TDM mode, DIT mode, synchronous and asynchronous clock modes, and multiple serializers up to 16 for multichannel audio. Source: https://github.com/texasinstruments/processor-sdk-doc/blob/master/source/linux/Foundational_Components/Kernel/Kernel_Drivers/Audio.rst
- The upstream Linux devicetree binding `davinci-mcasp-audio.yaml` is titled "McASP Controller for TI SoCs" and supports compatible strings including `ti,dm646x-mcasp-audio`, `ti,da830-mcasp-audio`, `ti,am33xx-mcasp-audio`, `ti,dra7-mcasp-audio`, and `ti,omap4-mcasp-audio`. Source: https://www.kernel.org/doc/Documentation/devicetree/bindings/sound/davinci-mcasp-audio.yaml
- The same McASP binding defines `op-mode` values where `0` means I2S and `1` means DIT, and defines `tdm-slots` as the number of channels over one serializer. Source: https://www.kernel.org/doc/Documentation/devicetree/bindings/sound/davinci-mcasp-audio.yaml
- The legacy McASP binding says `serial-dir` entries encode serializer pin direction as `0 = inactive`, `1 = TX`, and `2 = RX`. Source: https://www.kernel.org/doc/Documentation/devicetree/bindings/sound/davinci-mcasp-audio.txt
- The Linux ASoC overview says ASoC exists to provide better ALSA support for embedded SoC processors and portable audio codecs. Source: https://docs.kernel.org/sound/soc/overview.html
- Linux ASoC machine-driver documentation says the machine/board driver glues together codecs, platforms, and DAIs, and describes relationships including audio paths, GPIOs, interrupts, clocking, jacks, and voltage regulators. Source: https://docs.kernel.org/sound/soc/machine.html
- ALSA lib PCM documentation says ALSA PCM hardware parameters describe stream format, rate, channel count, and ring-buffer size. Source: https://www.alsa-project.org/alsa-doc/alsa-lib/pcm.html

### Buildroot and ARM hard-float Linux

- Buildroot's manual says Buildroot can generate a cross-compilation toolchain, root filesystem, Linux kernel image, and bootloader for a target. Source: https://buildroot.org/downloads/manual/manual.html
- Buildroot's ARM architecture config defines an `EABIhf` target ABI that depends on an ARM CPU with FPU support. Source: https://raw.githubusercontent.com/buildroot/buildroot/master/arch/Config.in.arm
- Buildroot's ARM architecture config says EABIhf supports the hard floating-point model, executes floating-point instructions using the FPU, and passes floating-point arguments in floating-point registers. Source: https://raw.githubusercontent.com/buildroot/buildroot/master/arch/Config.in.arm
- Buildroot's manual says changing architecture variant, binary format, or floating-point strategy has an impact on the entire system and requires a complete rebuild. Source: https://buildroot.org/downloads/manual/manual.html

### JUCE on Linux/X11

- JUCE's Linux dependencies document lists packages required to build JUCE projects on Linux by module. Source: https://raw.githubusercontent.com/juce-framework/JUCE/master/docs/Linux%20Dependencies.md
- JUCE's Linux dependencies document lists `libasound2-dev` for `juce_audio_devices`, `libfreetype-dev` or `libfreetype6-dev` for `juce_graphics`, and `libx11-dev`, `libxext-dev`, and `libxinerama-dev` for `juce_gui_basics`. Source: https://raw.githubusercontent.com/juce-framework/JUCE/master/docs/Linux%20Dependencies.md

### Linux GPIO sysfs

- Linux kernel GPIO sysfs documentation says the `/sys/class/gpio` userspace ABI is deprecated, moved to `Documentation/ABI/obsolete/sysfs-gpio`, and new userspace consumers should use the GPIO character-device ABI. Source: https://www.kernel.org/doc/Documentation/gpio/sysfs.txt
- The same kernel GPIO sysfs documentation says the old sysfs ABI will not receive new features and will only be maintained. Source: https://www.kernel.org/doc/Documentation/gpio/sysfs.txt

### Linux serial TTY and TI UART context

- Linux kernel serial-console documentation says serial-console support requires serial support compiled into the kernel, and for PC-style serial ports the relevant kernel option is console support on 8250/16550-compatible serial ports. Source: https://docs.kernel.org/admin-guide/serial-console.html
- The Linux device-list documentation assigns `/dev/ttyS0` and following names to UART serial ports under major character device 4; its text lists `/dev/ttyS0` as the first UART serial port and `/dev/ttyS191` as the 192nd UART serial port. Source: https://www.kernel.org/doc/Documentation/admin-guide/devices.txt
- Linux man-pages documentation describes `ttyS` devices as serial terminal lines and shows `/dev/ttyS0` through `/dev/ttyS3` as character devices. Source: https://man7.org/linux/man-pages/man4/ttys.4.html
- TI Processor SDK Linux UART documentation says TI Sitara SoCs have 8250-compliant UART IPs that use Linux's common 8250 serial-driver framework. Source: https://software-dl.ti.com/processor-sdk-linux/esd/docs/06_03_00_106/linux/Foundational_Components/Kernel/Kernel_Drivers/UART.html
- TI Processor SDK Linux UART documentation says probed serial ports are exposed to userspace as `/dev/ttySX`, where `X` is a zero-indexed serial port number. Source: https://software-dl.ti.com/processor-sdk-linux/esd/docs/06_03_00_106/linux/Foundational_Components/Kernel/Kernel_Drivers/UART.html

## Inferences About Mimic Pro

- The firmware strings `/dev/ads7953.0` and `/dev/ads7953.1` strongly suggest two ADS7953-class ADC interfaces, but upstream Linux would normally expose ADS7953 via IIO paths such as `/sys/bus/iio/devices/iio:deviceX/` and `/dev/iio:deviceX`, not as `/dev/ads7953.N`. Sources: https://www.kernel.org/doc/Documentation/devicetree/bindings/iio/adc/ti%2Cads7950.yaml and https://docs.kernel.org/driver-api/iio/core.html
- Because the firmware strings use `/dev/ads7953.N` rather than standard IIO names, Mimic Pro likely used a custom, vendor, or legacy character driver wrapper for trigger ADC access. That remains an inference until the running kernel config, loaded modules, device-tree, or filesystem image is extracted. Sources: https://docs.kernel.org/driver-api/iio/core.html and https://www.kernel.org/doc/Documentation/ABI/testing/sysfs-bus-iio
- Two ADS7953 chips would provide up to 32 single-ended ADC channels by part capability, but that does not prove how many channels are wired or used by Mimic Pro. Source for part capability: https://www.ti.com/product/ADS7953
- The firmware strings `/dev/mcasp.0` and `/dev/mcasp.1` point toward TI McASP audio hardware or a custom userspace-facing driver for it, but upstream ALSA ASoC normally presents audio through ALSA sound devices/APIs rather than `/dev/mcasp.N`. Sources: https://docs.kernel.org/sound/soc/overview.html and https://www.alsa-project.org/alsa-doc/alsa-lib/pcm.html
- The presence of both ADS7953 and McASP identifiers fits an electronic drum module architecture: ADS7953-class SPI ADCs are plausible for trigger sensing, while McASP is plausible for multichannel digital audio I/O. This is architectural inference, not confirmation of the board schematic. Sources for component capabilities: https://www.ti.com/product/ADS7953 and https://www.ti.com/lit/pdf/sprack0
- The McASP identifiers make a TI SoC or TI-audio-subsystem-derived design plausible, because upstream Linux McASP bindings cover TI SoC families including AM33xx, DRA7xx, OMAP4, DA830/DA850, and DM646x. This does not identify the exact Mimic Pro SoC. Source: https://www.kernel.org/doc/Documentation/devicetree/bindings/sound/davinci-mcasp-audio.yaml
- The firmware string `/sys/class/gpio` implies the application or scripts may control GPIO lines from userspace through the legacy sysfs GPIO ABI. Since upstream kernel docs mark that ABI deprecated, an Open Mimic port should prefer `libgpiod`/GPIO character devices for new code, while keeping sysfs compatibility during reverse engineering. Source: https://www.kernel.org/doc/Documentation/gpio/sysfs.txt
- The firmware string `/dev/ttyS9` implies a numbered serial/UART device exposed through Linux TTY; on TI-style 8250 UART systems, `/dev/ttySX` maps to zero-indexed serial ports after probing. This could be MIDI, debug, MCU communication, or another board-internal serial link; the string alone does not identify its role. Sources: https://www.kernel.org/doc/Documentation/admin-guide/devices.txt and https://software-dl.ti.com/processor-sdk-linux/esd/docs/06_03_00_106/linux/Foundational_Components/Kernel/Kernel_Drivers/UART.html
- The decompressed Mimic application previously appeared as a 32-bit ARM EABI5 hard-float Linux ELF using interpreter `/lib/ld-linux-armhf.so.3`. Buildroot's ARM EABIhf documentation matches the hard-float ABI concept, but the ELF interpreter alone does not prove the original OS was built with Buildroot. Sources for Buildroot and EABIhf: https://buildroot.org/downloads/manual/manual.html and https://raw.githubusercontent.com/buildroot/buildroot/master/arch/Config.in.arm
- The dynamic dependencies previously observed in the Mimic app included X11, Xext, Xinerama, and FreeType-like libraries. JUCE's Linux dependencies list those libraries for GUI/graphics modules, so JUCE-on-Linux remains plausible, but this is not proof unless JUCE symbols, resources, license notices, or build metadata are confirmed. Source: https://raw.githubusercontent.com/juce-framework/JUCE/master/docs/Linux%20Dependencies.md

## Reverse-Engineering Checks To Prioritize

- Extract `/proc/device-tree/compatible`, `/proc/cpuinfo`, kernel `dmesg`, `/proc/config.gz` if present, `/proc/modules`, `/sys/bus/spi/devices`, `/sys/bus/iio/devices`, `/sys/class/gpio`, `/sys/class/tty`, `/dev`, and `/proc/asound` from a running unit.
- Check whether `/dev/ads7953.*` and `/dev/mcasp.*` are static device nodes in the root filesystem or dynamically created by `udev`/`mdev`; record their major/minor numbers with `ls -l`.
- If the kernel exposes IIO, capture `find /sys/bus/iio/devices -maxdepth 3 -type f -print` and sample `in_voltage*_raw`/`*_scale` attributes without loading the drum engine.
- For McASP, capture `aplay -l`, `arecord -l`, `/proc/asound/cards`, `/proc/asound/pcm`, and the relevant `dmesg` ASoC probe lines.
- For `/dev/ttyS9`, capture `dmesg | grep -Ei 'ttyS|serial|uart|8250|omap'`, `stty -F /dev/ttyS9 -a`, and check whether the main app opens it during normal operation.
- Preserve original kernel, device tree, modules, and root filesystem before replacing any driver, because custom `/dev/ads7953.*` and `/dev/mcasp.*` APIs may be central to Pearl's low-latency trigger/audio path.
