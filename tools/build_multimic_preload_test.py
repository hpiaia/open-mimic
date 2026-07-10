#!/usr/bin/env python3
"""Build a six-channel test instrument whose audible sample is fully preloaded.

Installed Mimic instruments contain raw float32 preloads followed by compressed
streaming chunks. This builder puts the complete short diagnostic sound in every
raw preload and retains valid SCM compressed chunks as an unused safety tail.
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import struct
import wave
import zlib
from io import BytesIO
from pathlib import Path

from PIL import Image, ImageDraw

from build_multimic_proof import checksum_header, encode_checksum, encode_storage
from mimic_kv import instrument_summary, load_storage, parse_checksum, verify_checksum


def wav_float32(path: Path, max_frames: int) -> tuple[bytes, int, int]:
    with wave.open(str(path), "rb") as source:
        channels = source.getnchannels()
        frames = min(source.getnframes(), max_frames)
        if source.getframerate() != 48_000 or source.getsampwidth() != 2:
            raise SystemExit("test WAV must be 48 kHz, 16-bit PCM")
        pcm = source.readframes(frames)
    values = struct.unpack(f"<{frames * channels}h", pcm)
    return b"".join(struct.pack("<f", value / 32768.0) for value in values), frames, channels


def png_chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def test_image(source_png: bytes) -> bytes:
    """Modify a known-working stock thumbnail while retaining its RGBA profile."""
    image = Image.open(BytesIO(source_png)).convert("RGBA")
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 0, image.width - 1, image.height - 1), outline=(255, 0, 40, 255), width=4)
    draw.line((4, 4, image.width - 5, image.height - 5), fill=(0, 255, 255, 255), width=3)
    draw.line((image.width - 5, 4, 4, image.height - 5), fill=(0, 255, 255, 255), width=3)
    output = BytesIO()
    image.save(output, format="PNG", optimize=False, compress_level=6)
    return output.getvalue()


def build(source_bin: Path, wav_path: Path, output_root: Path, frames_limit: int) -> Path:
    source_drd = source_bin.with_suffix(".drd")
    source_checksum = source_bin.parent.parent / "checksum.dat"
    storage = load_storage(source_bin)
    before = instrument_summary(storage)
    audio, frames, channels = wav_float32(wav_path, frames_limit)
    expected_channels = sum(2 if mic["stereo"] else 1 for mic in before["mics"])
    if channels != expected_channels:
        raise SystemExit(f"WAV has {channels} channels; descriptor requires {expected_channels}")

    prefix = "INST0pool0"
    count = storage.u32[prefix + "psz"]
    old_offsets = storage.u32_arrays[prefix + "dofs"]
    old_lengths = storage.u32_arrays[prefix + "dlen"]
    source_audio = source_drd.read_bytes()
    compressed_chunks = [source_audio[o:o + n] for o, n in zip(old_offsets, old_lengths)]

    # The playback cursor starts at float 64. fcnt is the total number of
    # interleaved samples (frames * channels), not the number of WAV frames.
    # Keep 2048 valid floats after the audible data so the prefetch crossover
    # threshold cannot move playback into the SCM safety tail before fcnt ends.
    guard = b"\0" * (64 * 4)
    end_padding = b"\0" * (2048 * 4)
    preload = guard + audio + end_padding
    desc = len(preload) // 4
    fcnt = frames * channels
    preloads = [preload] * count
    dedo: list[int] = []
    cursor = 0
    for chunk in preloads:
        dedo.append(cursor)
        cursor += len(chunk)
    dofs: list[int] = []
    for chunk in compressed_chunks:
        dofs.append(cursor)
        cursor += len(chunk)

    storage.u32_arrays[prefix + "dedo"] = dedo
    storage.u32_arrays[prefix + "desc"] = [desc] * count
    storage.u32_arrays[prefix + "dofs"] = dofs
    storage.u32_arrays[prefix + "dlen"] = old_lengths
    storage.u32_arrays[prefix + "fcnt"] = [fcnt] * count
    storage.u32_arrays[prefix + "nchn"] = [channels] * count

    name = "Open Mimic Full Preload Six Channel Test"
    library_name = "Open Mimic Full Preload MultiMic Test"
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
    storage.blob = test_image(storage.blob)

    lib_dir = output_root / f"{library_name}.lib"
    if lib_dir.exists():
        shutil.rmtree(lib_dir)
    instruments = lib_dir / "instruments"
    instruments.mkdir(parents=True)
    out_bin = instruments / f"{name}.bin"
    out_drd = instruments / f"{name}.drd"
    out_bin.write_bytes(encode_storage(storage))
    with out_drd.open("wb") as output:
        for chunk in preloads:
            output.write(chunk)
        for chunk in compressed_chunks:
            output.write(chunk)
    (lib_dir / "libver.mimicinfo").write_text("10 July 2026", encoding="ascii")

    entries = [(out_bin.name, hashlib.md5(out_bin.read_bytes()).digest()),
               (out_drd.name, hashlib.md5(out_drd.read_bytes()).digest())]
    (lib_dir / "checksum.dat").write_bytes(encode_checksum(checksum_header(source_checksum), entries))

    rebuilt = load_storage(out_bin)
    verification = verify_checksum(lib_dir, parse_checksum(lib_dir / "checksum.dat"))
    assert rebuilt.u32_arrays[prefix + "fcnt"] == [frames * channels] * count
    assert rebuilt.u32_arrays[prefix + "desc"] == [64 + frames * channels + 2048] * count
    assert rebuilt.blob.startswith(b"\x89PNG\r\n\x1a\n")
    assert not verification["missing"] and not verification["mismatched"]
    assert max(o + n for o, n in zip(dofs, old_lengths)) == out_drd.stat().st_size
    print(f"built: {lib_dir}")
    print(f"audible preload: {frames} frames, {frames * channels} interleaved samples, {channels} channels")
    print(f"raw preload per layer: {len(preload)} bytes; valid compressed safety tail retained")
    print(f"checksums verified: {len(verification['ok'])}")
    return lib_dir


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_bin", type=Path)
    parser.add_argument("wav", type=Path)
    parser.add_argument("--frames", type=int, default=72_000, help="audible WAV frames per layer")
    parser.add_argument("--output-root", type=Path, default=Path("build"))
    args = parser.parse_args()
    build(args.source_bin, args.wav, args.output_root, args.frames)


if __name__ == "__main__":
    main()
