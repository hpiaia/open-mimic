#!/usr/bin/env python3
"""Pearl Mimic 1024-byte delta block codec."""

from __future__ import annotations

import struct

BLOCK_BYTES = 1024
PAYLOAD_BITS = (BLOCK_BYTES - 8) * 8
SCALE = 1 << 23


def sign_extend(value: int, bits: int) -> int:
    sign = 1 << (bits - 1)
    return (value ^ sign) - sign


def decode_block(block: bytes) -> list[int]:
    if len(block) != BLOCK_BYTES:
        raise ValueError("DRD blocks are exactly 1024 bytes")
    header, count = struct.unpack_from("<II", block)
    width = header & 0xff
    predictor = sign_extend(header >> 8, 24)
    if not 1 <= width <= 24 or count > PAYLOAD_BITS // width:
        raise ValueError(f"invalid block header width={width} count={count}")
    words = struct.unpack_from("<254I", block, 8)
    bitpos = 0
    output: list[int] = []
    for _ in range(count):
        word_index, offset = divmod(bitpos, 32)
        remaining = 32 - offset
        if width <= remaining:
            value = (words[word_index] >> (remaining - width)) & ((1 << width) - 1)
        else:
            high = words[word_index] & ((1 << remaining) - 1)
            low_bits = width - remaining
            low = words[word_index + 1] >> (32 - low_bits)
            value = (high << low_bits) | low
        predictor += sign_extend(value, width)
        output.append(predictor)
        bitpos += width
    return output


def required_signed_bits(value: int) -> int:
    for bits in range(1, 25):
        if -(1 << (bits - 1)) <= value < (1 << (bits - 1)):
            return bits
    raise ValueError(f"24-bit delta overflow: {value}")


def encode_block(samples: list[int], width: int | None = None, predictor: int = 0) -> bytes:
    deltas: list[int] = []
    previous = predictor
    for sample in samples:
        deltas.append(sample - previous)
        previous = sample
    needed = max((required_signed_bits(value) for value in deltas), default=1)
    width = needed if width is None else width
    if width < needed or not 1 <= width <= 24:
        raise ValueError(f"width {width} cannot hold {needed}-bit deltas")
    capacity = PAYLOAD_BITS // width
    if len(samples) > capacity:
        raise ValueError(f"block holds {capacity} samples at {width} bits")
    words = [0] * 254
    bitpos = 0
    mask = (1 << width) - 1
    for delta in deltas:
        value = delta & mask
        word_index, offset = divmod(bitpos, 32)
        remaining = 32 - offset
        if width <= remaining:
            words[word_index] |= value << (remaining - width)
        else:
            high_bits = remaining
            low_bits = width - high_bits
            words[word_index] |= value >> low_bits
            words[word_index + 1] |= (value & ((1 << low_bits) - 1)) << (32 - low_bits)
        bitpos += width
    header = ((predictor & 0xffffff) << 8) | width
    return struct.pack("<II254I", header, len(samples), *words)


def encode(samples: list[int], width: int = 24) -> bytes:
    capacity = PAYLOAD_BITS // width
    return b"".join(encode_block(samples[i:i + capacity], width) for i in range(0, len(samples), capacity))


def decode(data: bytes) -> list[int]:
    if len(data) % BLOCK_BYTES:
        raise ValueError("encoded DRD length is not block aligned")
    output: list[int] = []
    for offset in range(0, len(data), BLOCK_BYTES):
        output.extend(decode_block(data[offset:offset + BLOCK_BYTES]))
    return output
