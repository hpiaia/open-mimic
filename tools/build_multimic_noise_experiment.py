#!/usr/bin/env python3
"""Build the rejected six-channel float32 Mimic hardware experiment.

The firmware decoder has an uncompressed 32-bit branch. This builder exercises
that path by replacing every sample chunk in an SCM kick descriptor with the same
six-channel diagnostic WAV converted to interleaved float32. Hardware accepted
the container but crashed when the instrument was struck. This script is retained
only to reproduce that failed test; it refuses to build unless explicitly opted in.
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import struct
import wave
from pathlib import Path

from build_multimic_proof import checksum_header, encode_checksum, encode_storage
from mimic_kv import instrument_summary, load_storage, parse_checksum, verify_checksum


def wav_float32(path: Path) -> tuple[bytes, int, int]:
    with wave.open(str(path), "rb") as source:
        channels = source.getnchannels()
        frames = source.getnframes()
        if source.getframerate() != 48_000 or source.getsampwidth() != 2:
            raise SystemExit("test WAV must be 48 kHz, 16-bit PCM")
        pcm = source.readframes(frames)
    values = struct.unpack(f"<{frames * channels}h", pcm)
    output = bytearray()
    for value in values:
        output += struct.pack("<f", value / 32768.0)
    return bytes(output), frames, channels


def build(source_bin: Path, wav_path: Path, output_root: Path) -> Path:
    source_checksum = source_bin.parent.parent / "checksum.dat"
    storage = load_storage(source_bin)
    before = instrument_summary(storage)
    audio, frames, channels = wav_float32(wav_path)

    expected_channels = sum(2 if mic["stereo"] else 1 for mic in before["mics"])
    if channels != expected_channels:
        raise SystemExit(
            f"WAV has {channels} channels; descriptor requires {expected_channels}"
        )

    prefix = "INST0pool0"
    count = storage.u32[f"{prefix}psz"]
    chunk_size = len(audio)
    storage.u32_arrays[f"{prefix}dofs"] = [i * chunk_size for i in range(count)]
    storage.u32_arrays[f"{prefix}dlen"] = [chunk_size] * count
    storage.u32_arrays[f"{prefix}fcnt"] = [frames] * count
    storage.u32_arrays[f"{prefix}nchn"] = [channels] * count

    name = "Open Mimic Six Channel Noise Test"
    library_name = "Open Mimic Float32 MultiMic Test"
    storage.strings["INST0insnam"] = name
    storage.strings["INST0insdat"] = name
    storage.strings["INST0libnam"] = library_name
    storage.strings["INST0insimj"] = ""
    storage.strings["INST0micinf0micn"] = "110Hz Kick In"
    storage.strings["INST0micinf1micn"] = "220Hz Kick Out"
    storage.strings["INST0micinf2micn"] = "Noise OH"
    storage.strings["INST0micinf3micn"] = "440-880Hz Room"
    for idx in range(before["mic_count"]):
        storage.f32[f"INST0micinf{idx}micv"] = 1.0

    lib_dir = output_root / f"{library_name}.lib"
    if lib_dir.exists():
        shutil.rmtree(lib_dir)
    instruments = lib_dir / "instruments"
    instruments.mkdir(parents=True)
    out_bin = instruments / f"{name}.bin"
    out_drd = instruments / f"{name}.drd"
    out_bin.write_bytes(encode_storage(storage))
    with out_drd.open("wb") as output:
        for _ in range(count):
            output.write(audio)
    (lib_dir / "libver.mimicinfo").write_text("10 July 2026", encoding="ascii")

    entries = [
        (out_bin.name, hashlib.md5(out_bin.read_bytes()).digest()),
        (out_drd.name, hashlib.md5(out_drd.read_bytes()).digest()),
    ]
    header = checksum_header(source_checksum)
    (lib_dir / "checksum.dat").write_bytes(encode_checksum(header, entries))

    after = instrument_summary(load_storage(out_bin))
    verification = verify_checksum(lib_dir, parse_checksum(lib_dir / "checksum.dat"))
    assert after["pool"]["raw_pcm_byte_match_count"] == count
    assert after["pool"]["spans_drd"] is True
    assert not verification["missing"] and not verification["mismatched"]

    print(f"built: {lib_dir}")
    print(f"chunks: {count} x {frames} frames x {channels} channels")
    print(f"chunk bytes: {chunk_size} (float32 identity confirmed in metadata)")
    print(f"DRD bytes: {out_drd.stat().st_size}")
    print(f"checksums verified: {len(verification['ok'])}")
    print("status: EXPERIMENTAL — requires Mimic hardware validation")
    return lib_dir


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_bin", type=Path)
    parser.add_argument("wav", type=Path)
    parser.add_argument("--output-root", type=Path, default=Path("build"))
    parser.add_argument(
        "--reproduce-unsafe-crash-test",
        action="store_true",
        help="explicitly reproduce the package known to crash Mimic playback",
    )
    args = parser.parse_args()
    if not args.reproduce_unsafe_crash_test:
        raise SystemExit(
            "refusing to build: this construction imports but crashes Mimic playback; "
            "pass --reproduce-unsafe-crash-test only for offline research"
        )
    build(args.source_bin, args.wav, args.output_root)


if __name__ == "__main__":
    main()
