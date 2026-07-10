# OPEN MIMIC — Session Handoff

Last updated: 2026-07-10

Read this first when resuming on another machine. It summarizes the goal, what is
already figured out, what lives where, what must be re-acquired (large binaries are
**not** in the repo), and the prioritized next steps.

---

## 1. Goal

Build **OPEN MIMIC** for the **Pearl Mimic Pro** electronic drum module (abandoned by
Pearl). The immediate, concrete deliverable the owner wants:

> A **new instrument editor** that can **attach multiple mics to samples**
> (close/direct + overhead + room), which the stock public Mimic Instrument Editor
> cannot do.

Longer term: an open OS/firmware. But the near-term, high-value, low-risk win is
multi-mic instrument authoring, which runs on **unmodified stock firmware** (drop
files into `/mnt/user/instruments/`).

---

## 2. TL;DR status

- **Device internals fully identified.** Mimic Pro main board carries a **CompuLab
  CL-SOM-AM57x** SoM = **TI Sitara AM5728 (dual Cortex-A15, ARMv7-A hardfloat)**,
  mSATA SSD (**HooDisk HDSSESB-128GB**, 4× EXT4 partitions), TI PCM1690 DACs,
  ADS7953 trigger ADCs. Full BSP (U-Boot/kernel/Yocto) is public from CompuLab.
- **Firmware update format cracked.** `.mup`/`.mos` = `mimic_software_update` /
  `mimic_storage` containers, **zlib + CRC32C, no signature**. `.mos` runs an embedded
  string via `system(3)` → a root/shell path on-device without opening the case.
- **Instrument library format cracked & byte-exact round-trippable.** See §5.
- **Installed-instrument audio codec implemented.** The verified 1024-byte signed
  delta codec exists in Python and Rust. See §6.
- **Multi-mic is baked into the format AND the Editor's serializer.** Static analysis
  now rules out a hidden multi-mic authoring UI in r106: the saved project model has
  no independent mic collection and export derives its record count from
  `articulations_count`. See §7 and
  `docs/research/instrument-editor-r106-analysis.md`.
- **Hardware validation succeeded.** A newly encoded six-channel multi-mic kick
  imported and played all close/OH/room signals correctly without crashing.
- **Rust compiler implemented for Tauri.** See `compiler/` and
  `examples/multimic-kick.json`.

---

## 3. Repo layout (this is what transfers)

```
open-mimic/
  README.md
  HANDOFF.md                     <- you are here
  docs/research/
    pearl-mimic-pro-os-research.md      OS/firmware public + package findings
    reverse-engineering-map.md          working map (facts vs inferences)
    hardware-api-map.md                 /dev nodes, custom driver APIs
    hardware-primary-source-notes.md    primary-source hardware refs
    compulab-cl-som-am57x-notes.md      the SoM: AM5728, BSP links
    mimic-pro-firmware-1.4.18-analysis.md  .mup/.mos + app ELF symbol analysis
    instrument-format.md                .bin/.kit/.drd + checksum.dat format
    scm-mimic-library-source-notes.md   SCM library package facts
    drd-codec-notes.md                  .drd audio codec (disassembly findings)
  tools/
    analyze_mup.py       decode/extract a .mup firmware package -> app ELF
    analyze_mos.py       decode a .mos OS-update (note the system() payload)
    mimic_kv.py          decode mimic_storage + checksum.dat (canonical reader; --verify-md5)
    mimic_lib.py         section-aware reader + BYTE-EXACT round-trip proof
    drd_probe.py         inspect/diff .drd audio chunks (for codec black-box)
    gen_test_vectors.py  emit known WAVs for Editor black-box codec RE
    capture_mimic_state.sh  on-device state capture (run once we have a shell)
  assets/
    SCM Mimic Library/   (~1.7 GB) the Rosetta-Stone multi-mic library + install PDF
```

