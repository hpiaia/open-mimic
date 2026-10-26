# Pad calibration and waveform learning

The calibration service uses one generic pad definition and one shared form.
Kick, snare, tom and auxiliary names identify configurable slots; they do not
select different calibration implementations. Every pad can be single-zone
or dual-zone. The service collects labeled data, edits the MIDI map and runs
a shared TensorFlow-trained model through LiteRT for experimental live MIDI.
The touchscreen Start MIDI / Stop MIDI control persists across restarts.
Known crosstalk limitations remain; see the model card and runtime section below.

## Temporary ADC map

The tracked initial map is
`software/common/PadRouting.h`. The owner corrected the
initial assignments after testing and saved this map on the device
(2026-10-26):

| Pad | Head | Rim | Global channel indices |
| --- | --- | --- | --- |
| Kick | ADC1 channel 2 | ADC1 channel 3* | 18, 19 |
| Snare | ADC1 channel 0 | ADC1 channel 1 | 16, 17 |
| Tom 1 | ADC1 channel 10 | ADC1 channel 11 | 26, 27 |
| Tom 2 | ADC1 channel 12 | ADC1 channel 13 | 28, 29 |
| Tom 3 | ADC1 channel 14 | ADC1 channel 15 | 30, 31 |
| Tom 4 | ADC0 channel 0 | ADC0 channel 1 | 0, 1 |
| Aux 2 | ADC0 channel 8 | ADC0 channel 9* | 8, 9 |

*Secondary channels for the single-zone kick and Aux 2 follow the physical
input pair; they have not been validated with a second-zone strike.

Channels now identify the ADS7953 channel tag, not the sample position in a
DMA frame. Earlier tables used DMA positions, which rotated across boots.
See [ADC channel identification](adc-channel-identification.md).

ADC and channel indices are zero-based; global index is `ADC * 16 + channel`.
Remaining auxiliary pad channels are unassigned. Earlier Tom 1/2 assignments
were off by one channel and must not be used for new recordings. MIDI head-note defaults are editable
GM starting points, not a verified Superior Drummer preset. Rim notes are
initially unassigned.

The configuration begins with `OPEN_MIMIC_PADS 2`. Each row contains:

```text
PAD ZONES HEAD_ADC_CHANNEL RIM_ADC_CHANNEL MIDI_CHANNEL HEAD_NOTE RIM_NOTE
tom1 2 26 27 10 48 -1
```

`-1` means unassigned. The writable configuration in
`/var/lib/mimic/pads/config` overrides the initial map. Each physical input retains its two-channel assignment regardless of the
connected pad's zone count. Single-zone mode preserves the secondary channel
and MIDI note but omits rim stages and rim waveform input to the model.
Trial metadata uses `rim: null` for an inactive zone and `input_rim` for the
physical secondary channel. A changed zone/channel configuration
starts a new recording session; historical captures remain intact.

## Touchscreen workflow

1. Open **Pad calibration** and select a pad from the grid.
2. Under **Inputs**, choose Single zone or Dual zone and inspect its channel
   assignments. Save changes. Unknown channels must be measured before
   collecting; the live channel grid helps identify activity.
3. Under **Capture**, select **Quiet** and start. Avoid striking any pad.
4. Record the head with soft, medium and hard isolated strokes. Dual-zone
   pads also expose the corresponding rim stages. Follow the on-screen
   instructions and allow each stroke to decay before the next one.
5. During **Crosstalk**, strike other drums while leaving the selected pad
   untouched. These are negative examples for the selected pad.
6. Discard the most recent capture if its label was wrong. Rejected timing,
   clipping or activity checks do not advance the accepted-trial count.
7. Use **New session** and repeat the stages for an independent validation
   recording. Twelve accepted trials per stage are a pilot collection target,
   not proof that the dataset is sufficient for a reliable performance model.

The MIDI map stores channel and note assignments separately from ADC channels
and waveform labels. Changing a note does not change how raw signals are
recorded. There is no automatic MIDI output from calibration.

## Capture representation and quality

