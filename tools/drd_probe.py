#!/usr/bin/env python3
"""
drd_probe.py — inspect / diff Pearl Mimic Pro `.drd` audio payloads.

For each `<Instrument>.bin` in a `*.lib` folder it reads the pool metadata
(`dofs`/`dlen`/`fcnt`/`nchn`/`psz`) and reports each `.drd` chunk's header and
effective byte-rate. In `--diff` mode it compares two libraries chunk-by-chunk,
which is how we reverse the block codec from controlled Editor imports:
import a known WAV -> export a `.lib` -> diff against a reference.

Codec context: the firmware decoder `DecopressRIFFDataToFloat` treats chunks as
bit-depth-reduced blocks; bit-depth 32 is a straight float32 copy. See
docs/research/drd-codec-notes.md.

Usage:
    drd_probe.py <lib_dir>                 # summarize every instrument's chunks
    drd_probe.py <lib_dir> <Instrument>    # one instrument, all chunks
    drd_probe.py --diff <libA> <libB>      # per-chunk diff of two libraries
"""
import sys, os, zlib, struct, glob

def _payload(path):
    d = open(path, "rb").read()
    if d[:14] != b"mimic_storage\x00":
        raise ValueError(f"{path}: not mimic_storage")
    return zlib.decompressobj().decompress(d[22:])

def _u32_array(pl, key):
    i = pl.find(key + b"\x00")
    if i < 0:
        return None
    p = i + len(key) + 1
    n = struct.unpack("<I", pl[p:p+4])[0]; p += 4
    return [struct.unpack("<I", pl[p+4*j:p+4*j+4])[0] for j in range(n)]

def pool(bin_path):
    pl = _payload(bin_path)
    g = lambda k: _u32_array(pl, b"INST0pool0" + k)
    return dict(dofs=g(b"dofs"), dlen=g(b"dlen"), fcnt=g(b"fcnt"),
                nchn=g(b"nchn"), psz=(_u32_array(pl, b"INST0pool0psz") or [None])[0])

def chunks(lib_dir):
    for bin_path in sorted(glob.glob(os.path.join(lib_dir, "instruments", "*.bin"))):
        drd = bin_path[:-4] + ".drd"
        if os.path.exists(drd):
            yield bin_path, drd, pool(bin_path)

def summarize(lib_dir, only=None):
    for bin_path, drd, p in chunks(lib_dir):
        name = os.path.basename(bin_path)[:-4]
        if only and only.lower() not in name.lower():
            continue
        dofs, dlen, fcnt, nchn = p["dofs"], p["dlen"], p["fcnt"], p["nchn"]
        nc = nchn[0] if nchn else 0
        drd_sz = os.path.getsize(drd)
        span = max(o+l for o, l in zip(dofs, dlen)) if dofs else 0
        print(f"\n{name}: chunks={len(dofs)} nchn={nc} .drd={drd_sz}B "
              f"span_ok={span==drd_sz}")
        with open(drd, "rb") as f:
            order = sorted(range(len(dofs)), key=lambda i: dofs[i])
            for rank, k in enumerate(order[:6]):
                f.seek(dofs[k]); hdr = f.read(24)
                ints = struct.unpack("<6I", hdr)
                bps = dlen[k] / fcnt[k] if fcnt[k] else 0
                print(f"  chunk[{k:3d}] dofs={dofs[k]:>10} dlen={dlen[k]:>8} "
                      f"fcnt={fcnt[k]:>8} bytes/samp={bps:4.2f}  hdr={ints}")
            if len(order) > 6:
                print(f"  ... {len(order)-6} more chunks")

def diff(libA, libB):
    a = {os.path.basename(b)[:-4]: (b, d, p) for b, d, p in chunks(libA)}
    bb = {os.path.basename(b)[:-4]: (b, d, p) for b, d, p in chunks(libB)}
    for name in sorted(set(a) & set(bb)):
        (_, drdA, pA), (_, drdB, pB) = a[name], bb[name]
        print(f"\n### {name}")
        for fld in ("psz", "nchn", "fcnt", "dofs", "dlen"):
            va, vb = pA[fld], pB[fld]
            if va != vb:
                print(f"  {fld}: A={va if fld in ('psz','nchn') else va[:6]} != "
                      f"B={vb if fld in ('psz','nchn') else vb[:6]}")
        fa, fb = open(drdA, "rb").read(), open(drdB, "rb").read()
        if fa == fb:
            print("  .drd IDENTICAL"); continue
        first = next((i for i in range(min(len(fa), len(fb))) if fa[i] != fb[i]), None)
        print(f"  .drd differ: sizes {len(fa)} vs {len(fb)}, first diff @0x{first:x}"
              if first is not None else "  .drd differ (length only)")

def main():
    args = sys.argv[1:]
    if args and args[0] == "--diff":
        diff(args[1], args[2])
    elif len(args) >= 2:
        summarize(args[0], args[1])
    elif args:
        summarize(args[0])
    else:
        print(__doc__)

if __name__ == "__main__":
    main()