`assets/SCM Mimic Library/Tama SCM Steve Mackrill.lib/` is the reference multi-mic
library (compiled by Pearl's devs). It is the ground truth for the format work.
If the repo is transferred without it (large), re-download from
<https://theedrumworkshop.com/blogs/news/scm-mimic-download>.

---

## 4. External artifacts to RE-ACQUIRE on the new PC (NOT in repo)

These lived in `/private/tmp/open-mimic-fw/` and a scratch dir — they will not copy
with the repo.

1. **Pearl 1.4.18 PC package** (`pearl-mimic-pro-pc-1.4.18.zip`, ~22 MB) — the official
   firmware + Instrument Editor. From Pearl's Mimic Pro firmware/library support page
   (`pearldrum.com` → Mimic Pro → 1.4.18 Software Update, PC). Contains:
   - `mimic_pro_1_4_18.mup` (firmware)
   - `Mimic Instrument Editor r106 Setup.exe` (NSIS installer)
2. **Firmware app ELF** — extract from the `.mup`:
   `python3 tools/analyze_mup.py <mimic_pro_1_4_18.mup>` → `mimic_app_1_4_18.elf`
   (ARM32, **not stripped** — the authoritative spec for engine/codec/trigger logic).
3. **Instrument Editor binary** — unpack the NSIS installer:
   `brew install sevenzip` then `7zz x "Mimic Instrument Editor r106 Setup.exe"`.
   Real binary: `$APPDATA/Steven Slate Audio/MIE/Mimic Instrument Editor.exe`
   (PE32+ x86-64 JUCE app; the VC_redist is noise). This is the multi-mic **encoder**.

---

## 5. Instrument library format (SOLVED)

Full spec in `docs/research/instrument-format.md`. Essentials:

- A library is `<Name>.lib/` with `checksum.dat`, `libver.mimicinfo`, `kits/*.kit`,
  and `instruments/<Inst>.bin` (small descriptor) + `<Inst>.drd` (large audio).
  **Naming is inverted**: `.drd` = audio payload, `.bin` = metadata.
- `.bin`/`.kit` = **`mimic_storage`** container: 14-byte magic + u32 version(=3) +
  u32 uncompressed size + zlib stream.
- Payload = a sequence of **sections**, each `[u32 count][records]`; record styles:
  `i32` (4B), `byte` (1B flag), `arr4` (`[u32 n][n×4]`, unused slots `0xFFFFFFFF`),
  `blob` (`[u32 len][bytes]`, strings + embedded PNG).
- **`tools/mimic_lib.py` round-trips all 27 SCM files byte-for-byte.**
- **Multi-mic model (decoded):** `INST<i>micnt` + `INST<i>micinf<m>{micpos,mict,mictn,
  micn,isst,micen,micv,...}`. `mict` 0=close/direct(mono) 1=ambient(stereo). `micpos`
  5=Overhead 6=Room (shared), others instrument-specific. `pool0nchn` = Σ(mono close)+
  Σ(2×stereo ambient). Zones (`zonecnt`/`zoneType`/`zoneArtId`) map strike zones →
  articulations. Velocity/round-robin via `artic<a>veloLay<v>smpidx` (16-slot arrays).
- `checksum.dat` = "mimic_checksum_list" + zlib list of `filename + MD5`; covers all
  `.bin`/`.drd`. Decoded/verified by `tools/mimic_kv.py --verify-md5`.

---

## 6. Audio codec (`.drd`) — understood, see `docs/research/drd-codec-notes.md`

- `.drd` chunks are addressed by pool `dofs`/`dlen` (`max(dofs+dlen)` == file size).
- **Not** raw float32 and **not** FLAC (no `fLaC` marker; JUCE's FLAC is only for
  importing source audio). Effective rate ~1.5 bytes / interleaved sample (~2.6×).
- Firmware decoder path: `CDiskStreamer::doDecomp` / `doDecomp_wave` →
  `CDataCompression::DecopressRIFFDataToFloat` / `testDecompress1024Bytes` (768/1024-B
  blocks → float32).
- The installed-instrument path uses verified 1024-byte delta blocks: a signed
  24-bit predictor and residual width in word 0, decoded count in word 1, then
  MSB-first signed deltas. `tools/drd_codec.py` and `compiler/src/codec.rs`
  implement it.
- A newly encoded six-channel instrument was validated successfully on real Mimic
  hardware: two mono close channels, stereo OH, and stereo room all played correctly.

---

## 7. The Editor multi-mic question (resolved: no authoring UI)

- The Editor's **export serializer supports multi-mic records** (all `micinf*` keys
  present; full dormant vocabulary: `[Mic_Direct_Mono_Name]`, `[Mic_Bleed_Stereo_Name]`,
  positions Top/Bottom/Front/Back/Inside/Overhead/Room, `[Mic_Volume]`,
  `[Mic_Pan(0.5_is_center)]`, plus `[Enable_envelope_modeling]`).