`mimicd pad-service` publishes `/run/mimic-pads/status.json` and accepts
bounded FIFO commands via `/run/mimic-pads/command`. `S57mimic-pads` starts it
at boot. Portability tests may pass `--data-directory PATH`; ordinary boot
uses `/var/lib/mimic/pads` on the SD system. The original SSD is not written.

Every completed trial has an OMADC002 file and a JSON sidecar with format
`open-mimic-pad-trial-v1`. It records pad/session/phase, zone count, channel
assignments, collection threshold, cursor-quality counters and the snapshot
count. The full raw rings retain all 32 ADC channels. Metadata is published
only after the raw file closes. Hardware gain is explicitly `null` because
its current register state has not been measured by this service.

A window contains eight preceding snapshots, the onset snapshot and sixteen
following snapshots, nominally polled every 2 ms during collection. Idle meter polling uses 20 ms
and does not contribute capture-quality counters. Collection uses a heuristic
to locate isolated events; that heuristic is not a trained trigger engine.
Quiet windows are collected without hit onsets. Approximate storage is
205 kB per trial. The wizard reserves 16 MiB of free filesystem space. Completed pad sessions
transfer automatically to the Mac and release their SD copies only after
verification; there is no fixed 48 MiB dataset cap. See
[the current wizard and archive workflow](calibration-wizard.md).

Quality checks reject malformed/stalled cursors, long polling gaps, ring-copy
overlap, clipping and unexpected activity in quiet captures. Ordinary cursor
movement during a copy is expected. Modulo cursors cannot prove exact lost
frame counts. Snapshot timestamps bound availability; they are not precise
sample timestamps. Synchronization between the two ADCs remains unproved.

## Download and historical baseline training

Keep captures and model artifacts outside Git. The download helper stops
collection, waits for pending writes, verifies remote/local SHA-256 values,
and leaves the device copies intact:

```sh
python3 scripts/fetch_pad_dataset.py ../open-mimic-artifacts/pad-datasets/session-copy
python3 scripts/train_drum_model.py ../open-mimic-artifacts/pad-datasets/session-copy \
  --pad tom1 --inspect
python3 scripts/train_drum_model.py ../open-mimic-artifacts/pad-datasets/session-copy \
  --pad tom1 --validation-session SESSION_ID \
  --output-dir ../open-mimic-artifacts/pad-models/model-01
```

The original baseline trainer uses Python's standard library. Production
training now uses TensorFlow/Keras, as described below. It reconstructs completed frames,
cross-checks them against the following snapshot, rejects incompatible or
corrupt data, and uses a 32-frame onset-aligned waveform. Dual-zone pilot
training currently requires both channels on the same ADC; single-zone
training uses its one channel and zero-fills the absent second input.
Generic capture itself preserves both ADCs for later synchronization work.

The pilot learns a class-balanced linear softmax model over normalized
waveforms and four amplitude/magnitude features. Single-zone models classify
background/head; dual-zone models add rim. Soft/medium/hard annotations remain
in the dataset and evaluation, but do not create separate articulation classes
or establish true strike-force measurements. Velocity estimation and rimshot
articulations are not implemented in this pilot.

Training statistics use training sessions only. The selected validation
session is held out completely; identical capture payloads cannot appear on
both sides. Historical incompatible channel/zone configurations are excluded
and reported. Evaluation includes per-phase results so easy quiet examples
cannot hide poor crosstalk rejection. A two-channel classifier alone is not
proof of robust cross-pad discrimination or preserved simultaneous hits.

Outputs are `model.omdrum`, `model.json` and `evaluation.json`. The portable
C++ `DrumModel` loads the numerical weights off the real-time thread and
performs allocation-free inference. `mimic-drum-classify MODEL WAVEFORM.txt`
accepts 32 centered samples per configured zone, channel-major, for offline
checks. The model header records its zone/class counts. Its evaluation and
provenance must accompany it before any future installation; weights alone
do not bind a model to a physical pad, gain or sampling cadence.

