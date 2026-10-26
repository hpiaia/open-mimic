#!/usr/bin/env python3
"""Export one library pool sample to aligned, unprocessed per-microphone WAVs."""
from __future__ import annotations

import argparse
from array import array
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import wave

from inspect_mimic_library import parse


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("metadata", type=Path)
    parser.add_argument("--data", type=Path)
    parser.add_argument("--sample", type=int, default=0)
    parser.add_argument("--decoder", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--sample-rate", type=int, default=44100,
                        help="original Mimic engine rate is 44100 Hz")
    args = parser.parse_args()
    metadata = parse(args.metadata)
    groups = metadata["groups"]
    arrays = groups["int_array"]
    count = groups["int"]["INST0pool0psz"]
    if not 0 <= args.sample < count or not 8000 <= args.sample_rate <= 384000:
        parser.error("invalid sample index or output sample rate")
    data = args.data or (args.metadata if groups["bool"].get("INSTmnlth", False)
                         else args.metadata.with_suffix(".drd"))
    base = metadata["metadata_bytes"] if groups["bool"].get("INSTmnlth", False) else 0
    index = args.sample
    channels = arrays["INST0pool0nchn"][index]
    scalars = arrays["INST0pool0fcnt"][index]
    if channels < 1 or scalars % channels:
        raise ValueError("sample is not a whole number of multi-mic frames")
    frames = scalars // channels
    microphones = []
    channel = 0
    for mic in range(groups["int"]["INST0micnt"]):
        prefix = f"INST0micinf{mic}"
        name = groups["string"][prefix + "micn"]
        width = 2 if groups["bool"][prefix + "isst"] else 1
        safe_name = re.sub(r"[^\w.-]+", "_", name).strip("._") or "mic"
        output = args.output_dir / f"{mic:02d}-{safe_name}.wav"
        if output.exists():
            raise FileExistsError(output)
        microphones.append({"name": name, "channels": width, "first_channel": channel,
                            "path": str(output)})
        channel += width
    if channel != channels:
        raise ValueError("mic definitions do not cover the sample channels")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mimic-export-") as folder:
        decoded = Path(folder) / "sample.f32"
        subprocess.run([str(args.decoder.resolve()), str(data),
                        str(base + arrays["INST0pool0dofs"][index]),
                        str(arrays["INST0pool0dlen"][index]), str(decoded)], check=True,
                       capture_output=True)
        values = array("f")
        values.frombytes(decoded.read_bytes())
        if sys.byteorder != "little":
            values.byteswap()
        if len(values) != scalars:
            raise ValueError("decoded sample count differs from metadata")
        for mic in microphones:
            pcm = bytearray()
            for frame in range(frames):
                start = frame * channels + mic["first_channel"]
                for offset in range(mic["channels"]):
                    value = int(values[start + offset] * 8388608)
                    if not -8388608 <= value <= 8388607:
                        raise ValueError("decoded scalar is outside signed 24-bit PCM")
                    pcm.extend((value & 0xffffff).to_bytes(3, "little"))
            with wave.open(mic["path"], "wb") as output:
                output.setnchannels(mic["channels"])
                output.setsampwidth(3)
                output.setframerate(args.sample_rate)
                output.writeframes(pcm)
    report = {"source": str(args.metadata), "sample_index": index,
              "frames": frames, "sample_rate": args.sample_rate,
              "processing": "raw microphone channels; mixer and envelopes are separate",
              "microphones": microphones}
    (args.output_dir / "sample.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Exported {len(microphones)} aligned microphone files, {frames} frames each")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
