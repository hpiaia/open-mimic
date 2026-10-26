# Library sample codec and streaming

This document specifies the sample payload decoded from the owner's Factory,
Tama and eDW libraries. Container metadata and inventory are described in
[instrument-library-files.md](instrument-library-files.md). Microphone order,
gain and routing are separate from the codec; see
[library-microphones.md](library-microphones.md).

The original ARM `mimic_pro` used for native-code evidence has SHA-256
`b12895171c9cb8fd7db067c816ba28b4ae4817869a34654175f3e6dfb683fa8c`.
Addresses in this note refer to that exact executable. Static observations
come from REA/Ghidra and bounded assembly inspection. Isolated ARM emulation
and file comparisons are identified separately below.

## Pool descriptors

For pool item `i`, arrays prefixed `INST0pool0` describe these fields:

| Field | Unit | Observed meaning |
| --- | --- | --- |
| `dedo[i]` | Bytes | Raw float preload offset, relative to the sample-data base. |
| `desc[i]` | Float scalars | Serialized preload length, including 64 leading zero scalars. |
| `dofs[i]` | Bytes | Compressed full-sample offset, relative to the same base. |
| `dlen[i]` | Bytes | Compressed sample length; a multiple of 1,024. |
| `fcnt[i]` | Scalars | Total decoded scalar count, across all channels. |
| `nchn[i]` | Channels | Interleaved PCM channel count for this pool item. |
| `nobu[i]` | Pages | Number of compressed pages already represented by the preload. |

The sample-data base is zero for an external DRD. For an integrated MCI it is
the exact end of the metadata zlib stream, with no intervening header. The
23-byte MCI checksum footer comes after the sample data. Do not add its size
to sample offsets.

`fcnt` is not a per-channel frame count. Physical frames are
`fcnt / nchn`; this division has no remainder for all 22,287 pool descriptors
in the inspected catalog. Mic definitions expand in stored order, with one
channel for mono and two consecutive channels for stereo. The expanded count
equals `nchn` for every inspected instrument.

The first part of a DRD or integrated payload contains raw little-endian
float32 preloads. Compressed records follow it. The compressed record contains
the **whole sample from scalar zero**, including the audio already represented
by its preload. Concatenating the entire compressed record after the preload
would repeat the attack.

## Fixed-size compressed pages

The production disk path calls `CDataCompression::testDecompress1024Bytes` at
`0x5c0294`. A different method named `decompBlock` exists in the same binary;
it is not the decoder selected by this loader/streamer path.

Each independently decoded page occupies exactly 1,024 bytes:

| Page offset | Encoding |
| ---: | --- |
| 0 | Little-endian u32: low 8 bits are delta width; upper 24 bits are a signed predictor seed. |
| 4 | Little-endian u32: decoded scalar count. |
| 8 | Up to 8,128 delta bits, packed into little-endian u32 words. |

Within each u32 payload word, consume bits from most significant to least
significant. Fields can cross word boundaries. Sign-extend each delta using
the page's width and add it to the previous integer with modulo-2^32 ARM
arithmetic. The first delta is added to the predictor seed; the seed itself
is not an output sample. Convert each resulting signed integer to float32
and multiply by `2^-23`.

```text
previous = sign_extend_24(header >> 8)
for every declared scalar:
    delta = read_signed_bits_MSB_first(delta_width)
    previous = add_modulo_2_to_32(previous, delta)
    output = float32(previous) * 2^-23
```

Width 25 is valid: differences between signed 24-bit PCM values can require
an extra bit. The replacement decoder accepts widths 1..32 when the output
count fits within 8,128 bits. Its bounded validation intentionally rejects
malformed pages rather than reproducing unchecked reads from the old code.
A zero-count page emits no scalars.

Pages and their predictors operate on the flat interleaved scalar stream.
They do not carry independent predictors for individual microphones. A page
can end in the middle of a multichannel frame.

## Preload guards and streaming seams

`CSampleItem::DecompresPreloadBuffer` at `0x55d560` begins decoded audio at
scalar offset 64 in a zero-initialized buffer. Its legacy path decodes whole
compressed pages into that buffer. The newer `dav2` loader reads the
serialized float buffer directly; all 373 inspected instruments enable this
path. `loadSinglePoolItem` at `0x5c74d8` marks an item ready only after the
required read succeeds.

The following relation was checked against every sample in four complete
instrument fixtures, totaling 159 pool items:

```text
desc = 64 + sum(decoded_count(page) for page in [0, nobu))
preload[0:64] = all zero float32 bits
preload[64:desc] = decode(full_compressed_sample)[0:desc-64]
compressed_tail_offset = base + dofs + nobu * 1024
```

