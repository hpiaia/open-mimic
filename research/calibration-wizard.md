# Guided pad measurement and MIDI mapping

## Hardware routing and SD3 defaults

`software/common/PadRouting.h` is the single compiled physical route table.
It uses ADS7953 channel tags, independent of DMA phase. ADC routing is read-only;
the dedicated **ADC monitor** displays all 32 channels and their head/rim owners.
Unmeasured auxiliary inputs remain unassigned rather than receiving guessed routes.

The independent **Pad MIDI map** screen edits channel and head/rim/rimshot notes.
The initial preset follows the Toontrack-authored
[Superior Drummer 3 keyboard layout](https://guitar-type.com/wp-content/uploads/2021/07/SDX-Keys2.pdf).
The [manufacturer's published Toontrack layout](https://cdn.toontrack.com/app/uploads/pdf/EZD3_standard_layout.pdf)
also lists the tom rim-only and rimshot assignments. Libraries can omit particular
articulations; this is the default mapping, not a guarantee that every loaded kit
has all those samples enabled.

| Mimic input | SD3 instrument | Head / center | Rim only | Rimshot |
| --- | --- | ---: | ---: | ---: |
| Snare | Snare | 38 | 71 | 40 |
| Tom 1 | Rack Tom 2 | 47 | 79 | 80 |
| Tom 2 | Rack Tom 3 | 45 | 77 | 78 |
| Tom 3 | Floor Tom 1 | 43 | 74 | 75 |
| Tom 4 | Floor Tom 2 | 41 | 72 | 73 |
| Aux 2 | Rack Tom 1 | 48 | 81 | 82 |

Kick defaults to note 36. Notes can be disabled with `-1`; switching between single
and dual zone retains the secondary route and both articulation assignments.
Configuration version 3 stores `PAD ZONES MIDI_CHANNEL HEAD_NOTE RIM_NOTE RIMSHOT_NOTE`.
A version-2 file is migrated once: measured routes come from code, zone counts are
retained, and the requested SD3 note defaults replace the former note assignments.
The old file is backed up before deployment. ADC numbers are not settings anymore.

## Wizard

Choose a pad, choose single/dual zone, and press **Start calibration** once.
A two-second automatic ambient recording measures the quiet kit. Then each
articulation gets one continuous twenty-second take, with a short three-second
preparation countdown: head, rim only, rimshot. Single-zone pads record head only.
Vary from very light to normal strong playing inside each take; rolls and close
hits are welcome. There is no hit quota, intensity subdivision or per-hit gate.
Only play the selected pad. The raw recording includes both ADCs, preserving the
other pads' vibration for later crosstalk analysis without a separate playing step.

The service owns timing and progression, so closing/reopening the UI does not
control timing or lose an active wizard. Live MIDI pauses during calibration.
At completion the screen waits for verified Mac archival, then enables **Next pad**.
Stopping preserves completed takes; start a new wizard for a fresh session. A
service restart does not resume an unfinished wizard; partial sessions remain
available for explicit receiver import. The UI process can restart independently.

Guided labels identify articulation, not independently measured force. The current
model and velocity scale are not automatically retrained by the capture wizard.
The collection has no amplitude threshold: every completed ADC frame is recorded.
Later extraction uses documented onset heuristics while retaining the originals.

## Continuous format

Discrete windows remain raw `OMADC002`, with tagged physical-channel metadata.
Timed takes use `open-mimic-pad-sequence-v1` metadata and `.omseq` files:

- Little-endian header: `8s magic="OMSEQ001", uint32 version=1, uint32 poll_us`.
- Repeated prefix: `uint64 elapsed_ns, uint16 adc0_frames, uint16 adc1_frames, uint32 flags`.
- Followed by ADC0's frames, then ADC1's frames. Each frame contains sixteen
  little-endian uint16 conversion words, tagged by physical channel in bits 15:12.
- Flag bit 0 marks a polling/copy gap or malformed frame; frame counts are zero
  for that record. Modulo cursors cannot reveal every possible full-ring overrun.
- Timestamps are software acquisition bounds, not per-sample hardware timestamps.
  The converters' sample clocks have not been proved synchronized.

Completed frames are copied once and appended to a bounded take buffer. The writer
publishes the raw file before its metadata. All 32 channels remain available for
later crosstalk and timing analysis. A twenty-second take is around 15 MB, rather than repeatedly
copying two full rings for every poll (~82 MB per twenty seconds). Polling gaps and
clipping remain explicit in records/metadata; valid intervals are not discarded
because another interval has a gap. `scripts/inspect_pad_sequence.py` validates
layout, tags, counts, flags and monotonic timing and reports those limitations.

The existing three-class model remains installed. Rimshot collection does not
magically add that class to its learned weights. TensorFlow training and the C++
LiteRT wrapper now support either three outputs or four outputs ordered
background/head/rim/rimshot. Single-zone masking disables both rim classes.
`sequence_training.py` derives onset candidates and quiet windows from continuous
recordings. Each derived example has its source hash, segment and frame offset.
Gaps split segments, clipped windows are excluded, and distinct examples from one
file are not deduplicated away. Whole-file/pad splits prevent overlapping source
windows from leaking into holdout folds. Segmentation remains heuristic; raw data
allows later re-extraction and evaluation without replaying the measurements.

## Verified Mac archival

Run `python3 scripts/receive_pad_measurements.py` in a Mac terminal while
collecting. It uses the existing SSH key and host configuration and displays
connection, transfer and verification progress. Ctrl+C stops the receiver;
there is no installed background service. The Mac pulls data, so no network
server, password or new credential is installed on the module. The default
host is `root@192.168.31.215`; pass `--host` if the address changes.

At pad completion the service seals `dataset/<pad>/<session>/ready.json` and refuses
further capture into that session. The receiver checks source hashes, copies to a
hidden staging directory, checks source hashes again, verifies every local file,
flushes files and directory entries, and atomically publishes
`measurements/<pad>/<session>/`. Its `download.json` is compatible with the verified
source manifest used by the trainers. Only then does it send `archive-ack`.
The service removes the sealed SD session and writes a receipt keyed by both pad
and session (legacy recordings can share one session ID across multiple pads).
Interrupted acknowledgments can be retried from the durable verified Mac archive.

There is no fixed 48 MiB cap. At least 16 MiB of free SD space must remain, and the
wizard checks for space at every step. Missing network/Mac space never authorizes
SD deletion. While running, the Mac receiver reconnects automatically and pending sessions remain
on the module. Large captures and local receiver logs are excluded from Git.
Back up `measurements/` separately; remote cleanup makes the Mac archive the
remaining active copy after a successful transfer.

Tests cover fixed routing/migration, SD3 notes, background/head/rimshot labels,
continuous takes, the full twenty-second-per-articulation wizard, immutable session
sealing, pad-specific archive receipts, corruption rejection and acknowledgment
recovery. Framework fixtures separately verify three/four-output conversion;
they are not owner recordings and do not establish physical rimshot accuracy.
