#!/usr/bin/env python3
"""Generate deterministic 48 kHz WAVs for Mimic multi-mic routing tests."""

from __future__ import annotations

import argparse
import math
import random
import struct
import wave
from pathlib import Path

RATE = 48_000
DURATION = 1.5
AMP = 0.32


def tone(frame: int, frequency: float) -> float:
    # Short fade avoids clicks while retaining an obvious sustained identity.
    t = frame / RATE
    fade = min(1.0, t / 0.02, (DURATION - t) / 0.05)
    return AMP * max(0.0, fade) * math.sin(2 * math.pi * frequency * t)


def noise(frames: int, seed: int) -> list[float]:
    rng = random.Random(seed)
    values = []
    state = 0.0
    for frame in range(frames):
        # Filtered deterministic noise is easier to distinguish from a tone.
        state = 0.82 * state + 0.18 * rng.uniform(-1.0, 1.0)
        t = frame / RATE
        fade = min(1.0, t / 0.02, (DURATION - t) / 0.05)
        values.append(0.55 * max(0.0, fade) * state)
    return values


def pcm16(value: float) -> bytes:
    value = max(-1.0, min(1.0, value))
    return struct.pack("<h", round(value * 32767))


def write_wav(path: Path, channels: list[list[float]]) -> None:
    with wave.open(str(path), "wb") as output:
        output.setnchannels(len(channels))
        output.setsampwidth(2)
        output.setframerate(RATE)
        frames = bytearray()
        for values in zip(*channels):
            for value in values:
                frames += pcm16(value)
        output.writeframes(frames)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("build/multimic-test-wavs"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    count = round(RATE * DURATION)
    kick_in = [tone(i, 110.0) for i in range(count)]
    kick_out = [tone(i, 220.0) for i in range(count)]
    overhead_l = noise(count, 0x0A11)
    overhead_r = noise(count, 0x0A12)
    room_l = [tone(i, 440.0) for i in range(count)]
    room_r = [tone(i, 880.0) for i in range(count)]

    files = {
        "01-kick-in-110Hz-mono.wav": [kick_in],
        "02-kick-out-220Hz-mono.wav": [kick_out],
        "03-overhead-noise-stereo.wav": [overhead_l, overhead_r],
        "04-room-440-880Hz-stereo.wav": [room_l, room_r],
        "05-all-mics-6ch.wav": [
            kick_in, kick_out, overhead_l, overhead_r, room_l, room_r
        ],
    }
    for name, channels in files.items():
        path = args.output / name
        write_wav(path, channels)
        print(f"{path}: {len(channels)}ch {RATE}Hz {DURATION:.1f}s")


if __name__ == "__main__":
    main()
