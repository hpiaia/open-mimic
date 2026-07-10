# SCM Mimic Library Source Notes

Date gathered: 2026-07-08

Scope: source-level facts for the Pearl Mimic Pro SCM library investigation. This note only uses the public eDrum Workshop page, the local installation PDF, and visible local filesystem/package metadata under `assets/SCM Mimic Library`. It does not reverse engineer `.kit`, `.bin`, `.drd`, or `checksum.dat` contents.

## Sources

- Public page: The eDrum Workshop, "Free Tama SCM Mimic PRO Instrument Library Releases!", <https://theedrumworkshop.com/blogs/news/scm-mimic-download>.
- Local PDF: `assets/SCM Mimic Library/Installation Guide - Tama SCM Library.pdf`.
- Local package directory: `assets/SCM Mimic Library/Tama SCM Steve Mackrill.lib`.
- Local version file: `assets/SCM Mimic Library/Tama SCM Steve Mackrill.lib/libver.mimicinfo`.

## Public Page Facts

- The page title/headline is "Free Tama SCM Mimic PRO Instrument Library Releases!".
- The visible article date is June 30, 2023, and the visible author is Luke Hesketh.
- The page schema reports:
  - `headline`: `Free Tama SCM Mimic PRO Instrument Library Releases!`
  - `description`: `Download the FREE Mimic Pro Tama SCM library here!`
  - `dateCreated`: `2023-06-30T13:27:02Z`
  - `datePublished`: `2023-06-30T17:16:52Z`
  - `author.name`: `Luke Hesketh`
  - `publisher.name`: `The eDrum Workshop`
- The page describes the release as a free instrument library for the Pearl Mimic Pro module.
- The page says the SCM Mimic Library contains "12 new instruments" sampled from a 2002 Tama StarClassic Maple shell pack and a Meinl cymbal set.
- The page lists the included instruments as:
  - `22x18" SCM Kick`
  - `14x5.5" SCM Snare (wires on & wires off)`
  - `8x8", 10x8", 12x8", 13x9" and 15x14" SCM Toms`
  - `12x5" Steel Soprano Snare`
  - `14" Byzance Medium Hi-hats`
  - `20" Amun Powerful Ride`
  - `17" Pure Alloy Custom Medium Thin Crash (left)`
  - `18" Pure Alloy Medium Crash (right)`
- The page says the library includes 14 kit presets, many contributed by The eDrum Workshop.
- The page says the samples were recorded and made available for free download by Steven Mackrill.
- The page includes an embedded YouTube video at `https://www.youtube-nocookie.com/embed/P4zoMkKIZmI`.
- The page's download button links to `https://delivery.shopifyapps.com/-/328e4c1006973745/dacd966eaf2f8b6b`. I did not follow or re-download this archive.
- The page links to a GoFundMe at `https://www.gofundme.com/f/pearl-mimic-pro-free-library-update`.

## Installation Guide Facts

PDF metadata extracted from the local PDF:

- Pages: 3.
- PDF internal metadata:
  - `/Author`: `Luke Hesketh`
  - `/Creator`: `Microsoft(R) Office Word 2007`
  - `/Producer`: `Microsoft(R) Office Word 2007`
  - `/CreationDate`: `D:20230630122630+01'00'`
  - `/ModDate`: `D:20230630122630+01'00'`
- macOS file metadata reports content type `com.adobe.pdf`, PDF version `1.5`, page width `595.32`, page height `841.92`, and file size `1,135,342` bytes.

The installation guide states:

- The library can only be loaded if the Pearl Mimic Pro is running software version `1.4.18` or above.
- The guide says to update via the Pearl Electronics website if the module is not running that version.
- At time of writing, the guide says the `1.4.18` update only showed on the US version of Pearl's update site.
- The files must be loaded via a USB flash drive formatted in the module before use.
- USB/package preparation:
  - Unzip `SCM Mimic Library.zip`.
  - Copy or drag the folder named `Tama SCM Steve Mackrill.lib` to the root/top folder of the USB drive.
  - Do not put the `.lib` folder inside another folder, or the module may not load it.
- Module import path:
  - Insert the USB drive into the Pearl Mimic Pro and turn it on.
  - Open `SETTINGS`.
  - Select the `Sound Lib` tab.
  - Press `IMPORT INSTRUMENT LIBRARY FROM USB STICK`.
  - Select `Tama SCM Steve Mackrill.lib | 30 June 2023` from the USB-stick panel.
  - Press `IMPORT`.
  - The guide says installation may take up to 1 minute depending on USB drive speed.
  - After installation, `Tama SCM Steve Mackrill.lib | 30 June 2023` appears in the right panel alongside installed libraries.
