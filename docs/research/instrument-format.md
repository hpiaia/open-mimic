# Mimic Pro Instrument Library Format

Date: 2026-07-08

Reverse-engineered from the downloaded SCM Mimic Library at
`assets/SCM Mimic Library/`. Public release/install facts are tracked in
[scm-mimic-library-source-notes.md](./scm-mimic-library-source-notes.md); firmware
symbols are tracked in [mimic-pro-firmware-1.4.18-analysis.md](./mimic-pro-firmware-1.4.18-analysis.md).

Tooling: [tools/mimic_kv.py](../../tools/mimic_kv.py) decodes `mimic_storage`
containers and `checksum.dat`.

## Current Answer

The stock Mimic format supports multi-mic instruments. The SCM library uses
separate close/direct, overhead, and room mic definitions in the instrument
metadata, and the kit files preserve those mic names. The public Instrument
Editor appears to be the limitation, not the playback engine or on-disk format.

For Open Mimic, this means a multi-mic importer/compiler is plausible, but it
needs two pieces:

- a `mimic_storage` writer for `.bin`, `.kit`, and `checksum.dat`
- a correct encoder/decoder for the `.drd` audio chunk format

The second piece is still unresolved. The `.drd` files are definitely addressed
audio payloads, but they are not raw interleaved float32 PCM.

## Source Package

Local layout:

```text
Tama SCM Steve Mackrill.lib/
  checksum.dat
  libver.mimicinfo
  instruments/
    <Instrument>.bin
    <Instrument>.drd
  kits/
    <Preset>.kit
```

Observed counts:

- 13 `.bin` instrument descriptors
- 13 matching `.drd` audio payload files
- 14 `.kit` presets
- `libver.mimicinfo` contains `30 June 2023`
- `checksum.dat` validates all 26 `.bin`/`.drd` instrument files

The public eDrum Workshop page says "12 new instruments"; the local package has
13 `.bin`/`.drd` basename pairs. The visible filenames suggest that the public
"14x5.5 SCM Snare (wires on & wires off)" line maps to two local instruments:
`Snare Tma 5.5 SCL` and `Snare Tma 5.5 SWO`.

## `checksum.dat`

Confirmed layout:

```text
c_string magic = "mimic_checksum_list"
u32 little-endian version = 1
u32 little-endian header_len = 100
header_len bytes, meaning not decoded yet
u32 little-endian entry_count
zlib stream:
  repeated entry_count times:
    c_string filename
    16 bytes MD5 digest
```

Validation result from `tools/mimic_kv.py --verify-md5`:

```text
declared entries: 26
decoded entries: 26
MD5 ok: 26
missing: 0
mismatched: 0
```

The checksum list covers the 13 `.bin` and 13 `.drd` files. It does not list the
14 `.kit` presets or `libver.mimicinfo`.

## `mimic_storage`

Both `.bin` and `.kit` use this container:

```text
offset 0x00: c_string magic = "mimic_storage"
offset 0x0e: u32 little-endian format version, observed 3
offset 0x12: u32 little-endian decompressed payload size
offset 0x16: zlib stream
```

The decompressed payload is a typed key/value store. Keys are NUL-terminated
ASCII strings such as `INST0micinf2micpos` and `INST0artic0veloLay9smpidx`.

Confirmed payload section order:

```text
u32 count
  count x { u32 key_len; key bytes including NUL; u32 value }

u32 count
  count x { u32 key_len; key bytes including NUL; u32 item_count; u32[item_count] }

u32 count
  count x { u32 key_len; key bytes including NUL; f32 value }

u32 count
  count x { u32 key_len; key bytes including NUL; u32 item_count; f32[item_count] }

u32 count
  count x { u32 key_len; key bytes including NUL; u8 value }

u32 count
  count x { u32 key_len; key bytes including NUL; u32 byte_count; bytes }
  observed count is 0 in this library

u32 count
  count x { u32 key_len; key bytes including NUL; u32 byte_count; string bytes }

u32 blob_len
blob bytes
```

For instruments, the final blob is a PNG thumbnail. For kits, `blob_len` is `0`.

Example section counts:

| File | u32 | u32 arrays | f32 | f32 arrays | byte | strings | blob |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `Kick Tma 22 SCL.bin` | 61 | 41 | 93 | 17 | 21 | 10 | 5372 byte PNG |
| `Snare Tma 5.5 SCL.bin` | 289 | 215 | 301 | 95 | 36 | 15 | 5026 byte PNG |
| `Hi-Hat Mnl 14 BZM.bin` | 735 | 550 | 716 | 309 | 65 | 25 | 4078 byte PNG |
| `SCM 12 Natural.kit` | 1572 | 26 | 3802 | 292 | 2119 | 746 | 0 |

## Instrument Metadata

Important instrument-level keys:

- `INST0insnam`: instrument name
- `INST0instyp`: type, for example `Kick`, `Snare`, `Tom`, `Hat`, `Ride`, `Cymbal`
- `INST0libnam`: library name, `Mackrill's Kit` in this package
- `INST0insimj`: source image path from the developer build machine
- `INST0numart`: articulation count
- `INST0micnt`: mic count
- `INST0zonecnt`: zone count
- `INST0zoneTypeN` and `INST0zoneArtIdN`: trigger zone to articulation mapping

The SCM instruments embed Windows paths such as:

```text
E:\_welsh_steve_mimic_lib\steve_edit\steve\STEVE MACKRILL_Kick/KickDrum22\MacKick22.png
```

That is strong evidence these files were produced by an internal build/import
tool rather than the public editor.

## Multi-Mic Model

Per-mic keys use `INST0micinfN...`:

- `micn`: mic name string
- `micpos`: position enum
- `mict`: mic type enum
- `isst`: stereo flag, stored in the byte section
- `micen`: enabled flag, stored in the byte section
- `micv`: volume, stored as float32
- `micp`, `micpz`, `micrs`, `mictn`, `att`, `sus`, `rel`, `dmp`: mix/envelope values

Observed `mict` values:

| `mict` | Meaning inferred from names and stereo flags |
| ---: | --- |
| 0 | close/direct mic |
| 1 | ambient mic |

Observed global `micpos` values:

| `micpos` | Observed use |
| ---: | --- |
| 0 | snare top, tom close, hat close, ride close |
| 1 | snare bottom |
| 2 | kick out |
| 4 | kick in |
| 5 | overhead |
| 6 | room |

Instrument summary:

| Instrument | Mics | `pool0nchn` | Confirmed mic names |
| --- | ---: | ---: | --- |
| Crash L/R | 2 | 4 | OH stereo, Room stereo |
| Hi-Hat | 3 | 5 | Hi-Hat mono, OH stereo, Room stereo |
| Kick | 4 | 6 | Kick In mono, Kick Out mono, OH stereo, Room stereo |
| Ride | 3 | 5 | Ride mono, OH stereo, Room stereo |
| Snare | 4 | 6 | Snare Top mono, Snare Btm mono, OH stereo, Room stereo |
| Tom | 3 | 5 | Tom close mono, OH stereo, Room stereo |

This lines up exactly with channel count:

```text
pool0nchn = sum(1 for mono close/direct mics) + sum(2 for stereo ambient mics)
```

Examples:

- Kick: `1 + 1 + 2 + 2 = 6`
- Snare: `1 + 1 + 2 + 2 = 6`
- Tom: `1 + 2 + 2 = 5`
- Crash: `2 + 2 = 4`

## Articulations, Velocity Layers, Round Robin

Articulation keys use `INST0articN...`:

- `artn`: articulation name string
- `artid`: articulation id
- `noteon`: MIDI note
- `numlay`: velocity-layer count
- `veloLayVminvel`: minimum velocity threshold for layer `V`
- `veloLayVnums`: number of round-robin samples in layer `V`
- `veloLayVsmpidx`: u32 array of sample indexes into `pool0`

`smpidx` arrays are 16 entries long and unused slots are padded with
`0xffffffff`.

Example from `Snare Tma 5.5 SCL.bin`:

| Articulation | MIDI note | Velocity layers |
| --- | ---: | ---: |
| Snare Center | 38 | 10 |
| Snare Rimshot | 40 | 5 |
| Snare Rimshot Edge | 33 | 4 |
| Snare Side | 34 | 6 |
| Snare Sidestick | 37 | 5 |
| Snare Rimclick | 39 | 5 |

