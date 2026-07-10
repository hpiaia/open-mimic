# Open Mimic

Open Mimic is an open-source instrument compiler and reverse-engineering project
for the **Pearl Mimic Pro** electronic drum module.

The project can build importable instruments with discrete close, overhead, and
room microphones—functionality supported by the module but unavailable in the
public Pearl Instrument Editor.

## Current status

The complete six-channel authoring path has been validated on a real Mimic Pro:

- custom 48 kHz PCM16 WAV input;
- two mono close microphones;
- stereo overhead microphones;
- stereo room microphones;
- Mimic's native 1024-byte streaming codec;
- velocity layers and round robins;
- `.bin`, `.drd`, preload, and checksum generation;
- USB library import and stable playback.

The compiler creates instrument libraries only. It does **not** modify or replace
the module firmware.

## Rust compiler

The production compiler is a reusable Rust crate under [`compiler/`](./compiler).
It provides both a CLI and a library API intended for direct integration into a
Tauri desktop application.

Supported features:

- 1–16 velocity layers;
- 1–16 round-robin samples per layer;
- template-defined mono/stereo microphone layouts;
- independent close, overhead, and room channels;
- instrument and library names, type, MIDI note, and mic labels/volumes;
- optional images normalized to 94×63 RGBA PNG;
- deterministic native `.drd` encoding and Mimic checksum generation.

See the [compiler documentation](./compiler/README.md) for the Rust API and Tauri
command example.

## Quick start

Requirements:

- a current Rust toolchain;
- Python 3 only for generating the diagnostic example WAVs;
- a legally obtained multi-mic template instrument. The example expects
  `assets/Tama SCM Steve Mackrill.lib/instruments/Kick Tma 22 SCL.bin` and its
  matching library files.

Generate the example WAVs:

```sh
python3 tools/gen_multimic_test_wavs.py
```

Compile the example instrument:

```sh
cargo run --release --manifest-path compiler/Cargo.toml -- \
  examples/multimic-kick.json
```

The result is written to:

```text
build/Open Mimic Compiler Example.lib/
```

For the SCM kick template, every input WAV uses this interleaved channel order:

```text
0  Kick In
1  Kick Out
2  Overheads left
3  Overheads right
4  Room left
5  Room right
```

All WAVs assigned to an instrument must be 48 kHz, 16-bit PCM and match the
template's channel count and ordering.

## Validation

Run the Rust checks with:

```sh
cargo fmt --manifest-path compiler/Cargo.toml --check
cargo clippy --locked --manifest-path compiler/Cargo.toml --all-targets -- -D warnings
cargo test --locked --manifest-path compiler/Cargo.toml
```

The test suite covers storage round trips, bit-exact codec round trips, and a
six-channel library integration build when the SCM template asset is available.
The independent Python tools can also parse and verify compiler output.

## Repository layout

- [`compiler/`](./compiler) — Rust compiler library and CLI.
- [`examples/`](./examples) — compiler manifests.
- [`tools/`](./tools) — format parsers, research utilities, test-vector generators,
  and Ghidra scripts.
- [`docs/research/`](./docs/research) — format, codec, firmware, editor, and hardware
  analysis.
- [`HANDOFF.md`](./HANDOFF.md) — detailed project status and research map.

Useful research documents:

- [Instrument library format](./docs/research/instrument-format.md)
- [DRD codec notes](./docs/research/drd-codec-notes.md)
- [Instrument Editor analysis](./docs/research/instrument-editor-r106-analysis.md)
- [Firmware 1.4.18 analysis](./docs/research/mimic-pro-firmware-1.4.18-analysis.md)
- [Reverse-engineering map](./docs/research/reverse-engineering-map.md)

## Current limitations

- Microphone topology and trigger behavior are inherited from a compatible
  template instrument.
- The current manifest compiler targets one articulation; multi-articulation
  instruments and complete kit compilation are planned extensions.
- Retaining the template image is hardware-tested. Normalized custom thumbnails
  are implemented but still need a dedicated hardware compatibility test.

## Assets, licensing, and safety

The `assets/` and `build/` directories are intentionally excluded from Git.
Firmware binaries, commercial libraries, generated audio, and compiled packages
must not be committed or redistributed without permission.

This repository contains original tooling and research based on publicly
downloadable firmware, software, and manuals inspected as data. Users must supply
their own legally obtained templates and audio samples.
