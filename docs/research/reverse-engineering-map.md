# Mimic Pro Reverse-Engineering Map

Date: 2026-07-08

This is the current working map for Open Mimic. It separates confirmed facts from inferences so later hardware captures can replace guesses with data.

## Firmware Artifacts

- Official software update file: `mimic_pro_1_4_18.mup` from Pearl's 1.4.18 package.
- Extracted payload: ARM 32-bit Linux ELF, hard-float EABI, dynamically linked, not stripped, with symbols and some DWARF/debug metadata.
- Build hints in the binary point to an ARMv7-A Cortex-A9 hard-float toolchain and a Buildroot-style sysroot path.
- GUI/runtime dependencies include X11, Xext, Xinerama, FreeType, pthread, libstdc++, libc, libm, and libgcc.
- Many symbols and source file names strongly indicate a JUCE application.

## `.mup` Software Update Format

Confirmed from `CUpdaterDataStorage::DecompressUpdate` and the official 1.4.18 `.mup`:

```text
c_string magic = "mimic_software_update"
u32 little-endian format_version = 1
u32 little-endian expected_crc32c_of_decompressed_payload
u32 little-endian metadata_len
metadata bytes, updater accepts up to 100 bytes
u32 little-endian expected_decompressed_payload_size
zlib/gzip-compatible compressed payload
```

For 1.4.18:

- compressed stream starts at offset `0x8a`
- decompressed payload size is `11806472`
- CRC32C is `0xe8ae0866`
- decompressed payload is the `mimic_pro` executable

The checksum is CRC32C/Castagnoli with reversed polynomial `0x82f63b78`.

## `.mos` OS Update Format

Recovered from `CUpdaterDataStorage::saveOSUpdateToFile`, `processOSUpdate`, `readChunk`, and `addFileToOSUpdate`. We still need a real `.mos` sample from hardware or Pearl to verify all edge cases.

Outer file:

```text
u32 little-endian expected_crc32c_of_all_following_bytes
c_string magic = "mimic_software_update"
u32 little-endian format_version = 2
u32 little-endian chunk_count
zlib/gzip-compatible compressed chunk stream
```

Each decompressed chunk:

```text
i32 tag = 0
i32 length = 28
bytes "mimic_software_update_chunk\0"
i32 top_level_tag
i32 record_count
repeated record_count times:
  i32 record_tag
  i32 record_len
  bytes record_data
```

Known record tags:

- `1`: target path string
- `2`: command string or label string
- `3`: file payload bytes; updater writes this to the latest target path and marks it executable

Known top-level behavior:

- `8`: execute the string from record tag `2` using `system(3)`.
- `>8`: records are skipped by the observed reader.
- `4`: used by the app's hidden/debug OS update builder for two file chunks, but the semantic name is still unknown.

Security note: `.mos` files are executable update programs. Treat them like shell scripts plus file payloads.

## Hidden Update Builder Clues

The development pane contains `Create SW Update` and `Create OS Update` paths.

- The SW builder wraps a chosen Mimic app file as `.mup`.
- The OS builder writes `.mos`, declares two chunks, and uses two hard-coded development source paths:
  - `e:\os_update.ddd`
  - `e:\os_update_2.ddd`
- The two OS update chunks use tag `4`, target strings `12345` and `12345_2`, and no observed execute chunk in that debug path.

These look like developer test placeholders, not production OS payload paths.

## Filesystem Map

Observed from strings and `CDirectoryManager` symbols:

- app root: `/root/`
- active app: `/root/mimic_pro`
- incoming app update target: `/root/mimic_pro_update`
- update helper checked before OS update: `/root/kexec`
- settings root: `/mnt/settings/`
- module settings: `/mnt/settings/module_settings/`
- kits: `/mnt/settings/kits/`
- instrument presets: `/mnt/settings/instrument_presets/`
- pad presets: `/mnt/settings/pad_presets/`
- velocity curves: `/mnt/settings/velocurve_presets/`
- setlists: `/mnt/settings/setlists/`
- factory/user instruments: `/mnt/user/instruments/`
- waves: `/mnt/user/wave/`
- trigger captures: `/mnt/user/trig_data/`
- USB: `/mnt/usb-flash/`
- SD: `/mnt/sd-flash/`

Important state filenames include `curr_inst_stor.dat`, `curr_pad_stor2.dat`, `common_settings.dat`, `volume_settings.dat`, `xtalk_settings.dat`, `metr_settings.dat`, `ui_settings.dat`, and `hw_test_log.txt`.

## Hardware API Map

Board/SoM evidence:

- The board photos show a removable Hoodisk `HDSESB-128GB` mSATA SSD and a green 204-pin SO-DIMM compute module.
- A user forum comment identifies the compute module family as Compulab CL-SOM-AM57x.
- Official Compulab documentation for CL-SOM-AM57x describes a TI Sitara AM5728/AM5718 module with SATA-II, USB, Ethernet, and multiple UARTs. This matches the firmware's TI-style McASP/UART clues better than the earlier Cortex-A9-only inference from compiler tuning strings.
- Treat this as a strong hardware identification, but confirm it from the SSD image with `/proc/device-tree/compatible`, kernel logs, bootloader environment, or root filesystem board files.
- Primary-source notes for the Compulab/TI platform are tracked in [compulab-cl-som-am57x-notes.md](./compulab-cl-som-am57x-notes.md).

Observed device/API strings:

- `/dev/ads7953.0`
- `/dev/ads7953.1`
- `/dev/mcasp.0`
- `/dev/mcasp.1`
- `/dev/lcd-conf`
- `/dev/regulators`
- `/dev/ttyS9`
- legacy GPIO sysfs under `/sys/class/gpio`

Primary-source comparison:

- ADS7953 is a TI SPI ADC family. Upstream Linux normally exposes it through IIO, not `/dev/ads7953.N`.
- McASP is TI's multichannel audio serial port. Upstream Linux normally exposes audio through ALSA/ASoC, not `/dev/mcasp.N`.
- Therefore `/dev/ads7953.N` and `/dev/mcasp.N` are likely Pearl/vendor character APIs or compatibility wrappers.

Detailed userspace wrapper notes, ioctl numbers, GPIO mapping, and live-device questions are tracked in [hardware-api-map.md](./hardware-api-map.md).

This custom driver layer is probably the hardest and most valuable part of Open Mimic.

## Application Architecture

Major embedded source/module names:

- update/storage: `updater_dataStorage.cpp`, `data_storage.cpp`, `DirectoryManager.cpp`, `SettingsStorageManager.cpp`
- trigger engine: `EDrumTriggerEng2.cpp`, `trigalgo_1.cpp`, many `TPadProc_*` pad processors
- audio/sampler: `SamplerEngine.cpp`, `DiskStreamer.cpp`, `VoiceItem*.cpp`, `mixer_engine.cpp`, `convoengine.cpp`, `reverb_1.cpp`
- hardware: `adc.cpp`, `auxAdc.cpp`, `regulator.cpp`, `midi_inout_hw.cpp`, `ExtPortControl.cpp`
- diagnostics: `debug_info.cpp`, `AudioOutTest.cpp`, `TrigInputTest*.cpp`, `ExtPortTest*.cpp`
- UI: many `ui_*` and `CSettings_*` classes

The trigger engine has specialized processors for piezo/switch drums, hats, rides, ATV-style pads, and Lemon-style ride behavior.

## Instrument Library Format

The SCM Mimic Library confirms that stock Mimic instruments can contain separate
close/direct, overhead, and room mic definitions. The public Instrument Editor is
an authoring limitation; the playback format and stock firmware already support
multi-mic instruments.

Detailed notes and the current parser are tracked in
[instrument-format.md](./instrument-format.md). Public release/install facts are
tracked in [scm-mimic-library-source-notes.md](./scm-mimic-library-source-notes.md).

## Device Capture Plan

Preferred first step: remove the mSATA SSD, image it read-only/off-device, and inspect the partitions before probing unlabeled UART/test pads. This should reveal boot configuration, init scripts, enabled shells/services, root password policy, kernel/device tree paths, and custom driver modules.

Use `tools/capture_mimic_state.sh` on the running unit after shell access is available and before trying any custom update:

```sh
sh capture_mimic_state.sh /mnt/usb-flash/open-mimic-capture
```

Prioritize these artifacts:

- `/proc/cpuinfo`, `/proc/device-tree/compatible`, `/proc/cmdline`
- `/proc/config.gz` if present
- `dmesg`
- `/proc/modules`
- `/dev` major/minor listing
- `/sys/bus/spi/devices`
- `/sys/bus/iio/devices`
- `/sys/class/gpio`
- `/proc/asound`, `aplay -l`, `arecord -l`
- `/root/etc/edrums-version.txt`
- `/root/etc/edrums-build.txt`

## Open Mimic Milestones

1. Preserve: full SSD/block image, settings export, Pearl update packages, boot logs.
2. Identify: SoC, bootloader, partition table, kernel version/config, device tree, custom drivers.
3. Rehost: run the extracted `mimic_pro` app under emulation or matching ARM rootfs enough to inspect runtime behavior.
4. Probe: write minimal readers for `/dev/ads7953.*`, `/dev/regulators`, `/dev/mcasp.*`, and `/dev/ttyS9` on the real unit without modifying storage.
5. Boot: create a minimal Buildroot or Debian ARMv7 rootfs that starts screen/touch/storage and exposes logs.
6. Replace: implement clean trigger, MIDI, audio, storage, and UI layers without redistributing Pearl binaries or sample libraries.