- The documented UI workflow (help text steps 1–11) has **no mic step** (one WAV per
  velocity/round-robin cell). "Show advanced settings…" turned out to be JUCE
  **audio-device** options (a red herring), not mic authoring.
- Follow-up analysis with Ghidra 12.1.2 + GhidraMCP scanned all 10,487 functions.
  The saved `MimicInstPrj` serializer writes articulations, layer dimensions, WAV
  paths, names/type/image, but **no microphone metadata**. All accesses to the
  exported mic-record region belong to construction/destruction, import/export, or
  project-to-export conversion; no JUCE UI constructor/callback/layout code touches
  it. Export copies `articulations_count` directly into the output record count.
- The bracketed mic label strings have no consumers even when references,
  instruction operands, and decompiler P-code constants are scanned across the
  whole program. They are dormant/legacy format vocabulary, not evidence of a live
  panel.
- Detailed addresses and reproduction scripts:
  `docs/research/instrument-editor-r106-analysis.md`.
- **Reproduce the Editor RE** (headless Ghidra): install JDK + Ghidra (see §9), import
  `MIE.exe`, and run the Java scripts in `tools/ghidra/`. Ghidra 12 needs **Java**
  scripts unless PyGhidra is set up.

---

## 8. Hardware validation (compiler path complete)

The custom multi-mic compiler path has been validated on the user's device. Separate
firmware/hardware research can still use this path:
1. Pull the mSATA (removable, clones cleanly per the VDrums thread) and **image it
   read-only** (`ddrescue`), OR use the `.mos` `system()` payload to get a shell and
   dump the rootfs. Keep the stock HooDisk as golden master.
2. From the image: confirm SoC (`/proc/cpuinfo`), partition→mount map, kernel/DTB,
   custom drivers, and where `/mnt/user/instruments/` lives.

---

## 9. New-PC environment setup

- **Python 3** (stdlib only) for all `tools/*.py`.
- **7-Zip** to unpack the Editor NSIS installer: `brew install sevenzip` (mac) / distro
  pkg (Linux).
- **Ghidra headless** for binary RE (no sudo needed):
  - JDK 21: `brew install openjdk@21` (formula, not the cask which needs sudo).
  - Ghidra: download the release zip from
    <https://github.com/NationalSecurityAgency/ghidra/releases>, unzip anywhere.
  - Run: `JAVA_HOME=<jdk> <ghidra>/support/analyzeHeadless <proj_dir> <name>
    -import <binary> -scriptPath <dir> -postScript <Script.java>`
    (use `-process <binary> -noanalysis` to re-run scripts on the analyzed project).
  - **Ghidra 12 scripts must be Java** unless PyGhidra is set up.
- `objdump` (LLVM, ships with macOS CLT) disassembles the ARM ELF fine
  (`objdump -d --start-address=.. --stop-address=..`). For PE x86-64 too.

---

## 10. Prioritized next steps

1. Integrate the Rust crate in the Tauri UI (`compiler/README.md`).
2. Add template coverage for snares, toms, cymbals, hats, and multiple articulations.
3. Add kit compilation and a friendly template-selection layer.
4. Image the SSD only if resuming the separate firmware/hardware goal.

---

## 11. Notes / gotchas

- Two overlapping readers exist: `mimic_kv.py` (canonical: sections + checksum + MD5)
  and `mimic_lib.py` (byte-exact round-trip). Consider consolidating.
- `.drd` is a custom block codec, **not** FLAC — don't be misled by JUCE's bundled
  FLAC symbols (used only to read user source audio).
- Legal: fine to reverse-engineer/modify your own abandoned device and to write clean
  OPEN MIMIC code. Do **not** redistribute Pearl's firmware/app binary or the
  Slate/Pearl sample libraries; a shipped compiler must require users to supply their
  own legally-obtained samples.