Probabilities are not calibrated confidence guarantees. No validation score
automatically enables MIDI. A future continuous event engine still needs
onset/event policies, measured latency, velocity calibration, rolls/ghost-note
tests and simultaneous-pad validation. There are no trained user-pad weights
until the owner supplies the guided recordings.

## Checks

Native CTest covers completed-frame handling, configuration-independent
single/dual numerical inference and existing audio/MIDI primitives.
`scripts/test_pad_service.py` exercises the complete service with synthetic
ADCs in an isolated Linux container; it does not establish hardware timing.
`scripts/test_drum_model.py` tests reconstruction, quality rejection, session
separation, single/dual training and Python/C++ prediction agreement with
synthetic fixtures. Synthetic accuracy is a software check, not evidence of
real pad accuracy.

Hardware checks on the owner's Mimic completed quiet collection with both
ADCs, downloaded captures with matching SHA-256 hashes, and reconstructed all
12 accepted windows in the training reader. A separate run writing to the
actual SD filesystem accepted 12 windows and retained one rejected timing
window; the trainer excluded that rejected trial. These quiet-only checks do
not validate strike classification. Test recordings are archived separately
from the empty user dataset. Visual checks covered pad selection, Tom 1 input
assignments, changing to single-zone mode, MIDI mapping and an unassigned
snare slot.

ADC snapshots use aligned 32-bit volatile reads from the shared DMA mapping,
preserving full raw bytes and before/after cursors. In one two-second idle
measurement on this device, service CPU usage fell from approximately 52% to
19% of its exposed CPU. This is a local measurement, not a latency guarantee.

The binaries, factory map and `S57mimic-pads` were installed on the owner's
SD. After the final reboot, SSH did not return; automatic startup after that
reboot is not verified. The next physical test should select the SD boot
path with the BOOT button and confirm the UI and service are running.

Idle CPU work was reduced by polling display-only ADC snapshots every 20 ms,
skipping hidden calibration/MIDI panel refreshes unless an operation is pending,
and repainting only waveform-meter/channel-grid regions. Capture retains its
2 ms cadence. After deployment, a three-second measurement reported about 17%
total CPU on the home screen and 25% on the input screen; the idle pad service
used about 2–3%. These are local samples with the existing Wi-Fi adapter, not
worst-case measurements. A separate temporary hardware run after the cadence
change accepted 12 quiet windows without rejection. Owner recordings and the
saved pad/MIDI configuration were preserved; a verified copy of all 475 files
was saved outside Git before deployment. The owner dataset has reached the
48 MiB collection limit and has not been deleted.

The owner subsequently corrected Tom 4 to ADC0 channels 1 (head) and 2 (rim).
The earlier 0/1 assignment is obsolete. The new recordings retain all 32 raw
channels: median ADC0 channel-1 peaks in Tom 4 head-soft/medium/hard trials
were approximately 41/168.5/494 counts, consistent with the corrected head
assignment. Channel 2 also has a response, but a separate labeled rim strike
is still needed to validate it. Existing trial metadata keeps its original
mapping; training must apply an explicit provenance-recorded correction to
a derived dataset rather than silently changing original recordings.
The verified `toms-snare-20261026` archive has complete 12-trial stages for
snare and Tom 3. Tom 4 has 12 quiet, 12 soft head, 12 medium head and five
hard head trials; the dataset quota prevented completion. Nothing was deleted.

## Historical shared numerical baseline

The first shared numerical model was developed for
`software/models/shared-drum-pilot`. `scripts/train_shared_drum_model.py` trains
one model across pads and evaluates complete configured pad groups held out
from the weights and feature normalization. This supersedes per-slot training
as the development direction; `train_drum_model.py` remains an extraction and
single-pad diagnostic tool.

The first run uses 329 usable real trials, including a provenance-recorded
Tom 4 map correction. It augments head/background trials with disabled-secondary
views after partitioning; those views are not additional physical recordings.
An explicit 69th feature marks the secondary sensor as active/inactive.
`OMDRUM02` models share three class outputs and mask the rim class when that
sensor is inactive; the runtime still reads legacy `OMDRUM01` models.

