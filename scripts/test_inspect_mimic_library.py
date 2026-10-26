#!/usr/bin/env python3
"""Synthetic tests for the library metadata reader; no library assets required."""
from __future__ import annotations

import struct
import tempfile
import unittest
import zlib
from pathlib import Path
from unittest.mock import patch

from inspect_mimic_library import MAGIC, READ_CHUNK, parse


def text(value: str) -> bytes:
    data = value.encode() + b"\0"
    return struct.pack("<I", len(data)) + data


def payload(image: bytes = b"\x89PNG\r\n\x1a\nsynthetic") -> bytes:
    return b"".join((
        struct.pack("<I", 1), text("INST0numart"), struct.pack("<i", 2),
        struct.pack("<I", 1), text("INST0pool0nchn"), struct.pack("<I2i", 2, 1, 2),
        struct.pack("<I", 0),
        struct.pack("<I", 0),
        struct.pack("<I", 1), text("INST0micinf0isst"), b"\0",
        struct.pack("<I", 0),
        struct.pack("<I", 1), text("INST0insnam"), text("Synthetic instrument"),
        struct.pack("<I", len(image)), image,
    ))


def container(data: bytes) -> bytes:
    return MAGIC + struct.pack("<II", 3, len(data)) + zlib.compress(data)


class LibraryMetadataTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / "synthetic.mci"

    def test_image_blob_and_integrated_payload_boundary(self) -> None:
        encoded = container(payload())
        self.path.write_bytes(encoded + b"audio samples are deliberately opaque")
        document = parse(self.path)
        self.assertEqual(document["metadata_bytes"], len(encoded))
        self.assertEqual(document["trailing_bytes"], 37)
        self.assertEqual(document["groups"]["int"]["INST0numart"], 2)
        self.assertEqual(document["groups"]["int_array"]["INST0pool0nchn"], [1, 2])
        self.assertEqual(document["image"]["format"], "png")

    def test_audio_payload_is_not_read_in_full(self) -> None:
        encoded = container(payload(b""))
        self.path.write_bytes(encoded + b"\0" * (READ_CHUNK * 10))
        opened = self.path.open("rb")
        self.addCleanup(opened.close)
        sizes = []

        class CountingStream:
            def __enter__(self):
                return self

            def __exit__(self, *_):
                opened.close()

            def read(self, count):
                value = opened.read(count)
                sizes.append(len(value))
                return value

        with patch.object(Path, "open", return_value=CountingStream()):
            document = parse(self.path)
        self.assertEqual(document["metadata_bytes"], len(encoded))
        self.assertLessEqual(sum(sizes), len(MAGIC) + 8 + READ_CHUNK)
        self.assertEqual(document["image"]["format"], "none")

    def test_rejects_truncated_compressed_metadata(self) -> None:
        self.path.write_bytes(container(payload())[:-2])
        with self.assertRaisesRegex(ValueError, "incomplete compressed"):
            parse(self.path)

    def test_rejects_inconsistent_declaration(self) -> None:
        encoded = bytearray(container(payload()))
        struct.pack_into("<I", encoded, len(MAGIC) + 4, 1)
        self.path.write_bytes(encoded)
        with self.assertRaisesRegex(ValueError, "exceeds declared"):
            parse(self.path)

    def test_rejects_image_length_larger_than_metadata(self) -> None:
        data = bytearray(payload(b""))
        struct.pack_into("<I", data, len(data) - 4, 1000)
        self.path.write_bytes(container(data))
        with self.assertRaisesRegex(ValueError, "truncated payload"):
            parse(self.path)


if __name__ == "__main__":
    unittest.main()