The guard is **64 scalars**, not 64 frames per channel. After removing it,
72 of those 159 preloads end partway through a multichannel frame. For
example, Cowbell pool item 0 has 24,844 real preload scalars across five
channels: the seam occurs after channel 3 of a frame, with channel 4 still
to come from the compressed tail. Restarting the channel index at zero at
this seam would permanently misroute microphone channels.

A replacement source should therefore retain a flat scalar cursor and any
incomplete frame across preload, decoded-page and worker-buffer boundaries.
Only complete frames are exposed to the common multi-mic voice cursor. All
microphones then share the same playback position and pitch advance. No
per-mic trimming, independent random selection or independent clock is needed.

`CCompressedDatasource::Start` at `0x5bfb98` calculates the compressed read
offset from the starting page and sample offset. Its next-read method at
`0x5bfc24` advances by `0x20000` bytes, a 128 KiB compressed read request.
`CDecompressedDataSource::ReplyDataPtr` at `0x5c14a8` requests the next worker
buffer before reaching the current buffer end, retains overlap around the
switch, and returns no data when the expected buffer is not ready. The
original overlap and seek-index bookkeeping are implementation details; a
different worker-buffer design must still preserve the exact scalar sequence,
interpolation look-ahead, readiness behavior and real-time deadline.

## Implemented decoder and offline tools

`software/common/SampleDecoder.h` and `.cpp` contain an allocation-free C++17
page decoder. The caller supplies a complete page and an output buffer.
Bounds failures throw on a loader/streaming worker; this function is not an
instruction to perform disk I/O or exception handling in the audio callback.
The `mimic-sample-decode` tool decodes an explicit file range to little-endian
float32 data and refuses an already existing output file.

```sh
cmake -S software -B build/library-tests -DMIMIC_BUILD_UI=OFF -DMIMIC_USE_JUCE=OFF
cmake --build build/library-tests
ctest --test-dir build/library-tests --output-on-failure

python3 scripts/validate_mimic_samples.py /path/to/Instrument.bin \
  --data /path/to/Instrument.drd \
  --decoder build/library-tests/mimic-sample-decode \
  --output build/library-research/instrument-validation.json

python3 scripts/export_mimic_sample.py /path/to/Instrument.bin \
  --data /path/to/Instrument.drd --sample 0 \
  --decoder build/library-tests/mimic-sample-decode \
  --output-dir /path/to/private-preview
```

For an MCI, pass it as metadata and omit `--data`. The WAV exporter splits
one selected pool item into aligned mono/stereo mic files, preserving raw
24-bit PCM values. It does not apply layer/sample gain, envelopes, tuning,
kit overrides or effects; these WAVs are extraction previews, not a render
of the original sampler. The default 44,100 Hz comes from the original
engine; the codec itself stores a scalar sequence without a sample-rate field.

## Validation and scope

| Fixture | Format | Full samples decoded | Preload byte comparisons |
| --- | --- | ---: | ---: |
| Cowbell Pearl 10 PCB | BIN + DRD | 30 | 30 |
| Snare 06 SSD | BIN + DRD | 37 | 37 |
| Kick Tma 22 SCL | BIN + DRD | 28 | 28 |
| eDW DWCP 20x18 Direct | Integrated MCI | 64 | 64 |
| Total | Three library families | 159 | 159 |

Every decoded length equals `fcnt`. Every compared preload scalar after its
64-value guard is byte-identical to the corresponding decoded prefix. All
159 `nobu` values also identify exactly the serialized preload pages. These
prefix comparisons do not independently prove the un-preloaded tails.

For a separate oracle, `scripts/emulate_mimic_sample_decoder.py` executes only
the original decoder's ARM/VFP instruction range in isolated Unicorn 2.1.4
memory. It locks the application SHA-256, permits no host-I/O hooks, rejects
execution outside the declared function and limits each call to one million
instructions. The replacement output matches the original byte for byte for
32 synthetic widths and 28 selected real pages from two fixtures, including
middle/end pages. The two retained reports repeat the same 32 synthetic
cases; they are not 92 distinct cases. This is isolated function emulation,
not observation of the original application running on Mimic hardware.

REA evidence IDs for the production decoder, preload loader and pool loader:
`ev_2494c4f5c6be62be645f1ab0ebbae670da2bdb1bfe2641d7e9298b8dae801b52`;
streaming readers:
`ev_1db9b4ccdb39597dfe1603f9ab1f0eebdfa1a75b15b9e51e6febb5fa1cb03758`.
Ignored local reports are `*-codec-validation.json`,
`*-decoder-original-arm-comparison.json` and `preload-seams.json` under
`build/library-research`. Original samples, generated PCM and WAV previews
remain outside Git. The complete 25 GB library has not been decoded, nor has
the replacement been compared against a full original mixed audio render.