Four leave-one-pad-group-out folds produced complete-group balanced accuracies
of 90.3% (snare), 75.5% (Tom 1), and 97.2% (Tom 3), averaging 87.7%.
Tom 4 lacks rim trials and is reported separately, without a three-class
balanced score. Poor rejection of unseen Tom 1 crosstalk prevents promoting
this pilot to an automatic trigger. Final weights include all eligible data;
their training score is not independent validation of those final weights.
No physical pad model identities were recorded, so group holdouts cannot prove
accuracy on different commercial trigger models.

Native and real ARM C++ inference were compared with Python on 550 cases
(329 real dual-zone windows and 221 derived primary-only windows). Class
outputs agreed and probability errors were below 0.00002. The weights are
initially installed as an offline artifact. That restriction describes the
previous numerical baseline, superseded by the TensorFlow/LiteRT runtime below.

New archives can be added to the source manifest and the small model retrained
with all accumulated data. Preserve fixed regression suites and obtain fresh
independent test recordings: repeatedly optimizing against the same holdout
folds turns them into development data. Incremental weight updates would also
need older examples replayed to check for regression; they are not implemented.


## TensorFlow training and LiteRT live execution

Production training uses `scripts/train_tensorflow_model.py` and the pinned
`software/ml/requirements.txt` environment: TensorFlow 2.20, Keras `Model.fit`,
class-balanced cross entropy, Adam and L2 regularization. Shared extraction
and whole-pad holdouts remain the same. A `.keras` training checkpoint retains
optimizer state; a fixed float32 `[1,69] -> [1,3]` `.tflite` artifact is exported
with the official Keras converter. New verified archives can be added to the
manifest and the model retrained with all accumulated data.

The ARM application links Google LiteRT 2.3 through its CompiledModel C API.
The Buildroot external `litert-runtime` package cross-compiles the runtime
for the existing ARMv7/musl target. Framework sources and dependencies are
pinned; patches disable unused vendor/GPU downloads and expose creation errors.
The CPU backend explicitly uses built-in optimized kernels with one thread:
XNNPACK delegate initialization failed on the device. Model buffers are
allocated once, warmed up and reused; no Python is installed on the module.

On the Mimic all 550 reference classifications agreed with TensorFlow and
maximum probability error was below 0.000001. A 20,000-invocation tight loop
averaged approximately 46 microseconds including buffer I/O. These figures
measure model execution, not worst-case or end-to-end strike-to-MIDI latency.

The acquisition worker copies only newly completed ADC frames while live
interpretation is active. Calibration retains complete raw-ring snapshots.
Cursor validation rejects malformed/overwritten intervals, and polling gaps
above 8 ms reset acquisition and event state. Modulo cursors cannot identify
unobserved complete wraps; observed ring duration is approximately 11.3 ms.
`engine.adcFrames` and `engine.dmaResets` expose processed frames and resets.
Cross-ADC dual-zone pairs are rejected because synchronization is unverified.

Event policy uses quiet-baseline initialization, a minimum 16-count onset,
32 onset frames, classification confidence >= 0.65, quiet rearming, clipping
rejection and a 128-frame holdoff. These explicit timing policies remain
necessary around the learned classifier; they are not a learned force model.
Velocity currently follows peak amplitude. Each configured pad has independent
state, but simultaneous playing and rolls still need physical validation.

Calibration pauses MIDI. Saved head/rim note assignments control routing;
`-1` remains silent. The acquisition worker sends bounded, nonblocking `hit`
commands to the existing MIDI service. That service owns 100 ms note-off
scheduling, cancels an older timer before retriggering the same note and
releases pending notes on shutdown. Engine status includes accepted events,
queued/dropped MIDI, unmapped notes and the latest prediction.

The model card documents poor Tom 1 held-out crosstalk rejection. The live
engine is enabled for owner testing and refinement, not a claim of universal
pad accuracy. Real strike force, commercial pad identity, quick calibration,
continuous false-trigger rate and complete physical latency remain unmeasured.
