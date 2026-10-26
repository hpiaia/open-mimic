# Pad recording inspection with Rerun

Use the official **Rerun 0.40.0** viewer on the development computer. The project
provides only an OMSEQ adapter, a default viewer layout and provenance diagnostics.
There is no custom waveform UI or additional runtime dependency on the Mimic.

## Open a recording

From the project root:

```sh
scripts/inspect_pads.sh --pad tom1 --open
```

The first run creates `build/inspection-venv` and installs the pinned host-only
dependencies from `scripts/requirements-inspection.txt`. Python 3.11 or newer
is required. The receiver does not need to be running, and the module does not
need to be connected: inspection reads verified local recordings.

By default the adapter selects the most recently downloaded session for that pad,
using `download.json` timestamps rather than the random session ID. Open an older
session explicitly:

```sh
scripts/inspect_pads.sh --pad snare --session SESSION_ID --open
```

Export all archived sessions without opening multiple windows:

```sh
scripts/inspect_pads.sh --all-sessions
```

Each session becomes `build/pad-inspection/PAD/SESSION_ID.rrd`, accompanied by a
JSON report. Open any exported file directly with the bundled viewer:

```sh
build/inspection-venv/bin/rerun build/pad-inspection/PAD/SESSION_ID.rrd
```

The source recordings stay under `measurements/`. Both the derived exports and
raw measurements are excluded from Git. Existing exports are reused only when
their source hashes, tool hashes, SDK version and RRD hash still match. After
changing the adapter or extractor, select a fresh `--output-dir` to regenerate.

## What the viewer shows

Each guided take has its own tab: ambient, head, rim and rimshot where available.

- **Blue / orange:** physical head/rim signals centered on ambient sensor medians.
  Single-zone recordings omit the unused rim signal.
- **Gray:** measured positive and negative onset thresholds.
- **Green points:** onset candidates found by the current training extractor.
- **Purple window track:** exact 32-frame training windows; 1 for guided hit
  candidates, 0.4 for background windows, 0 outside training windows.
- **Candidate table:** source example IDs, segment/frame positions, guided labels
  and diagnostic flags. Gap records include the original poll-end timestamp bound.

Zoom a signal plot to inspect the samples around a candidate and compare them
with the window track. Use the native viewer's pan, zoom and selection tools;
the original signal remains available at full sample resolution.
Signal overview aggregation uses minimum/maximum values so narrow peaks are
retained when zoomed out. Window tracks disable aggregation to avoid averaging
binary window edges into misleading intermediate levels.

The x-axis is **frame index within that take**, not elapsed wall-clock time.
Tabs represent independent recordings and must not be interpreted as simultaneous
events. Separate line entities prevent interpolation across acquisition gaps.
Each unknown gap reserves one index slot; it does not claim that only one sample
was lost. Poll-end timestamp bounds are available under each take's `poll_bounds`
entity. No interpolation or cross-ADC synchronization is invented.

The display baseline is the ambient median; the existing extractor can use a
slightly different per-segment baseline. The JSON candidate metrics use the exact
extractor window. The green points and labels are **heuristic candidates**, not
independently verified strikes or physical force measurements.

## Diagnostics and safety

The JSON report flags hit candidates whose strongest sensor is no more than twice
its onset threshold, and candidates followed by a peak more than twice as large
outside the initial 32 frames but within the following 256 contiguous samples.
These flags identify review targets, not proven labeling errors. A larger later
peak could also belong to a separate fast strike. Context never crosses a gap.

A valid ambient recording with no eligible windows remains visible in full,
with its extraction exclusion reported. The adapter does not weaken thresholds,
change labels, retrain a model or install anything on the module.

All source payload and metadata hashes are verified before export and checked
again before publishing. Original files are never rewritten. RRD files and
their sidecars record source fingerprints and tool hashes for reproducibility.

## Validation

```sh
build/inspection-venv/bin/python -m unittest discover \
  -s scripts -p 'test_rerun_pad_measurements.py' -v
build/inspection-venv/bin/rerun rrd verify build/pad-inspection/PAD/SESSION_ID.rrd
```

Tests cover shuffled physical channel tags, single-zone handling, gap isolation,
background window display, valid ambient recordings rejected by the extractor,
corrupt payload rejection, read-only exports, cache reuse and latest-session
selection. The native viewer's `--screenshot-to` option supports visual checks.

Official references:

- [Viewer installation](https://rerun.io/docs/getting-started/install-rerun/viewer)
- [Column-oriented logging](https://rerun.io/docs/howto/logging-and-ingestion/send-columns)
- [Scalar plotting](https://rerun.io/docs/howto/visualization/plot-any-scalar)

## First inspection of the 21 new sessions (extractor v1)

The adapter reproduces the training run's 10,242 windows: 8,107 guided onset
candidates and 2,135 background windows across 72 takes. The 263 acquisition-gap
records remain explicit discontinuities; no window or diagnostic context joins
their samples together.

Of the 8,107 onset candidates, 4,503 (55.5%) peak at no more than twice their
sensor threshold. Another diagnostic flags 394 (4.9%) with a substantially larger
peak after the initial window. These sets may overlap. Neither percentage is a
false-trigger rate: weak valid strikes, ringing, onset alignment and separate fast
hits all need to be distinguished before correcting labels or event policies.

All three Tom 1 sessions yield zero background windows, including their ambient
takes. The source remains visible in Rerun. The current extractor requires the
maximum deviation across both sensors and all 32 samples to be strictly below
half of the smallest active threshold. That differs from its sensor-specific
onset test, and can reject quantized quiet signals even when neither sensor
crosses its own threshold. Fix and test that inconsistency before concluding that
the owner needs to repeat the ambient recordings.

Native Rerun inspection around Tom 1 session `0cecc05bab1776f42726b4243fca2e47`,
head take frame 57,231, shows the onset peak inside the 32-frame window followed
by several hundred frames of decay and coupled rim activity. The first window
contains only part of this particular waveform. This supports testing longer
temporal context; it does not establish a required window length or that waiting
for the full decay is compatible with live MIDI latency.

No extraction rule, response profile, classifier weight or live module service
was changed by that initial inspection. The next experiments should compare corrected
quiet-window selection and onset alignment, then evaluate a temporal model with
explicit latency limits using the same whole-session/whole-pad boundaries.

The subsequent v2 extractor now uses each sensor's own threshold for quiet
windows. All 21 sessions produce background examples again: 11,259 total windows,
including 3,152 background windows and the original 8,107 onset candidates.
Updated Rerun exports use these v2 windows. See
[the comparison protocol and results](temporal-classifier.md).
