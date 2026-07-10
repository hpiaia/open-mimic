# Mimic Instrument Editor r106 analysis

This note records static-analysis results for the official Windows x64 Mimic
Instrument Editor 1.0.6 (`Mimic Instrument Editor.exe`, SHA-256
`9681ac64faab4a67f6f5434dfa0c89926d638b0934b05aff50bb34b221cf0002`).
The copyrighted executable is not stored in this repository.

## Conclusion: no hidden multi-mic authoring UI

The Editor contains multi-mic-capable export structures and legacy mic-label
strings, but its live project format and UI model do not contain an independently
editable microphone collection. The stock r106 application therefore cannot
author close + overhead + room layouts through a hidden control or visibility
gate.

This is stronger than the earlier observation that documented UI lacks mic
controls:

1. The saved `MimicInstPrj` project serializer contains displayed instrument and
   library names, version, `articulations_count`, instrument type, custom image,
   articulation layer dimensions, velocities, and WAV paths. It serializes no mic
   count, position, type, name, pan, volume, enable flag, or envelope-model flag.
2. A scan for every access to the exported instrument object's mic region found
   only construction/destruction, import/export, and project-to-export conversion
   code. No JUCE component constructor, callback, layout method, or visibility
   gate accesses that region.
3. During export, the value written to the output mic-count field is read directly
   from the project model's `articulations_count` at offset `+0x10`; it is not read
   from a separate mic collection.
4. The bracketed strings such as `[Mic_position_Overhead]` and
   `[Mic_position_Room]` have no code references. Scanning all 10,487 functions at
   the reference, instruction-operand, and decompiler P-code levels found no
   consumers in their contiguous table region.

## Binary and Ghidra project

- PE32+ x86-64 Windows GUI application, image base `0x140000000`
- Ghidra 12.1.2 auto-analysis: 10,487 functions, 61,413 symbols
- Local project: `~/ghidra-projects/OpenMimicEditor.gpr`
- Local analyzed program: `/Mimic Instrument Editor.exe`

## Function map

Names below are provisional descriptions assigned from decompilation; addresses
are stable for the r106 executable hash above.

| Address | Provisional role | Evidence |
|---|---|---|
| `0x1400e09d0` | Save MIE project | Creates `MimicInstPrj`; writes `articulations_count`, instrument type/image, articulation/layer metadata and WAV paths; no mic metadata |
| `0x1400e5080` | Convert live project to export model | Copies `project+0x10` into `export+0x80004`, then copies `0x158`-byte records from the project into the export model |
| `0x1400e5b70` | Export instrument | Calls the conversion routine and the binary serializer |
| `0x1402ac7f0` | Serialize export model | Writes the record count and iterates `micinf` records of size `0x158` |
| `0x1402ab8f0` | Deserialize export model | Reads the record count and `micinf` records |
| `0x1402a83d0` | Serialize one `micinf` record | Writes mic fields including `micen` and envelope state |
| `0x1402a7c70` | Deserialize one `micinf` record | Reads mic fields including `micen` and envelope state |
| `0x1402ad8a0` | Export-model constructor/reset | Constructs eight `0x158`-byte records at `+0x80008` |
| `0x140002170`, `0x1402ad720`, `0x1402dd320` | Destruction helpers | Destroy the fixed mic-record array |

The exported model layout relevant here is:

```text
+0x80004  int32 record_count
+0x80008  record[8]
           stride 0x158
+0x80ac8  next field (articulation count in the larger export object)
```

The live project model begins its articulation collection near:

```text
+0x10     int32 articulations_count
+0x18     first 0x158-byte record (as consumed by export conversion)
```

The naming collision is important: the exporter feeds articulation-oriented
project records into fields named `micinf` in the binary export schema. Future
compiler work should follow the decoded on-disk SCM library semantics rather than
assuming the public Editor's project model represents all valid mic layouts.

## Reproduction scripts

- `tools/ghidra/FindMicTableConsumers.java` scans references, operands, and
  decompiler P-code for consumers of the legacy mic-label string table.
- `tools/ghidra/FindMicModelAccess.java` enumerates accesses to the export model's
  mic-record region.
- `tools/ghidra/FindMicStride.java` is a broad diagnostic for uses of the `0x158`
  record size; most matches are unrelated and require contextual filtering.

## Implication for OPEN MIMIC

Patching a hidden flag is not a viable route. The practical path is an independent
authoring/compiler tool that directly emits the already-decoded multi-mic library
schema. The official Editor remains useful as a single-mic/reference encoder and
for black-box `.drd` comparisons.
