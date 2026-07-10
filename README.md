# Open Mimic

Reverse-engineering notes and tooling for the **Pearl Mimic Pro** electronic drum module (model MIMP24B1 / MIMP24B1).

This repository collects research, format documentation, and analysis tools produced while studying Pearl's official firmware, the `.mup` software update format, and the Mimic instrument library format — all from publicly available downloads and manuals, treated as data only.

## Contents

- `docs/research/` — Working research notes. Confirmed facts are kept separate from inferences so later hardware captures can replace guesses with data. Key documents:
  - `reverse-engineering-map.md` — Top-level map of what's known.
  - `mimic-pro-firmware-1.4.18-analysis.md` — Analysis of the 1.4.18 firmware ELF.
  - `pearl-mimic-pro-os-research.md` — Public facts and firmware package findings.
  - `instrument-format.md` — Notes on the `.lib` / `.kit` instrument format.
  - `hardware-api-map.md`, `compulab-cl-som-am57x-notes.md`, `drd-codec-notes.md` — Hardware platform notes.
- `tools/` — Python and shell utilities for probing and parsing:
  - `mimic_kv.py`, `mimic_lib.py` — Library/kit format parsing.
  - `analyze_mup.py`, `analyze_mos.py` — Software/OS update file analysis.
  - `drd_probe.py`, `gen_test_vectors.py`, `capture_mimic_state.sh` — Hardware probing and test-vector helpers.

## Notes

- The `assets/` folder (firmware binaries, the SCM Mimic library, etc.) is **not** tracked in git due to size and licensing. See individual research docs for how to obtain the corresponding public files.
- All analysis is based on publicly downloadable firmware and manuals inspected as data. Nothing here redistributes Pearl's copyrighted binaries or sample content.
