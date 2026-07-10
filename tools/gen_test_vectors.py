#!/usr/bin/env python3
"""
gen_test_vectors.py — write known WAV inputs for black-box RE of the `.drd` codec.

Import each WAV into the Mimic Instrument Editor, export a `.lib`, then compare
the resulting `.drd` against these known inputs with `tools/drd_probe.py`.
Controlled inputs reveal block size, header meaning, bit-depth handling, and
whether the encoding is lossy (quantized) or near-lossless.

Writes 48 kHz mono 16-bit PCM WAVs (and a few float32) into ./drd_test_vectors/.

Design rationale:
  silence      -> baseline / how empty blocks encode
  impulse      -> where sample 0 lands; block alignment
  dc_full      -> constant value; reveals per-block scaling
  ramp         -> monotonic; reveals predictor/delta coding
  step         -> single discontinuity; block boundary behavior
  sine_1k      -> smooth tone; lossy-vs-lossless tell (compare decoded spectrum)
  altmax       -> +full/-full alternation; worst case for delta coders
  len_N        -> lengths 1,2,3,4,5,8,760,768,776 to find the block size (~768)
"""
import os, math, struct, wave

SR = 48000
OUT = os.path.join(os.getcwd(), "drd_test_vectors")

def write_wav_i16(name, samples):
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name + ".wav")
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        clip = lambda v: max(-32768, min(32767, int(round(v))))
        w.writeframes(b"".join(struct.pack("<h", clip(s)) for s in samples))
    return path, len(samples)

def write_wav_f32(name, samples):
    """Minimal float32 WAV (format tag 3) — to test the bit-depth-32 copy path."""
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name + ".wav")
    data = b"".join(struct.pack("<f", s) for s in samples)
    n = len(data)
    hdr = (b"RIFF" + struct.pack("<I", 36 + n) + b"WAVE"
           + b"fmt " + struct.pack("<IHHIIHH", 16, 3, 1, SR, SR*4, 4, 32)
           + b"data" + struct.pack("<I", n))
    open(path, "wb").write(hdr + data)
    return path, len(samples)

FULL = 32767
vectors = {
    "silence":  [0]*1024,
    "impulse":  [FULL] + [0]*1023,
    "dc_full":  [FULL]*1024,
    "ramp":     [(-FULL + 2*FULL*i//1023) for i in range(1024)],
    "step":     [0]*512 + [FULL]*512,
    "sine_1k":  [FULL*0.9*math.sin(2*math.pi*1000*i/SR) for i in range(4096)],
    "altmax":   [FULL if i % 2 == 0 else -FULL for i in range(1024)],
}
for n in (1, 2, 3, 4, 5, 8, 760, 768, 776):
    vectors[f"len_{n:04d}"] = [FULL]*n     # constant, varying length -> block size

def main():
    made = []
    for name, s in vectors.items():
        made.append(write_wav_i16(name, s))
    # float32 variants to probe the uncompressed (bit-depth 32) path
    made.append(write_wav_f32("f32_dc",   [0.5]*1024))
    made.append(write_wav_f32("f32_ramp", [(-1.0 + 2.0*i/1023) for i in range(1024)]))
    print(f"wrote {len(made)} WAVs to {OUT}")
    for path, n in made:
        print(f"  {os.path.basename(path):16s} {n:5d} samples")
    print("\nNext: import each into the Mimic Instrument Editor, export a .lib per "
          "input, then run:  tools/drd_probe.py <that_lib>")

if __name__ == "__main__":
    main()
