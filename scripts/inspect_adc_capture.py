#!/usr/bin/env python3
"""Summarize an OMADC002 diagnostic capture without interpreting ADC samples."""

import argparse
from pathlib import Path
import struct


MAGIC = b"OMADC002"
HEADER = struct.Struct("<8s7I")
RECORD_PREFIX = struct.Struct("<Q4I")


def inspect(path: Path) -> None:
    with path.open("rb") as capture:
        raw_header = capture.read(HEADER.size)
        if len(raw_header) != HEADER.size:
            raise ValueError("capture header is incomplete")
        magic, poll_us, map0, dma0, portion0, map1, dma1, portion1 = HEADER.unpack(raw_header)
        if magic != MAGIC:
            raise ValueError(f"unsupported capture magic: {magic!r}")
        if dma0 == 0 or dma1 == 0 or dma0 > 1024 * 1024 or dma1 > 1024 * 1024:
            raise ValueError("invalid DMA sizes")
        record_size = RECORD_PREFIX.size + dma0 + dma1
        payload_size = path.stat().st_size - HEADER.size
        count, remainder = divmod(payload_size, record_size)
        if remainder:
            raise ValueError(f"capture has {remainder} incomplete trailing bytes")

        moved = [0, 0]
        invalid = [0, 0]
        minima = [None, None]
        maxima = [None, None]
        last_ns = 0
        for _ in range(count):
            elapsed_ns, before0, after0, before1, after1 = RECORD_PREFIX.unpack(
                capture.read(RECORD_PREFIX.size)
            )
            last_ns = elapsed_ns
            for index, (before, after, dma) in enumerate(
                ((before0, after0, dma0), (before1, after1, dma1))
            ):
                moved[index] += before != after
                invalid[index] += not (0 <= before <= dma and 0 <= after <= dma)
                low, high = min(before, after), max(before, after)
                minima[index] = low if minima[index] is None else min(minima[index], low)
                maxima[index] = high if maxima[index] is None else max(maxima[index], high)
            capture.seek(dma0 + dma1, 1)

    print(f"Capture: {path}")
    print(f"Poll interval requested: {poll_us} us")
    print(f"Records: {count}; last timestamp: {last_ns / 1_000_000_000:.3f} s")
    for index, (mapped, dma, portion) in enumerate(
        ((map0, dma0, portion0), (map1, dma1, portion1))
    ):
        print(
            f"ADC{index}: map={mapped} DMA={dma} portion={portion}; "
            f"cursor range={minima[index]}..{maxima[index]}; "
            f"moved during copy={moved[index]}; out of range={invalid[index]}"
        )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    args = parser.parse_args()
    try:
        inspect(args.capture)
    except (OSError, ValueError) as error:
        parser.exit(1, f"error: {error}\n")


if __name__ == "__main__":
    main()
