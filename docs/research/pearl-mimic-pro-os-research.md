# Pearl Mimic Pro OS Research

Date: 2026-07-08

## Confirmed Public Facts

- Pearl still hosts the Mimic Pro support/download pages. The EU download page lists "Download 1.4.18 Software Update" for PC and Mac OS, plus the Mimic Pro Instrument Editor v1.0.6. Source: https://pearldrum.com/eu/support/firmware/mimic-pro-library
- Pearl's 2022 user manual identifies the device as MIMP24B1 and documents a 120GB SSD, USB flash removable storage, MIDI in/out, a 7 inch IPS touchscreen, 16 trigger inputs, and 16 balanced line outputs. Source: https://pearldrums.prod.acquia-sites.com/sites/default/files/image_folder/SUPPORT/FIRMWARE/MIMICPRO/PearlMimicPRO-User-Manual-v1.4.18.pdf
- The same manual labels the rear "EXT & USB" area as usable with USB cable/thumb drive for adding sounds, and says the EXT RJ45 connection is for future expansion. Source: https://pearldrums.prod.acquia-sites.com/sites/default/files/image_folder/SUPPORT/FIRMWARE/MIMICPRO/PearlMimicPRO-User-Manual-v1.4.18.pdf
- The side controls are documented as POWER, BOOT, and SD card slot. BOOT is documented as "For use with software updates"; the SD slot is documented for MP3/WAV song files, presets, and backup import/export. Source: https://pearldrums.prod.acquia-sites.com/sites/default/files/image_folder/SUPPORT/FIRMWARE/MIMICPRO/PearlMimicPRO-User-Manual-v1.4.18.pdf
- Pearl's software update instructions say the update ZIP contains a `.mup` file, and the update is applied from Settings > Sys > Apply Software Update. Source: https://pearldrum.com/sites/default/files/image_folder/SUPPORT/FIRMWARE/MIMICPRO/Pearl-Mimic-Pro-How-to-Install-Software-Updates.pdf

## Local Firmware Package Findings

These findings came from inspecting Pearl's official PC Windows 1.4.18 download as data only.

- The official Windows ZIP is about 21.7 MB and contains two files:
  - `Mimic Instrument Editor r106 Setup.exe`
  - `mimic_pro_1_4_18.mup`
- The `.mup` file starts with the ASCII magic `mimic_software_update`.
- A zlib stream begins at offset `0x8a` / decimal `138`.
- Decompressing the zlib stream yields an 11.8 MB ELF:
  - `ELF 32-bit LSB executable, ARM, EABI5`
  - interpreter `/lib/ld-linux-armhf.so.3`
  - dynamically linked for GNU/Linux
  - not stripped
  - contains debug info
- Dynamic dependencies include X11, Xext, Xinerama, freetype, pthread, libstdc++, libm, libgcc_s, and libc. This strongly suggests the main UI/app is a Linux/X11 application, likely built with JUCE.
- Symbol and string names expose useful reverse-engineering anchors:
  - `CUpdaterDataStorage::CreateUpdate`
  - `CUpdaterDataStorage::DecompressUpdate`
  - `CUpdaterDataStorage::processOSUpdate`
  - `CSettings_SysPane::applySWUpdate`
  - `CSettings_SysPane::applyOSUpdate`
  - `CUIDataMgr::PrepareForOsUpdate`
  - `Create SW Update`
  - `Create OS Update`
  - `Export Captured Trigger Data to USB Stick`
  - `Dump Raw Midi To Usb`
  - `Scan Internal Storage And Export To USB Stick`
- Embedded paths/devices worth investigating on hardware:
  - `/mnt/settings/`
  - `/mnt/user/`
  - `/mnt/usb-flash/`
  - `/mnt/sd-flash/`
  - `/mnt/user/instruments/`
  - `/mnt/user/wave/`
  - `/mnt/user/trig_data/`
  - `/dev/mcasp.0`
  - `/dev/mcasp.1`
  - `/dev/ads7953.0`
  - `/dev/ads7953.1`
  - `/dev/lcd-conf`
  - `/dev/regulators`
  - `/dev/ttyS9`
  - `/sys/class/gpio/gpio...`
  - `./etc/edrums-version.txt`
  - `./etc/edrums-build.txt`

Minimal reproduction:

```sh
mkdir -p /tmp/open-mimic-fw
curl -L 'https://pearldrums.canto.com/direct/other/redkt629310dbc33ie9rveui1b/2S6Y_blDyk4IMG0IfXrKWjJ5UxY/original?content-type=application%2Fzip&name=Pearl+Mimic+Pro+Update+%26+Instrument+Editor+-+PC+Windows+1.4.18.zip' -o /tmp/open-mimic-fw/pearl-mimic-pro-pc-1.4.18.zip
unzip -p /tmp/open-mimic-fw/pearl-mimic-pro-pc-1.4.18.zip 'Pearl Mimic Pro Update & Instrument Editor - PC Windows 1.4.18/mimic_pro_1_4_18.mup' > /tmp/open-mimic-fw/mimic_pro_1_4_18.mup
python3 - <<'PY'
from pathlib import Path
import zlib
data = Path('/tmp/open-mimic-fw/mimic_pro_1_4_18.mup').read_bytes()
Path('/tmp/open-mimic-fw/mimic_app_1_4_18.elf').write_bytes(zlib.decompress(data[138:]))
PY
file /tmp/open-mimic-fw/mimic_app_1_4_18.elf
strings -a -n 8 /tmp/open-mimic-fw/mimic_app_1_4_18.elf | grep -i update
nm -C /tmp/open-mimic-fw/mimic_app_1_4_18.elf | grep CUpdater
```

## Recommended Extraction Path

1. Preserve the factory state before experimenting.
   - Export all official backups/configs from the UI.
   - Save the official update ZIPs and manuals.
   - If the internal SSD is physically removable, image it read-only before modifying anything.

2. Keep the first hardware tests non-invasive.
   - Test normal USB and BOOT-held USB enumeration from a host computer.
   - Test normal SD and BOOT-held SD behavior.
   - Record exactly what changes on host USB, screen, and storage LEDs.
   - Do not assume the RJ45 is Ethernet; Pearl documents it as future expansion.

3. Find a console before trying to boot a new OS.
   - Inspect the board for UART pads/headers.
   - Use a 3.3V USB-TTL adapter, not RS-232 voltage.
   - Capture boot logs with and without BOOT held.

4. Reverse-engineer the official app and updater.
   - Load the decompressed ARM ELF into Ghidra/Rizin/IDA.
   - Start with updater symbols, mount path symbols, device node strings, and debug menu strings.
   - Document the `.mup`/`.mos` formats before attempting to generate an update file.

5. Build Open Mimic in layers.
   - First target: reproducible backup/extraction tooling.
   - Second target: hardware map and boot logs.
   - Third target: minimal Linux userspace that boots and lights up screen/touch/storage.
   - Fourth target: trigger/MIDI/audio IO proof-of-concepts.
   - Final target: full replacement UI/sampler.

## Legal/Distribution Notes

- Avoid redistributing Pearl firmware, the Mimic app binary, Steven Slate/Pearl sample libraries, or other proprietary assets.
- If the device ships Linux/GPL components, requesting corresponding source from Pearl or distributors may be legitimate and useful, but verify the actual license notices from the device/package.
- A distributable Open Mimic project should use clean-room replacement code and require users to provide their own legally obtained samples/assets.
