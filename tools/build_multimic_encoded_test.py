#!/usr/bin/env python3
"""Build a six-channel Mimic instrument using the verified DRD block codec."""

from __future__ import annotations

import argparse
import hashlib
import shutil
import struct
import wave
from pathlib import Path

from build_multimic_proof import checksum_header, encode_checksum, encode_storage
from drd_codec import decode, encode
from mimic_kv import instrument_summary, load_storage, parse_checksum, verify_checksum


def wav_int24(path: Path) -> tuple[list[int], int, int]:
    with wave.open(str(path), "rb") as source:
        channels = source.getnchannels()
        frames = source.getnframes()
        if source.getframerate() != 48_000 or source.getsampwidth() != 2:
            raise SystemExit("test WAV must be 48 kHz, 16-bit PCM")
        pcm = source.readframes(frames)
    pcm16 = struct.unpack(f"<{frames * channels}h", pcm)
    # PCM16 maps exactly into the decoder's signed-24 domain.
    return [sample << 8 for sample in pcm16], frames, channels


def build(source_bin: Path, wav_path: Path, output_root: Path) -> Path:
    source_checksum = source_bin.parent.parent / "checksum.dat"
    storage = load_storage(source_bin)
    before = instrument_summary(storage)
    samples, frames, channels = wav_int24(wav_path)
    expected_channels = sum(2 if mic["stereo"] else 1 for mic in before["mics"])
    if channels != expected_channels:
        raise SystemExit(f"WAV has {channels} channels; descriptor requires {expected_channels}")

    encoded = encode(samples, width=24)
    if decode(encoded) != samples:
        raise SystemExit("internal DRD round-trip failed")

    prefix = "INST0pool0"
    count = storage.u32[prefix + "psz"]
    # Match the verified SCM preload shape: 64 zero floats followed by a prefix
    # of the exact same signed-24 samples stored by the streaming codec.
    preload_samples = min(len(samples), 24_000)
    preload = bytes(64 * 4) + b"".join(
        struct.pack("<f", sample / float(1 << 23)) for sample in samples[:preload_samples]
    )
    desc = len(preload) // 4

    dedo: list[int] = []
    cursor = 0
    for _ in range(count):
        dedo.append(cursor)
        cursor += len(preload)
    dofs: list[int] = []
    for _ in range(count):
        dofs.append(cursor)
        cursor += len(encoded)

    storage.u32_arrays[prefix + "dedo"] = dedo
    storage.u32_arrays[prefix + "desc"] = [desc] * count
    storage.u32_arrays[prefix + "dofs"] = dofs
    storage.u32_arrays[prefix + "dlen"] = [len(encoded)] * count
    storage.u32_arrays[prefix + "fcnt"] = [len(samples)] * count
    storage.u32_arrays[prefix + "nchn"] = [channels] * count
    storage.u32_arrays[prefix + "nobu"] = [64] * count

    name = "Open Mimic Encoded Six Channel Test"
    library_name = "Open Mimic Encoded MultiMic Test"
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
    # Deliberately retain the byte-identical SCM PNG until custom thumbnail
    # compatibility is investigated separately.

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
            output.write(preload)
        for _ in range(count):
            output.write(encoded)
    (lib_dir / "libver.mimicinfo").write_text("10 July 2026", encoding="ascii")

    entries = [(out_bin.name, hashlib.md5(out_bin.read_bytes()).digest()),
               (out_drd.name, hashlib.md5(out_drd.read_bytes()).digest())]
    (lib_dir / "checksum.dat").write_bytes(encode_checksum(checksum_header(source_checksum), entries))

    rebuilt = load_storage(out_bin)
    verification = verify_checksum(lib_dir, parse_checksum(lib_dir / "checksum.dat"))
    assert rebuilt.u32_arrays[prefix + "fcnt"] == [frames * channels] * count
    assert rebuilt.u32_arrays[prefix + "dlen"] == [len(encoded)] * count
    assert len(encoded) % 1024 == 0
    assert max(o + len(encoded) for o in dofs) == out_drd.stat().st_size
    assert not verification["missing"] and not verification["mismatched"]
    print(f"built: {lib_dir}")
    print(f"audio: {frames} frames, {len(samples)} interleaved samples, {channels} channels")
    print(f"codec: 24-bit delta, {len(encoded) // 1024} blocks/layer, bit-exact round-trip")
    print(f"DRD bytes: {out_drd.stat().st_size}; checksums verified: {len(verification['ok'])}")
    return lib_dir


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_bin", type=Path)
    parser.add_argument("wav", type=Path)
    parser.add_argument("--output-root", type=Path, default=Path("build"))
    args = parser.parse_args()
    build(args.source_bin, args.wav, args.output_root)


if __name__ == "__main__":
    main()
