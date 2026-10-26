# Instrument library files

This document records read-only observations of the libraries on the owner's
Mimic SSD. It establishes container layout and metadata consistency; playback
semantics and complete behavioral parity require separate native-code and audio
validation. Source assets and decoded per-instrument JSON stay outside Git.

## Inspected inventory

The read-only SSD path is `/mnt/pearl/user/instruments`. The inspected tree has
845 files totaling 25,202,374,087 bytes.

| Library directory | BIN / DRD pairs | Integrated MCI | Kits | Version text |
| --- | ---: | ---: | ---: | --- |
| Mimic_Factory.lib | 336 | 0 | 104 | 24 June 2022 |
| Tama SCM Steve Mackrill.lib | 13 | 0 | 14 | 30 June 2023 |
| eDW PUNCH.lib | 0 | 24 | 0 | 12 June 2025 |

`.lib` denotes a directory, not a single archive. `libver.mimicinfo` is a
12-byte ASCII date in these three examples. Factory and Tama also contain
`checksum.dat`; eDW does not.

- 349 BIN metadata files: 8,813,784 bytes total.
- 349 DRD sample payload files: 23,071,792,920 bytes total.
- 24 integrated MCI files: 2,117,463,785 bytes total.
- 118 kit files: 4,285,098 bytes total.

All 349 BIN, 24 MCI metadata prefixes, and 118 KIT files were decoded without
unparsed metadata bytes or duplicate typed keys: **491 containers**. Maximum
observed uncompressed metadata size was 477,909 bytes.

## Metadata prefix

BIN, KIT and MCI share this container prefix:

| Offset | Content |
| ---: | --- |
| 0 | `mimic_storage\0` (14 bytes) |
| 14 | little-endian u32 container version, observed 3 |
| 18 | little-endian u32 uncompressed metadata byte count |
| 22 | zlib stream ending at its own EOF/checksum |

The uncompressed body contains seven typed maps, in order: `int`,
`int_array`, `float`, `float_array`, `bool`, `bool_array`, `string`. Each map
starts with a u32 entry count. Keys and string values are length-prefixed
UTF-8 including their terminating NUL. Numeric values are little endian;
booleans are one byte; arrays carry a u32 element count.

After the string map comes **one u32 image byte length followed directly by
the image bytes**, not an image map. All 373 instrument images were present;
kit images had length zero. Instrument images inspected by the reader carry
PNG signatures. The earlier settings-only parser treats the zero image field
as a map count; that ambiguity was hidden by image-free settings files.
`inspect_mimic_library.py` deliberately uses a separate parser.

The reader stops at zlib EOF and reports the exact encoded metadata length.
It does not hash or read the entire sample payload. Synthetic tests verify
nonzero image decoding, integrated payload boundaries, bounded reading,
truncated compressed data, inconsistent declarations, and image truncation.

## External and integrated sample data

A BIN has no bytes after its compressed metadata. Its same-stem DRD file
contains the sample data. All 349 external pairs exist in the inspected tree.

An MCI contains the same metadata followed immediately by integrated sample
data. Its sample-data base is **the exact zlib EOF offset**. A separate
23-byte checksum footer follows the sample data; it must not be mistaken for
an extra header before the data.

All 24 MCI footers contain `MMKCSM\0` (7 bytes) and a 16-byte digest. For the
complete 14,384,263-byte eDW DWCP 20x18 Direct fixture, that digest equals
MD5 of the entire preceding file, excluding all 23 footer bytes. It does not
match MD5 of sample data alone. The same checksum scope has not yet been
recomputed across every full integrated file; all 24 footer signatures were
read and checked.

Nine compressed-stream headers were read at the first, middle and last sample
positions of three different MCI files using metadata EOF as their base.
They had plausible widths 17–25 and decoded counts 325–478. These observations
support the base-offset calculation; full codec validation is separate.

## Sample pool consistency

Every inspected instrument has one `INST0pool0` pool. Together they describe
22,287 sample entries, with a maximum of 706 in one instrument. For every pool,
all seven arrays have exactly `psz` elements:

`dedo`, `desc`, `dlen`, `dofs`, `fcnt`, `nchn`, `nobu`.

The following byte-range relationships held across **all 373 instruments**:

```text
max(dedo[i] + desc[i] * 4) == min(dofs[i])
external DRD byte length == max(dofs[i] + dlen[i])
integrated MCI byte length == metadata EOF + max(dofs[i] + dlen[i]) + 23
```

The `dedo`/`desc` region precedes the `dofs`/`dlen` streaming region. These
range checks do not alone establish the precise playback meaning of every
field, so consumers should corroborate names against the native loader.

Mic declarations match channel counts exactly: summing one channel for a mono
mic and two for an `isst` stereo mic equals every sample's `nchn`. The corpus
contains one to four mic definitions and one to six stored PCM channels.
`micpos` is a logical mic role rather than a byte/channel offset: observed
roles include direct, snare bottom, kick in/out, overhead and room.

The catalog contains 1,417 articulation definitions and 5,947 velocity-layer
sample-index arrays. All index arrays have 16 slots; the separate `nums` field
specifies the active count. Observed layers per articulation range from 1 to
12, and active variants per layer from 0 to 12. One zero-variant layer exists;
a reader must handle it without blindly selecting an array slot. Full
selection, anti-repeat and variation behavior require native-code evidence.

## Container version versus instrument version

The wrapper version is 3 in all 491 decoded containers. The distinct metadata
field `INST0insv` is 0 for the 336 Factory instruments, 1 for the 24 integrated
eDW instruments, and `0xCDCDCDCD` (signed -842150451) for all 13 Tama
instruments. The latter resembles an uninitialized legacy field. Rejecting
all instruments whose `insv` is not 0 or 1 would therefore exclude a complete
installed library whose remaining structure passes the same checks.

## Library checksum manifests

The two `checksum.dat` files use a different envelope:

```text
20-byte "mimic_checksum_list\0"
u32 version (observed 1)
100 reserved bytes
u32 entry count at offset 128
zlib stream at offset 132
```

Decoded records are NUL-terminated filename bytes followed by a 16-byte MD5.
The reserved region contains pointer-like, nonzero data and is not interpreted
as portable semantic fields. Factory has 778 entries and Tama 26. Every copied
metadata entry checked against its manifest matched: 441 Factory files and
13 Tama files. Large DRD hashes were not all recomputed. Factory's manifest
also references `105 Metal Hybrid.kit`, absent from this SSD inventory; checksum
lists must not be assumed to describe an exact current directory snapshot.

## Reproducible tools and private artifacts

- `scripts/inspect_mimic_library.py`: bounded streaming metadata reader and JSON export.
- `scripts/test_inspect_mimic_library.py`: five synthetic, asset-free tests.
- `../open-mimic-artifacts/library-research/metadata/`: copied small metadata.
- `../open-mimic-artifacts/library-research/prefixes/`: bounded payload prefixes.
- `build/library-research/metadata-validation.json`: all 491 parse results.
- `build/library-research/pool-validation.json`: all 373 pool range checks.
- `build/library-research/mic-validation.json`: channel declaration checks.
- `build/library-research/catalog-statistics.json`: observed counts and flag values.
- `build/library-research/mci-tail-validation.json`: 24 footers and selected stream headers.

```sh
python3 scripts/inspect_mimic_library.py /path/to/copied-metadata --output-dir build/library-json
python3 scripts/test_inspect_mimic_library.py
```

A copied prefix is only partial evidence of its source file. The metadata hash
reported by the tool covers the exact encoded metadata prefix; it is not a
claim to have hashed an entire remote DRD or MCI payload.
