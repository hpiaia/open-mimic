#!/usr/bin/env python3
"""
mimic_lib.py — reader / round-trip for Pearl Mimic Pro `mimic_storage` containers
(`.bin` instrument descriptors and `.kit` presets inside a `*.lib` library).

STATUS (2026-07-08): byte-exact round-trip PROVEN on every SCM instrument + a kit.
The payload is a sequence of SECTIONS, each `[u32 count][count records]`. Records
within a section share one value style:

    i32   : [u32 keylen][key\\0][4 bytes]              int32 or float32
    byte  : [u32 keylen][key\\0][1 byte]               bool / small flag
    arr4  : [u32 keylen][key\\0][u32 nslots][nslots*4] counted u32/f32 array;
                                                       unused slots = 0xFFFFFFFF
    blob  : [u32 keylen][key\\0][u32 len][len bytes]   string / PNG / raw blob

Confirmed section order for an instrument `.bin` (SCM Kick):
    0 i32  (scalars: artid, noteon, numlay, micnt, zonecnt, pool sizes, ...)
    1 arr4 (veloLay*smpidx round-robin tables, pool dofs/dlen/fcnt/nchn, rmsenv)
    2 i32  (per-layer minvol/maxvol, rmsenv magnitudes, ...)
    3 arr4 (artic timing arrays: maxrtb/minrtb/..., pool svol)
    4 byte (flags: artic chk/fak/mut, micinf*{enven,isst,micen,micm}, ...)
    5 blob (names: artic artn, INST insdat=display name, insimj=PNG path+data)
    ...   (further sections; classifier falls back to verbatim if unsure)

The reader classifies each section by trying the styles and keeping the one that
lands on a valid next-section boundary. Anything it can't classify is preserved
as an opaque tail so re-encoding is always byte-identical.

NOTE: this tool covers the `mimic_storage` CONTAINER only. The `.drd` audio it
references is NOT raw float32 PCM — it is a custom-COMPRESSED codec (~15-17x:
sum(dlen)=19.4MB vs raw float32=289MB for the SCM Kick; max(dofs+dlen)==.drd size
exactly). Decoding that codec (firmware syms `CCompressedDatasource`,
`CDiskStreamer::doDecomp_wave`) is the real blocker for an instrument compiler.
For the fuller section/`checksum.dat`/MD5 analysis see `tools/mimic_kv.py` and
`docs/research/instrument-format.md`.

Usage:
    mimic_lib.py <file>              # summary + section map + round-trip check
    mimic_lib.py <file> KEY [KEY..]  # dump records whose key contains a substring
"""
import sys, zlib, struct

MAGIC = b"mimic_storage\x00"
FF = b"\xff\xff\xff\xff"

def load(path):
    d = open(path, "rb").read()
    if d[:14] != MAGIC:
        raise ValueError(f"not mimic_storage: {d[:14]!r}")
    ver, usize = struct.unpack("<II", d[14:22])
    pl = zlib.decompressobj().decompress(d[22:])
    if len(pl) != usize:
        print(f"! decompressed {len(pl)} != declared {usize}", file=sys.stderr)
    return ver, pl

def _key_at(pl, p):
    if p + 4 > len(pl):
        return None
    klen = struct.unpack("<I", pl[p:p+4])[0]
    if not (1 <= klen <= 128) or p + 4 + klen > len(pl):
        return None
    k = pl[p+4:p+4+klen]
    if k[-1:] != b"\x00" or not all(32 <= c < 127 for c in k[:-1]):
        return None
    return klen, k[:-1].decode("latin1")

def _read_val(pl, p, style):
    if style == "byte":
        return (pl[p:p+1], p+1) if p+1 <= len(pl) else None
    if style == "i32":
        return (pl[p:p+4], p+4) if p+4 <= len(pl) else None
    if p + 4 > len(pl):
        return None
    n = struct.unpack("<I", pl[p:p+4])[0]; p += 4
    size = n*4 if style == "arr4" else n
    if n > 10_000_000 or p + size > len(pl):
        return None
    return ((n, pl[p:p+size]), p+size)

def _parse_section(pl, off, style):
    count = struct.unpack("<I", pl[off:off+4])[0]
    if count == 0 or count > 500_000:
        return None
    p = off + 4; recs = []
    for _ in range(count):
        ka = _key_at(pl, p)
        if not ka:
            return None
        klen, key = ka; p += 4 + klen
        rv = _read_val(pl, p, style)
        if not rv:
            return None
        val, p = rv; recs.append((key, val))
    return recs, p, count

STYLES = ("i32", "byte", "arr4", "blob")

def parse(pl):
    """Return (sections, tail_offset). sections=[(off,style,count,recs)]."""
    off = 0; sections = []
    while off < len(pl):
        chosen = None
        for style in STYLES:
            r = _parse_section(pl, off, style)
            if not r:
                continue
            recs, noff, count = r
            if noff == len(pl) or _key_at(pl, noff + 4):   # lands on EOF or next section
                chosen = (style, recs, noff, count); break
        if not chosen:
            break
        style, recs, noff, count = chosen
        sections.append((off, style, count, recs))
        off = noff
    return sections, off

def rebuild(pl, sections, tail_off):
    out = bytearray()
    for off, style, count, recs in sections:
        out += struct.pack("<I", count)
        for key, val in recs:
            kb = key.encode() + b"\x00"
            out += struct.pack("<I", len(kb)) + kb
            if style in ("i32", "byte"):
                out += val
            else:
                n, blob = val
                out += struct.pack("<I", n) + blob
    out += pl[tail_off:]                                    # verbatim remainder
    return bytes(out)

def _fmt(style, val):
    if style == "byte":
        return f"u8={val[0]}"
    if style == "i32":
        i = struct.unpack("<i", val)[0]; f = struct.unpack("<f", val)[0]
        return f"i32={i} f32={f:.4g}" if 1e-4 < abs(f) < 1e7 else f"i32={i}"
    n, blob = val
    if style == "arr4":
        u = [struct.unpack("<I", blob[j:j+4])[0] for j in range(0, len(blob), 4)]
        used = [x for x in u if x != 0xFFFFFFFF]
        return f"arr[{n}] used={len(used)} {used[:8]}"
    txt = blob.split(b"\x00")[0]
    printable = all(32 <= c < 127 for c in txt) and len(txt) > 0
    return f'str="{txt.decode("latin1")}"' if printable else f"blob[{n}]"

def main():
    path = sys.argv[1]; filt = sys.argv[2:]
    ver, pl = load(path)
    sections, tail = parse(pl)
    rt = rebuild(pl, sections, tail) == pl
    total = sum(c for _, _, c, _ in sections)
    print(f"# {path}")
    print(f"# ver={ver} payload={len(pl)}B sections={len(sections)} records={total} "
          f"classified_to=0x{tail:x}/0x{len(pl):x} tail={len(pl)-tail}B")
    print(f"# ROUND-TRIP byte-exact: {rt}")
    if not filt:
        for off, style, count, recs in sections:
            print(f"  sec @0x{off:<6x} {style:4s} n={count:<5d} "
                  f"{recs[0][0]!r} .. {recs[-1][0]!r}")
        if tail < len(pl):
            print(f"  [unclassified tail 0x{tail:x}..0x{len(pl):x}] "
                  f"head: {pl[tail:tail+16].hex(' ')}")
    else:
        for off, style, count, recs in sections:
            for key, val in recs:
                if any(t in key for t in filt):
                    print(f"  {key:44s} {_fmt(style, val)}")

if __name__ == "__main__":
    main()