- Kit behavior:
  - SCM kits appear in the kit list after numbered factory kits and before User `[U]` kits.
  - The SCM kits are treated the same as factory kits by the module.
  - Editing one of these kits creates a User `[U]` version and leaves the installed preset unchanged.
  - Individual installed SCM presets can be deleted from the kit list as "Factory Kit" entries, but restoring them requires reinstalling `Tama SCM Steve Mackrill.lib`; the guide does not recommend deleting the installed presets.
- Full library deletion:
  - In the same Sound Lib import screen, selecting `Tama SCM Steve Mackrill.lib | 30 June 2023` in the right panel and pressing `DELETE` removes the `.lib` file and its associated kit presets and instrument files from the module.

## Local Package Facts

Visible top-level local layout:

```text
assets/SCM Mimic Library/
  Installation Guide - Tama SCM Library.pdf
  Tama SCM Steve Mackrill.lib/
    checksum.dat
    libver.mimicinfo
    instruments/
    kits/
```

`.DS_Store` files are also present locally and appear to be macOS filesystem artifacts.

Local size and type observations:

- `assets/SCM Mimic Library`: `1,854,147,702` total file bytes.
- `assets/SCM Mimic Library/Tama SCM Steve Mackrill.lib`: `1,853,004,164` total file bytes.
- `kits/`: `553,475` total file bytes.
- `instruments/`: `1,852,439,653` total file bytes.
- `Installation Guide - Tama SCM Library.pdf`: `1,135,342` bytes; detected as a 3-page PDF document, version 1.5.
- `libver.mimicinfo`: `12` bytes; detected as ASCII text; content is `30 June 2023`.
- `checksum.dat`: `780` bytes; detected as data. It was not interpreted.
- `.kit`, `.bin`, and `.drd` files were detected as data. They were not interpreted.

Local package file counts:

- `.kit`: 14 files.
- `.bin`: 13 files.
- `.drd`: 13 files.
- `.dat`: 1 file.
- `.mimicinfo`: 1 file.

Visible kit preset filenames:

```text
SCM 12 Clean [eDW].kit
SCM 12 Natural.kit
SCM 12 Proc [eDW].kit
SCM 12 Raw [eDW].kit
SCM 12 Roomy [eDW].kit
SCM 14 Clean [eDW].kit
SCM 14 Natural.kit
SCM 14 Proc [eDW].kit
SCM 14 Raw [eDW].kit
SCM 14 Roomy [eDW].kit
SCM GateRock [eDW].kit
SCM Metal [eDW].kit
SCM OldSkool [eDW].kit
SCM Tiny [eDW].kit
```

Visible instrument basename pairs. Each listed basename has both a `.bin` and a `.drd` file:

```text
Crash L Mnl 17 PAM
Crash R Mnl 18 PAM
Hi-Hat Mnl 14 BZM
Kick Tma 22 SCL
Ride Mln 20 AMU
Snare Tma 5 STS
Snare Tma 5.5 SCL
Snare Tma 5.5 SWO
Tom Tma SCL 08
Tom Tma SCL 10
Tom Tma SCL 12
Tom Tma SCL 13
Tom Tma SCL 15
```

Source nuance: the public page says "12 new instruments"; the local package visibly contains 13 `.bin`/`.drd` basename pairs. This note does not resolve that difference. The visible filenames suggest the public page's single `14x5.5" SCM Snare (wires on & wires off)` line corresponds to two local basenames, `Snare Tma 5.5 SCL` and `Snare Tma 5.5 SWO`, but that is only a filename-level observation.

## Selected Hashes

SHA-256 hashes for small source/metadata files:

```text
4caa1829ba751899fde4abb2423567aa24ad5534b9759de533c411857a6f6f7f  assets/SCM Mimic Library/Installation Guide - Tama SCM Library.pdf
49aba374c86c5fc179080799112554ba257d9b4165bcf4422ac79b18a51ddf51  assets/SCM Mimic Library/Tama SCM Steve Mackrill.lib/libver.mimicinfo
ed2bd77259c8f8aef87498f628f66228a4c43caf3fb0d545708b69bee71c2fe5  assets/SCM Mimic Library/Tama SCM Steve Mackrill.lib/checksum.dat
```
