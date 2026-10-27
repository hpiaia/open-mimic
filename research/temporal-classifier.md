# Temporal classifier comparison protocol

The next comparison uses TensorFlow/Keras and the existing LiteRT CPU runtime.
It retains the production float32 `[1,69] -> [1,4]` interface and the 32-frame
acquisition window. This experiment tests model capacity without adding capture
latency or changing sensor routing, MIDI notes or response profiles.

## Extraction and supervision

`offline_onset_candidates_v2` fixes quiet-window selection: every active sensor
must remain strictly below its own onset threshold for the entire window. The
previous shared half-threshold wrongly excluded quantized quiet signals. Onset
selection, rearming and holdoff are unchanged by this fix.

Guided task labels are not independently verified strike labels. For training
only, a head/rim positive needs a peak at least three times that articulation's
sensor threshold. A rimshot needs both sensors to cross their own thresholds and
at least one to reach the three-times ratio. Candidates failing this evidence
rule remain unknown; they are not relabeled as background. Evaluation retains
them all and reports the ambiguous subset separately. This avoids treating a
recording task's entire noise/ringing tail as confirmed positive supervision,
but does not prove that the filtered subset is perfectly labeled.

Background/head sensor-mask augmentation stays within the training partition.
Class weights assign equal mass to classes, then equal mass to each observed
pad/session/class cell. Normalization uses training data only.

## Models and fixed settings

- **Linear:** existing standardized 69-feature softmax, Adam 0.03, 400 epochs.
- **CNN:** reshape the first 64 channel-major samples into `(32,2)`, Conv1D
  (12 filters, kernel 5), max pooling, Conv1D (16 filters, kernel 3), global mean
  and max pooling, concatenate the five standardized auxiliary features,
  Dense(24), dropout 0.1 and four-class softmax. Adam 0.003, batch 256, 80 epochs.
- Seed 1729. Existing single-zone output mask forbids rim/rimshot on mono pads.
- Framework layers, gradients, optimizers and official LiteRT conversion only.

These settings are fixed before running the comparison. Any subsequent tuning
against these folds is development work, not independent release testing.

## Validation and promotion

Seven whole-pad folds and three whole-session folds are used. Session folds
hold out one deterministic, complete session per pad; no raw recording or
derived window may cross either boundary. The session order is by session ID,
not playing strength or model performance. All variants share these splits.

Reports include confusion matrices, per-class recall, weak/ambiguous subsets,
per-pad results and the current 0.65 probability gate. Window classification
is not continuous hit detection, a false-trigger rate or end-to-end MIDI latency.

Numerical promotion requires each present positive class to reach at least
85% gated recall in every held-out pad, background rejection at least 98%, and
each session fold's present-class macro recall at least 90%. These thresholds
are a conservative development gate, not an established product specification.
Actual ARM conversion agreement, inference timing and physical playing tests
are still required. The comparison script never deploys a model automatically.

The offline candidate extractor currently uses a fixed ambient baseline and
32-frame quiet rearming; the live engine adapts its quiet baseline/noise and
rearms after 16 quiet frames. Offline holdoff begins at onset, while live
holdoff begins when classification finishes. These are known distribution and
event-policy differences. Do not claim training/live candidate parity or hide
them by tuning the classifier. A replay of the actual C++ engine is required
before making continuous-triggering performance claims.

## Run

```sh
build/ml-venv/bin/python scripts/compare_drum_models.py MANIFEST.json \
  --output-dir NEW_OUTPUT_DIRECTORY
```

The command writes the protocol before training, verifies original source
hashes, records per-window provenance and training eligibility, saves every
fold's report, then exports the final `.keras` and `.tflite` artifacts for both
architectures. No old checkpoint is loaded.

## Separate-support sensor adaptation experiment

After observing the raw-window comparison, an additional development experiment
uses `--calibration-adapter sensor-demix`. It reserves the first complete session
by lexical ID of each pad exclusively as calibration support. No support window
enters classifier training or evaluation; the other two sessions supply queries.
Seven whole-pad folds and two whole-query-session folds are used.

For dual-zone support, the per-window absolute sensor peak vector is normalized
by its largest peak. Component-wise median vectors from sensor-evidenced head
and rim candidates form the two columns of a mixing matrix. NumPy inverts that
matrix; condition numbers above 20 are rejected. The inverse is scaled to have
infinity norm at most 1, bounding output amplitude without amplifying absolute
ADC noise. Mono pads retain identity. Rimshot support labels do not fit the
matrix. The same matrix transforms both query channels before normal feature
extraction; velocity calibration and physical ADC routing remain independent.