The same file maps 6 zones to those 6 articulations:

```text
zoneType: 1, 3, 5, 6, 4, 2
zoneArtId: 0, 1, 2, 3, 4, 5
```

## Sample Pool and `.drd`

Sample pool keys use `INST0pool0...`:

- `psz`: sample chunk count
- `dofs`: u32 array of offsets into the `.drd`
- `dlen`: u32 array of chunk lengths in the `.drd`
- `fcnt`: u32 array of frame counts
- `nchn`: u32 array of channel counts
- `nobu`: u32 array, meaning not decoded yet
- `dedo`: u32 array, meaning not decoded yet
- `desc`: u32 array, likely descriptor/string offsets, not decoded yet
- `svol`: f32 array of per-sample volume values
- `rmsenvN...`: RMS envelope data for UI/dynamics

For every SCM instrument, `max(dofs[i] + dlen[i])` equals the matching `.drd`
file size. That proves the `.bin` descriptor addresses the entire `.drd` payload.

Observed pool sizes:

| Instrument | `pool0psz` | `pool0nchn` | `.drd` span |
| --- | ---: | --- | --- |
| Crash L | 58 | `[4]` | exact |
| Crash R | 65 | `[4]` | exact |
| Hi-Hat | 420 | `[5]` | exact |
| Kick | 28 | `[6]` | exact |
| Ride | 80 | `[5]` | exact |
| Snare 5 STS | 128 | `[6]` | exact |
| Snare 5.5 SCL | 173 | `[6]` | exact |
| Snare 5.5 SWO | 152 | `[6]` | exact |
| Toms | 60-65 | `[5]` | exact |

Important correction: the `.drd` chunks are not raw interleaved float32 PCM.
For Kick chunk 0:

```text
fcnt = 429972
nchn = 6
dlen = 658432
raw float32 bytes would be 429972 * 6 * 4 = 10319328
```

`tools/mimic_kv.py` reports `raw_pcm_byte_match_count = 0` for the Kick. The
first chunk also does not begin with RIFF/WAV, Ogg, FLAC, gzip, zlib, bzip2, or
lzma magic. Firmware symbols include `CCompressedDatasource`,
`CDecompressedDataSource`, `CDiskStreamer::doDecomp`, and
`CDiskStreamer::doDecomp_wave`, so the next target is that decompression path.

## Kit Files

`.kit` files use the same `mimic_storage` container. They contain mix, pad,
trigger, mic-name, and instrument-slot strings.

Useful string keys:

- `insstCS<slot>lsin`: selected instrument name
- `insstCS<slot>lsln`: selected library name
- `CME<slot>miNmicn`: mic names shown by the kit/mixer
- `TPAD...`: pad preset and zone names

Example from `SCM 12 Natural.kit`:

```text
insstCSKicklsin      = Kick Tma 22 SCL
insstCSKicklsln      = Mackrill's Kit
CMEKickmi0micn       = Kick In
CMEKickmi1micn       = Kick Out
CMEKickmi2micn       = OH
CMEKickmi3micn       = Room
```

Some SCM kit presets reference non-SCM installed instruments, for example
`Splash X Sbn 09 AXM` and `China R Sbn 19 AXT` from `SSD 5`. That means a kit
compiler must preserve external library references, not assume every referenced
instrument is bundled in the same `.lib` folder.

## Open Tasks

1. Disassemble and document `CCompressedDatasource`, `CDecompressedDataSource`,
   `CDiskStreamer::doDecomp`, and `CDiskStreamer::doDecomp_wave`.
2. Decode the `.drd` chunk codec and produce a WAV export proof for one sample.
3. Implement a read/write `mimic_storage` round trip and byte-compare against an
   existing `.bin` and `.kit`.
4. Implement `checksum.dat` writing and verify the stock import screen accepts a
   rebuilt library with unchanged audio.
5. Build the real compiler: multi-mic WAV sets plus articulation/velocity/round
   robin metadata to `.bin`/`.drd`/`.kit`/`checksum.dat`.
