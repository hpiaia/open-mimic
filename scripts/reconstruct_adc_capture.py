#!/usr/bin/env python3
"""Extract completed 32-byte ADC frames from consecutive OMADC002 snapshots."""

import argparse
import csv
from pathlib import Path
import struct
import sys


HEADER = struct.Struct("<8s7I")
PREFIX = struct.Struct("<Q4I")
FRAME = struct.Struct("<16H")


def read_record(source, dma_sizes):
    prefix = source.read(PREFIX.size)
    if not prefix:
        return None
    if len(prefix) != PREFIX.size:
        raise ValueError("incomplete record prefix")
    elapsed_ns, before0, after0, before1, after1 = PREFIX.unpack(prefix)
    rings = tuple(source.read(size) for size in dma_sizes)
    if any(len(ring) != size for ring, size in zip(rings, dma_sizes)):
        raise ValueError("incomplete DMA ring")
    return elapsed_ns, (before0, before1), (after0, after1), rings


def interval_offsets(previous_cursor, current_cursor, dma_size):
    if not (0 < previous_cursor <= dma_size and 0 < current_cursor <= dma_size):
        raise ValueError("ADC cursor outside DMA ring")
    if previous_cursor % FRAME.size or current_cursor % FRAME.size:
        raise ValueError("ADC cursor is not frame aligned")
    offset = previous_cursor % dma_size
    end = current_cursor % dma_size
    while offset != end:
        yield offset
        offset = (offset + FRAME.size) % dma_size


def reconstruct(capture_path, output_path, adc_index, channels):
    with capture_path.open("rb") as source:
        header = source.read(HEADER.size)
        if len(header) != HEADER.size:
            raise ValueError("incomplete capture header")
        magic, _, map0, dma0, portion0, map1, dma1, portion1 = HEADER.unpack(header)
        if magic != b"OMADC002":
            raise ValueError(f"unsupported capture magic: {magic!r}")
        if (portion0, portion1) != (FRAME.size, FRAME.size):
            raise ValueError("capture does not use 32-byte ADC frames")
        dma_sizes = (dma0, dma1)
        if any(size == 0 or size > 1024 * 1024 or size % FRAME.size for size in dma_sizes):
            raise ValueError("invalid DMA ring size")
        if map0 < dma0 + 4 or map1 < dma1 + 4:
            raise ValueError("mapping is too small for the cursor")

        previous = read_record(source, dma_sizes)
        current = read_record(source, dma_sizes)
        if previous is None or current is None:
            raise ValueError("capture needs at least two snapshots")
        following = read_record(source, dma_sizes)
        total = 0
        mismatched = 0
        with output_path.open("x", newline="") as output:
            writer = csv.writer(output)
            writer.writerow(["frame_index", "snapshot_elapsed_ns", *(f"adc{adc_index}_ch{ch}" for ch in channels)])
            while current is not None:
                ring = current[3][adc_index]
                next_ring = following[3][adc_index] if following is not None else None
                for offset in interval_offsets(
                    previous[1][adc_index], current[1][adc_index], dma_sizes[adc_index]
                ):
                    frame = ring[offset : offset + FRAME.size]
                    if next_ring is not None and frame != next_ring[offset : offset + FRAME.size]:
                        mismatched += 1
                    samples = FRAME.unpack(frame)
                    writer.writerow([total, current[0], *(samples[ch] & 0x0FFF for ch in channels)])
                    total += 1
                previous, current = current, following
                following = read_record(source, dma_sizes) if current is not None else None

    print(f"Extracted {total} ADC{adc_index} frames to {output_path}", file=sys.stderr)
    print(f"Frames differing from following snapshot: {mismatched}", file=sys.stderr)
    print("Snapshot timestamps are upper bounds, not individual sample times.", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--adc", type=int, choices=(0, 1), required=True)
    parser.add_argument("--channels", type=int, nargs="+", required=True)
    args = parser.parse_args()
    if not args.channels or any(not 0 <= channel < 16 for channel in args.channels):
        parser.error("channels must be between 0 and 15")
    try:
        reconstruct(args.capture, args.output, args.adc, args.channels)
    except (OSError, ValueError) as error:
        parser.exit(1, f"error: {error}\n")


if __name__ == "__main__":
    main()
