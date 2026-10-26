#!/usr/bin/env python3
"""Validate and summarize a compact OMSEQ001 continuous ADC recording."""
import argparse
import json
from pathlib import Path
import struct

HEADER = struct.Struct("<8sII")
RECORD = struct.Struct("<QHHI")
FRAME = struct.Struct("<16H")


def read_records(path: Path):
    """Yield validated timestamp bounds, tagged ADC batches, gap flags and poll interval."""
    previous = -1
    with path.open("rb") as stream:
        header = stream.read(HEADER.size)
        if len(header) != HEADER.size:
            raise ValueError("truncated sequence header")
        magic, version, poll = HEADER.unpack(header)
        if magic != b"OMSEQ001" or version != 1 or not 100 <= poll <= 100000:
            raise ValueError("unsupported sequence header")
        while prefix := stream.read(RECORD.size):
            if len(prefix) != RECORD.size:
                raise ValueError("truncated sequence record")
            timestamp, left, right, flags = RECORD.unpack(prefix)
            if timestamp <= previous or max(left, right) > 127 or flags & ~1 or (flags and (left or right)):
                raise ValueError("invalid sequence timestamp, count or flags")
            previous = timestamp
            batches = [[], []]
            for adc, size in enumerate((left, right)):
                for _ in range(size):
                    raw = stream.read(FRAME.size)
                    if len(raw) != FRAME.size:
                        raise ValueError("truncated sequence frame")
                    words = FRAME.unpack(raw)
                    if sorted(word >> 12 for word in words) != list(range(16)):
                        raise ValueError("duplicate or missing physical ADC channel")
                    batches[adc].append(words)
            yield timestamp, batches, flags, poll


def inspect(path: Path):
    frames = [0, 0]
    count = gaps = clipped = 0
    timestamp = poll = 0
    for timestamp, batches, flags, poll in read_records(path):
        gaps += bool(flags)
        for adc, batch in enumerate(batches):
            frames[adc] += len(batch)
            clipped += sum((word & 4095) >= 4090 for frame in batch for word in frame)
        count += 1
    if not count or not all(frames):
        raise ValueError("sequence has no completed ADC frames")
    return {"format": "OMSEQ001", "records": count, "adc_frames": frames,
            "duration_seconds": timestamp / 1e9, "gap_records": gaps,
            "clipped_samples": clipped, "poll_microseconds": poll}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    args = parser.parse_args()
    print(json.dumps(inspect(args.capture), indent=2))