Eligibility is determined on original sensor evidence, before transformation.
Changing query waveforms cannot change the support matrix; tests verify this
and reject insufficient/collinear support. These are slot-based physical pad
groups, not independently identified commercial trigger models.

This is a **few-shot calibration** experiment, not zero-shot generalization.
Its results are not directly comparable to folds using all three sessions as
queries. The current live engine does not apply these transforms. Artifacts
explicitly declare `runtime_input_compatible: false`; they must not replace the
active raw-input model even though tensor dimensions are identical.

The matched `--calibration-adapter support-control` run reserves exactly the
same support sessions and uses the same query folds, eligibility, class balance,
architectures and epochs, but leaves the query waveforms untransformed. Compare
against this control to attribute a result to the matrix rather than the changed
session composition. Original three-query-session results alone are not a valid
before/after test of adaptation.

References:

- [Keras Conv1D](https://www.tensorflow.org/api_docs/python/tf/keras/layers/Conv1D)
- [Training-only normalization and temporal splits](https://www.tensorflow.org/tutorials/structured_data/time_series)
- [TensorFlow Lite performance measurement](https://github.com/tensorflow/tensorflow/blob/master/tensorflow/lite/g3doc/performance/measurement.md)

## Results — 2026-10-26

The corrected extractor recovers 1,017 background windows without changing any of the original 8,107 positive onset candidates: 11,259 total windows, 3,152 background, no excluded takes. The evidence policy retains 6,463 training-eligible windows; all held-out windows are still evaluated.

| Architecture | Mean held-out pad gated macro recall | Mean held-out session gated macro recall | Parameters | LiteRT size |
| --- | ---: | ---: | ---: | ---: |
| linear | 57.5% | 52.9% | 280 | 4,312 bytes |
| cnn | 61.4% | 59.5% | 1,736 | 14,668 bytes |

The macro scores average present-class recalls per fold, then average folds. Mono pads lack rim/rimshot examples. These scores include the fixed 0.65 event-output gate and are not directly comparable to the earlier ungated, unfiltered pilot. Both architectures here share identical splits, training evidence and balancing policies.

**Neither model passes the promotion gate. The active three-class model was retained.** The CNN improves average performance and rejects quiet windows more reliably, but regresses on held-out Aux 2 head hits and still fails rim/rimshot generalization on Tom 4. For the training-eligible held-out Tom 4 subset, CNN gated recall is 97.8% head, 7.3% rim and 31.1% rimshot. A single aggregate improvement cannot justify promotion.

The next model experiment should examine per-sensor gain/coupling normalization using a separate calibration support session, with the remaining sessions reserved for evaluation. Deriving such normalization from the evaluated session would leak articulation information into the test. This is a hypothesis, not a confirmed cause or a reason to change the owner-confirmed ADC map.

## Actual ARM verification

Both final artifacts were checked through the production LiteRT C API on the Mimic, with built-in CPU kernels and one inference thread. Each matched all 17,602 host conversion cases with zero argmax disagreements. Maximum probability differences were below 0.000001.

| Architecture | Mean inference | p95 | p99 | Maximum observed |
| --- | ---: | ---: | ---: | ---: |
| linear | 55.2 µs | 94.3 µs | 156.6 µs | 12.51 ms |
| cnn | 136.8 µs | 194.5 µs | 291.2 µs | 13.94 ms |

Each benchmark uses 10,000 timed invocations, 144 varied cases and 100 warm-up calls, at nice level 10 alongside existing services. Timings include buffer I/O and scheduling delays. They are not end-to-end MIDI latency or a real-time deadline guarantee; the observed maxima exceed the DMA ring duration. No model was promoted on the basis of average inference cost.

## Actual engine replay and startup recovery

`mimic-trigger-replay` calls the same C++ `TriggerEngine` as the live service; it does not open ADC or MIDI devices. Use `scripts/replay_pad_session.py` to verify an archived session, decode physical channel tags and generate the replay input:

```sh
python3 scripts/replay_pad_session.py measurements/tom1/SESSION_ID \
  --profile measurements/.profiles/tom1.json \
  --tool build/mimic-trigger-replay --output-dir NEW_REPLAY_DIRECTORY
```

Build the native tool through CMake with JUCE/UI disabled, or use the Linux/native target build. The default replay writes features without a classifier; a LiteRT-enabled executable accepts `--model`. Input lines are `frame_index head_raw rim_raw`; a line containing only `-1` resets the engine at a discontinuity. The index is an ordering axis, not a missing-duration estimate. The companion `mimic-drum-benchmark` tool accepts a model, feature rows and an iteration count.

On a Mac with the command-line compiler installed, the feature-only tool can
also be built directly without adding a native LiteRT SDK:

```sh
c++ -std=c++17 -O2 -I software/common \
  software/tools/replay_triggers.cpp software/common/TriggerEngine.cpp \
  software/common/DrumModel.cpp software/common/PadCalibration.cpp \
  -o build/mimic-trigger-replay
```

For the latest Tom 1 session, the actual C++ engine produces 67 head-take candidates versus 65 from the Python extractor, 58 versus 48 for rim and 52 versus 49 for rimshot. This confirms the documented train/live event-policy mismatch. With the candidate CNN, actual ARM replay of the head take produces 59 event outputs from 67 candidates; this is an output count, not 59 independently verified correct hits. Its two-second ambient take produces zero candidates/events. These replays use a response fitted from the same session and are mechanism checks, not independent model validation.

Replay also exposed an uncalibrated startup failure after a gap: warm-up could average an ongoing strike into the baseline, preventing all later quiet arming. Before the first successful quiet arming only, the engine now retries uncalibrated baseline bootstrap after 128 unsettled frames. Calibrated startup and the normal post-hit event policy are unchanged. Synthetic replay, the C++ regression suite and an actual ARM engine test pass. The corrected daemon was installed; the active classifier checksum is unchanged.

## Response update and artifacts

Response algorithm 2 rejects counterpart-only candidates from per-articulation velocity references. All seven mapped pads now have active response profiles with no calibration errors, including snare and Tom 4. The latest verified recordings were refitted through the manual receiver; original raw captures remain unchanged. Previous derived profiles and the previous daemon are backed up under `build/response-v1-backup/`.

Private artifacts live under `build/temporal-comparison-v2/`: fixed protocol, dataset/provenance, every fold, both Keras/LiteRT artifacts, ARM agreement and timing reports, and replay evidence. Original data remains in `measurements/`. These local artifacts are ignored by Git and require separate backup.

## Matched sensor-adaptation results

Both runs use the same 7,512 query windows, the same support session per pad, the same eligible training IDs and identical fold sizes. Their input query ID/hash/label/eligibility sets were checked for exact agreement. The linear/CNN weights are retrained separately for each representation.

| Model | Raw control: pad macro | Matrix: pad macro | Raw control: session macro | Matrix: session macro |
| --- | ---: | ---: | ---: | ---: |
| linear | 57.1% | 56.9% | 52.0% | 53.4% |
| cnn | 63.1% | 66.5% | 59.6% | 59.7% |

On the sensor-evidenced held-out Tom 4 query subset, CNN gated head/rim/rimshot recall changes from **96.0% / 18.2% / 27.7%** without the matrix to **81.9% / 84.5% / 67.7%** with it. Background rejection is 100% in both versions for that fold. The matrix helps distinguish coupled rim activity but regresses on head and remains insufficient for rimshot. No numerical promotion gate passed.

This supports investigating calibration-aware generic classification; it does not validate a static mixing model for every trigger. Temporal channel coupling, ambiguous segmentation and unverified individual strike labels remain limitations. The prototype matrix is an experimental external calibration parameter, not a per-pad trained classifier. No matrix was applied to the live engine or response files.

The adapted CNN also matched 11,741 ARM/LiteRT comparisons on **host-pretransformed** features with zero class disagreements and maximum probability difference 1.49e-06. This verifies the network operators only; it does not verify an on-device adapter, which is not implemented.

Matched artifacts are in `build/calibrated-temporal-comparison/` and `build/calibration-support-control/`. `paired-control-comparison.json` records matching dataset checks, metrics and ARM scope.

## Training-protocol study — 2026-10-26 (evening)

Goal: find what still limits the generic classifier after the matrix experiment.
All runs below use the same 7,512 query windows and support sessions as the
matched sensor-adaptation runs, so their folds are directly comparable. New
options in `scripts/compare_drum_models.py`: `--calibration-adapter sensor-gain`,
`--augmentation sensor-gain`, `--few-shot-support` and `--seed`.
`scripts/summarize_comparison.py` checks window identity before comparing two
runs; `scripts/seed_variance.py` reports per-fold spread across seeds.

### Why Tom 4 fails zero-shot

Per-articulation median rim/head peak ratios show Tom 4 is physically unlike the
other toms: a Tom 4 rim hit peaks at only 0.35× on the rim sensor relative to the
head sensor, while Toms 1–3 peak at 2.7–4.1×. Tom 4 head hits sit at 0.07 and
rimshots at 0.22. A model that has never seen Tom 4 has no way to know that a
head-dominant window is a rim hit there. This is a per-pad coupling fact, not a
label or capacity problem.

### Seed variance

Three seeds of the raw support-control CNN differ by up to 28 points in one
fold's class recall (Aux 2 head 34–62%, Tom 4 rim 2–18%). Differences of a few
points between single runs are not evidence of anything.

### Candidates that do not need a support session in training

| CNN variant (evidenced held-out Tom 4 head / rim / rimshot) | Pad macro | Tom 4 | Notable regressions |
| --- | ---: | --- | --- |
| Raw control | 63.1% | 96 / 18 / 28 | — |
| Matrix demix (not runtime compatible) | 66.5% | 82 / 85 / 68 | Tom 4 head |
| Per-sensor gain equalization (not runtime compatible) | 65.6% | 42 / 65 / 92 | Tom 4 head, Aux 2 head 62→25 |
| Training-time gain augmentation (runtime compatible) | 63.7%* | 90 / 68 / 37 | Snare rimshot 82→29 |
| Gain equalization + augmentation | 66.6% | 83 / 78 / 75 | Tom 2 background 100→88, Aux 2 head |
| Demix + augmentation | 63.8% | 80 / 92 / 51 | Tom 2 background 98→39 |

\* measured against the three-session baseline, not the support-control run.

Every representation trade moves the error between pads rather than removing it.
None is recommended.

### Few-shot: the calibration session enters training

`--few-shot-support` keeps each pad's reserved support session as training-only
rows in every fold. No support window is ever evaluated; query windows, fold
sizes and eligibility are identical to the support-control run. This mirrors
real use: the calibration wizard already records head, rim and rimshot for each
pad before playing.

| CNN, evidenced held-out recall (head / rim / rimshot) | Zero-shot control | Few-shot raw | Few-shot seed range |
| --- | --- | --- | --- |
| Aux 2 | 62 / — / — | 87 / — / — | 82–87 |
| Snare | 100 / 99 / 86 | 100 / 98 / 88 | 100 / 98–100 / 88–91 |
| Tom 1 | 85 / 89 / 88 | 95 / 97 / 85 | 95–97 / 92–98 / 85–88 |
| Tom 2 | 87 / 93 / 89 | 95 / 98 / 79 | 89–95 / 95–98 / 79–84 |
| Tom 3 | 89 / 95 / 87 | 97 / 95 / 87 | 91–97 / 95–98 / 87–91 |
| Tom 4 | 96 / 18 / 28 | 92 / 88 / 86 | 92–98 / 83–89 / 82–89 |

Background rejection stays at 99–100% in every few-shot pad fold and seed. The
largest seed spread under few-shot is 7.7 points. Raw input is used, so the
live engine needs no adapter. On top of few-shot, the demix matrix, gain
equalization, gain augmentation and 160 CNN epochs all land inside seed noise
and are not worth their complexity.

**Decision: no model was promoted or installed.** The numerical gate still
fails: rimshot stays at 79–91% evidenced recall, below 85% on Tom 2 in every
seed, and the gate's whole-window metrics include the ambiguous subset below.
The recommended protocol change is to treat the owner's calibration session as
training data for the owner's model; the existing all-session final model
already does this for this kit, and its expected behaviour is the session-fold
column, not the zero-shot pad column.

### What the ambiguous subset is

Of 8,107 positive-labeled candidates, 4,796 fail the 3× evidence rule. Their
median absolute peak is 3 ADC counts, at thresholds of 2–3 counts; 2,261 head,
744 rim and 802 rimshot candidates peak between 1× and 2× threshold. These are
noise blips that armed the offline extractor during a guided take, not light
strikes. Gated "all"-window recall of roughly 40–55% therefore mostly measures
correct rejection of noise, and the current gate cannot pass on it. The gate
should be restated on sensor-evidenced windows plus a separate false-event rate
from engine replay of ambient takes; this is proposed, not done.

The same 2–3 count thresholds drive the live engine. Each noise blip opens a
32-frame window and a 128-frame holdoff (about 11 ms at 11.25 kframes/s) during
which a real strike cannot arm. Whether this masks real hits in play has not
been measured and is the next replay question.

Artifacts: `build/exp-*/`, `build/training-protocol-study.json`,
`build/ambiguous-window-summary.json`. Local only; not in Git.

## Within-kit study — 2026-10-26 (night)

Goal restated by the owner: reach near 100% on this kit with the existing
recordings; no other kits are expected for now. The relevant protocol is the
three whole-session folds (two sessions of every pad train, the third is held
out), scored on sensor-evidenced windows. `scripts/within_kit_study.py` runs
it with pad-conditioned variants the module could use because it knows the
pad slot: a shared CNN, a three-seed ensemble, a shared CNN with a pad one-hot
input, a per-pad two-layer fine-tune of the shared CNN, and a per-pad CNN.

Minimum gated recall over the three folds, CNN, evidenced windows:

| Pad | Shared (head / rim / rimshot) | Per-pad fine-tune | Per-pad model |
| --- | --- | --- | --- |
| Aux 2 | 96 | 100 | 96 |
| Kick | 98 | 96 | 98 |
| Snare | 99 / 97 / 84 | 99 / 99 / 84 | 96 / 100 / 84 |
| Tom 1 | 96 / 97 / 81 | 97 / 97 / 81 | 97 / 86 / 71 |
| Tom 2 | 88 / 94 / 82 | 96 / 97 / 74 | 97 / 90 / 79 |
| Tom 3 | 91 / 87 / 91 | 99 / 97 / 91 | 98 / 85 / 88 |
| Tom 4 | 94 / 85 / 81 | 94 / 94 / 74 | 94 / 88 / 81 |

The per-pad two-layer fine-tune (Dense(24) + classifier, 60 epochs on the
pad's own training windows) is the best head/rim option and is cheap enough
for the module. The ensemble and the pad one-hot input did not beat it. Gate
rejections are now a minor error source; gated and argmax recall differ by at
most three points.

Rimshot is the remaining gap and it is a label problem. Misclassified
held-out rimshots are 1–6 windows per pad per fold, and their rim/head peak
ratios fall inside the same pad's pure-rim range (2–3.8) or pure-head range
(0.06–0.2). A ratio-consistency rule (`--clean-rimshots`: exclude rimshot
candidates at or beyond the pad's head q90 / rim q10) flags 5 of 93 snare
rimshots but 63 of 94 on Tom 3 and 72 of 103 on Tom 4, almost all rim-like.
Rim/head onset-timing deltas do not separate them cleanly either. With the
flagged windows removed, per-pad fine-tune reaches 92–100% rimshot on snare,
Tom 1 and Tom 3 and 80–86% on Toms 2 and 4, but that is partly circular: it
measures the windows the rule kept. Whether the flagged windows were real
rimshots cannot be decided from the recordings. The rimshot class also has
about 95 evidenced windows per pad against about 230 for head.

Conclusions:

- Head and rim on this kit are at 97–100% with per-pad fine-tuning, except
  Tom 4 head (94% minimum, 1–4 windows per fold).
- Rimshot needs ground truth, not more modelling: one new, careful rimshot
  take per dual-zone pad, reviewed against the model's own disagreements,
  is the only new recording that would change the result.
- Deployment path that follows from this: shared base model, per-pad
  two-layer refit on the module at calibration time, per-class thresholds.
  Not implemented; no model installed.

Artifacts: `build/exp-within-kit*/evaluation.json`.

## Deployment note — 2026-10-26 (night)

The per-pad two-layer fine-tune from the within-kit study is now the
production path: see "Per-pad classifier adaptation" in
[pad-calibration.md](pad-calibration.md). Seven pad models trained from the
kit base `8f080af3…` are active on the module; the shared factory model is the
fallback. Per-class gates default to 0.65 and can be set per pad through the
model sidecar. The rimshot label review tool exists for the next rimshot
recordings. Nothing in this note changes the measured numbers above.
