# `.drd` Audio Codec Notes

Date: 2026-07-08

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

## Key finding: it's bit-depth reduction, and 32-bit is uncompressed

`DecopressRIFFDataToFloat` computes `samples = byteLen / (bitdepth/8)` then branches:

- `bitdepth == 32` → **direct 4-byte word copy** src→dst (an unrolled `ldr/str`
  loop at 0x5c0d94). No decompression — the payload *is* float32 PCM.
- `bitdepth == 16` → int16→float conversion path (0x5c1090).
- `bitdepth == 24` (checked via `bic r,#8` folding 24→16) → int24→float path.

So the "codec" is primarily **sample-width reduction** (store 16/24-bit instead of
32-bit float), wrapped in fixed 768/1024-byte blocks. This matches the measured
SCM rate of ~1.5 bytes per interleaved sample (below 16-bit, so the block path adds
a little more than plain 16-bit — likely per-block scaling; not yet fully traced).

## Why this matters for Open Mimic

**There is very likely an uncompressed authoring path.** If a new instrument stores
audio as **float32 with the chunk's bit-depth field = 32**, the engine's decoder
just copies it — no need to reproduce Pearl's block codec at all. That would let the
instrument compiler ship working multi-mic instruments without reversing the lossy/
block details.

Open questions before relying on it:
1. **Where is the per-chunk bit-depth field?** It's an argument to
   `DecopressRIFFDataToFloat`; trace its source — the `.drd` chunk header
   (`[u32 22][u32 369][0,0,0,0]…`) or a `pool0` field (`desc`/`nobu`/`dedo`).
2. Does the loader accept a chunk declaring bit-depth 32, or do factory libraries
   always use 16/24? (SCM chunks are compressed, so this needs a device test or a
   deeper read of the load path.)
3. Confirm `768` vs `1024` block selection and whether `wave` vs the 1024 path is
   tied to bit-depth or to a codec flag.

## Next steps

1. Trace the bit-depth argument back to its `.drd`/`pool` source field.
2. Build a minimal float32/bit-depth-32 `.drd` + matching `.bin` and test load on
   real hardware (needs the SSD image / a device).
3. If 32-bit-uncompressed is rejected, black-box the 16/24-bit block codec with the
   Editor (`tools/gen_test_vectors.py` + `tools/drd_probe.py`) and/or finish
   disassembling `DecopressRIFFDataToFloat` + `testCompress`.
