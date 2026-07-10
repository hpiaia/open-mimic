# `.drd` Audio Codec Notes

Date: 2026-07-08

## Hardware test: raw float32 construction crashes during playback

On 10 July 2026, a hand-built library cloned the SCM kick descriptor, replaced
all 28 `.drd` chunks with six-channel interleaved float32 PCM, and updated
`dofs`, `dlen`, `fcnt`, and `nchn`. The Mimic imported the library successfully,
listed the kick, and displayed it in the instrument selector. Striking the kick
then crashed the module and required a power cycle.

This separates container validation from audio validation:

- the `.lib`, checksum, `.bin`, instrument registration, mic layout, and embedded
  image were accepted;
- raw float32 audio is **not** selected merely because
  `dlen == fcnt * nchn * 4`;
- retaining the SCM `desc`, `nobu`, `dedo`, and/or chunk framing while replacing
  the payload is invalid and unsafe to play.

Do not distribute or retest that float32 package. Before another hardware build,
trace the decoder-selection/bit-depth argument to its stored field and reproduce
the required chunk/block framing.

## Installed-library preload and streaming layout

The 1.4.18 loader maps the installed-library pool arrays into `CSampleItem` as
follows:

| Pool array | `CSampleItem` offset | Meaning |
| --- | ---: | --- |
| `dlen` | `+0x08` | compressed streaming byte length |
| `fcnt` | `+0x18` | total interleaved sample count (not WAV frames) |
| `nchn` | `+0x1c` | channel count |
| `desc` | `+0x10` | raw preload length in float32 words |
| `nobu` | `+0x14` | compressed/preload block bookkeeping |
| `dofs` | `+0x34` | compressed streaming offset in `.drd` |
| `dedo` | `+0x38` | raw preload offset in `.drd` |

`loadSinglePoolItem` reads `desc * 4` bytes from `dedo` directly into a float
buffer. SCM's preloads begin with exactly 64 zero floats. Across the SCM kick,
`sum(desc) * 4 == 2,827,288`, exactly equal to the first `dofs`; therefore the
`.drd` is a raw-preload region followed by compressed streaming chunks.

For the six-channel SCM kick, `fcnt / nchn / 48000` gives about 1.493 seconds,
confirming that `fcnt` counts interleaved samples. The failed experiment set it
to WAV frames and therefore underreported the sample count by 6x.

`CDecompressedDataSource::ReplyDataPtr` plays from the raw preload first while it
prefetches the compressed tail. A short-sample experiment can therefore place
the complete sound after the 64-float guard in the preload, while retaining
known-valid SCM compressed chunks as a safety tail. This avoids feeding unknown
bytes to the custom decoder but still requires hardware validation.

Hardware testing disproved the last paragraph on production settings. `INST0dav2`
is additionally gated by the zero-initialized global
`mimic_pro_global_is_decomp_preload_enabled`. The only writer is a hidden
`CSettings_DevPane` toggle. With the production default, the loader ignores
`desc`/`dedo` and uses the compressed `dofs`/`dlen` stream. This is why preload
experiments played the retained SCM tail and none of the replacement WAV.

## 1024-byte streaming block codec

The standard streaming codec is now decoded and encoded. Each block is exactly
1024 bytes:

```text
u32 header = (signed_24_bit_starting_predictor << 8) | residual_bit_width
u32 decoded_sample_count
1016 bytes of MSB-first packed signed deltas
```

For every delta, the decoder sign-extends `residual_bit_width` bits, adds it to
the predictor, and emits `predictor / 2^23` as float32. The maximum count is
`floor(8128 / residual_bit_width)`. Thus an observed SCM header `(22, 369)` is
exact: `floor(8128 / 22) == 369`.

Validation:

- synthetic encode/decode is bit-exact;
- every decoded SCM chunk produces exactly its metadata `fcnt` samples;
- the SCM raw preload after its 64-float guard is bit-identical to the prefix of
  the decoded streaming data;
- `tools/drd_codec.py` implements the verified encoder and decoder.

Hardware validation on 10 July 2026 succeeded completely. The package
`Open Mimic Encoded MultiMic Test` contained 28 newly encoded copies of a
1.5-second, six-channel diagnostic WAV (two mono close channels, stereo OH, and
stereo room), with no SCM streaming blocks retained. It imported successfully,
appeared as a kick, played all generated signals correctly, and did not crash.

This confirms the complete authoring path for custom installed multi-mic
instruments: `.bin` metadata, raw preload layout, 1024-byte streaming encoding,
`.drd` offsets/lengths, checksum generation, import, routing, and playback.

From disassembly of the firmware decoder (`/private/tmp/open-mimic-fw/mimic_app_1_4_18.elf`,
ARM32, not stripped) with `objdump`, plus the Editor encoder
(`Mimic Instrument Editor.exe`, x86-64 JUCE). This is the audio-codec half of the
instrument format; the container half is in `instrument-format.md`.

## Decode path (firmware)

```
CDiskStreamer::doDecomp        @ 0x5295f8   1024-byte input blocks
CDiskStreamer::doDecomp_wave   @ 0x529888   768-byte input blocks
      |
      v  (both call, output = float32 PCM)
CDataCompression::DecopressRIFFDataToFloat(bitdepth, byteLen, ..., float* dst, int& n)  @ 0x5c0d08
CDataCompression::testDecompress1024Bytes(u32* src, float* dst, int& n, u32*)           @ 0x5c0294
```

Encoder side (Editor + firmware): `CDataCompression::testCompress` @ 0x5bfe50 (1084 B),
`CSampleItem::ConvertRIFFDataToFloat` @ 0x55d98c.

## Separate wave/raw conversion path

`DecopressRIFFDataToFloat` computes `samples = byteLen / (bitdepth/8)` then branches:

- `bitdepth == 32` → **direct 4-byte word copy** src→dst (an unrolled `ldr/str`
  loop at 0x5c0d94). No decompression — the payload *is* float32 PCM.
- `bitdepth == 16` → int16→float conversion path (0x5c1090).
- `bitdepth == 24` (checked via `bic r,#8` folding 24→16) → int24→float path.

This function belongs to the separate wave/raw streaming path. It is not selected
for normal installed `.bin` + `.drd` instruments.

## Rejected raw-float experiment

Changing only pool lengths and inserting float32 data caused a reproducible playback
crash. A preload-only attempt was also invalid because the production global
`mimic_pro_global_is_decomp_preload_enabled` defaults to false. The working compiler
therefore uses the fully understood 1024-byte installed-instrument codec documented
above.

## Current implementation

- Python research/reference codec: `tools/drd_codec.py`
- Production Rust codec and compiler: `compiler/src/codec.rs` and
  `compiler/src/compile.rs`
- Hardware-validated example: `examples/multimic-kick.json`
