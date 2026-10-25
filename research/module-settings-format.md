# Mimic Pro module settings format

This analysis uses read-only copies of nine files from the original SSD's
`/mnt/pearl/settings/module_settings/` directory. The copies and decoded
device-specific values are kept in ignored `build/research/` paths. The source
SSD was mounted `ro,norecovery` throughout extraction.

## Container

All nine files share the following layout:

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | 14 bytes | `mimic_storage\0` |
| 14 | `uint32_le` | Format version, `3` in every inspected file |
| 18 | `uint32_le` | Uncompressed payload length |
| 22 | zlib stream | Payload bytes |

The decompressed payload consists of eight groups in this order: scalar
integers, integer arrays, scalar floats, float arrays, scalar booleans, boolean
arrays, strings, and images. Each group begins with a `uint32_le` record count.
Each record starts with a length-prefixed, NUL-terminated UTF-8 key. Scalar
integers are signed 32-bit little-endian values; floats are IEEE-754 binary32
little-endian; booleans are one byte. Arrays have a `uint32_le` element count
followed by elements. Strings have another length-prefixed, NUL-terminated
value. The sample files contain no boolean arrays or images, so those two
value encodings remain unconfirmed by specimen data. All nine payloads parse
without leftover bytes using this layout.

REA decompilation of `CDataStorage::SaveToFile` in the original ARM executable
shows calls to eight save methods in the same order (Evidence
`ev_506d4f00a79f046b6b11ecfe985aeedb2852a0c35ce5b01c20e56a8b89b8e024`).
The executable SHA-256 is
`b12895171c9cb8fd7db067c816ba28b4ae4817869a34654175f3e6dfb683fa8c`.

## `xtalk_settings.dat`

This specimen is 4,053 compressed bytes and 17,835 uncompressed bytes
(SHA-256 `695776b7407ac3112216d3f644689d8a930052e6347bb4007c7cc42f8bcd17d8`).
It contains exactly 1,024 float keys `XTCa{a}b{b}`, covering every pair of
indices from 0 to 31, and one boolean key `XTCen`, which is true in this
specimen. REA decompilation of `CXtalkManager::LoadCurrState_XTM` reads a
32-by-32 grid of floats and then a boolean (Evidence
`ev_b86a923fdc67302c21473468e663b6e2b9c4be5bf7f448d5a6d3d6c00c6f3cd4`).
`CXtalkManager::SaveCurrState_XTM` writes the same grid and boolean (Evidence
`ev_1eb9d3e546d1a765f34a9a63b046630ad5e3c034a461dafcf15a7460420d285b`).

The observed matrix has 596 zero cells and 428 nonzero cells. All 32 diagonal
cells are zero. It is not symmetric: only 175 of 496 off-diagonal pairs have
identical values. Positive values range from approximately `0.0002442` to
`0.966789`. These are stored values, not necessarily a direct sensitivity
percentage. The `a` and `b` indices correspond to the program's two matrix
dimensions, but the exact source-versus-victim direction and suppression
equation need a controlled calibration change or a focused data-flow proof.
Do not edit the original file based on this analysis alone.

## Reproduce

```sh
python3 scripts/decode_mimic_storage.py build/research/module_settings \
  --output-dir build/research/decoded-settings \
  --xtalk-csv build/research/decoded-settings/xtalk-matrix.csv
```

The decoder verifies the magic, version, declared length, zlib stream, group
counts, key uniqueness, and complete payload consumption. The JSON and CSV
outputs are ignored by Git because they contain the owner's current settings.
