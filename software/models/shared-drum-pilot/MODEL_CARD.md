# Shared drum waveform pilot

One class-balanced linear softmax classifier is shared across configured pads.
The current artifact uses LiteRT in an experimental live MIDI trigger engine.
Weights are trained on 329 usable owner recordings: 96 each for snare, Tom 1
and Tom 3, plus 41 partial Tom 4 trials. Tom 2 has no available recordings.
Input labels come from the owner's guided playing task, not independently
verified physical articulation or force measurements.

The 69 features are 32 onset-aligned samples per sensor, four amplitude and
magnitude statistics, and an explicit secondary-sensor presence mask. Quiet
baselines are removed before feature extraction. Both channels share a peak
normalization so their relative level remains available. This preprocessing
is not a validated quick-calibration procedure for arbitrary new triggers.
The 32-frame window is approximately 2.8 ms at the estimated recording rate;
it does not measure end-to-end detection latency.

With the secondary sensor disabled, its samples and amplitude features are
zeroed and the rim output is disabled before probability normalization.
Training augments real dual-zone head/background trials with masked views.
These views do not establish performance on real single-zone pads.

## Evaluation

Each fold withholds an entire configured pad group. All synthetic views of
a capture remain on the training side of that fold. Raw capture hashes are
checked for overlap. Means and scales are fitted on each training fold only.
Pad model/serial identities were not recorded, so withholding slots is not
proof of generalization to a new commercial pad or trigger model.

| Held-out pad | Head recall | Rim recall | Background recall | Balanced accuracy |
| --- | ---: | ---: | ---: | ---: |
| Snare | 97.2% | 94.4% | 79.2% | 90.3% |
| Tom 1 | 97.2% | 100% | 29.2% | 75.5% |
| Tom 3 | 100% | 100% | 91.7% | 97.2% |
| Tom 4, incomplete | 96.6% | No examples | 100% | Not defined |

The unweighted mean balanced accuracy of the three complete folds is 87.7%.
Crosstalk rejection is inadequate: when Tom 1 is withheld, all 12 crosstalk
examples are falsely classified as strikes; five of 12 quiet examples are
also falsely classified. Snare rejects seven of 12 crosstalk examples; Tom 3
rejects ten of 12. These are presegmented trial results, not false-trigger
rates over continuous playing. The final weights use all eligible captures,
so their training score is not independent validation of those final weights.

Tom 4 recordings originally carried ADC0 channels 0/1. Extraction explicitly
uses the owner-confirmed 1/2 correction, preserving original metadata and
raw files. Full provenance, excluded trials, per-fold weights, waveforms and
evaluation reports are stored outside Git with the private recording archives.
`metadata.json` records hashes and the detailed phase-level results.

## Reproduce and extend

```sh
python3 -m venv build/ml-venv
build/ml-venv/bin/pip install -r software/ml/requirements.txt
build/ml-venv/bin/python scripts/train_tensorflow_model.py MANIFEST.json --output-dir NEW_DIRECTORY
```

The manifest identifies verified downloads, source pad identities, accepted
channel profiles and any explicit map corrections. Each source directory must
have the `download.json` produced by `fetch_pad_dataset.py`. The owner training
manifest is kept alongside the private model artifacts. Training is deterministic
for a fixed manifest, source content and implementation; artifact paths may vary.
New recordings can be added to that manifest and all data retrained. Compare
whole-pad holdout results and collect additional independent recording sessions
before promoting a replacement. Repeated development against these holdouts
makes them development data; future release claims need fresh independent tests.

```sh
mimic-drum-classify model.tflite waveform.txt
mimic-drum-classify model.tflite primary-waveform.txt --single-zone
```

Waveform files contain 32 centered values per active sensor, head first.
The shared model does not select an ADC or MIDI note. Routing and future
per-pad calibration belong outside its numerical weights.

Velocity estimation, rimshot articulation, event segmentation/retrigger policy,
continuous rolls, simultaneous-pad handling and global crosstalk are not
validated. No model probability threshold automatically authorizes MIDI.

## Production library migration

The active artifact is now `model.tflite`, trained through TensorFlow 2.20 /
Keras with `Model.fit`, class-balanced cross entropy, Keras Adam and L2
regularization. `scripts/train_tensorflow_model.py` exports a fixed float32
`[1,69] -> [1,3]` model using the official converter and saves a `.keras`
training checkpoint. The original manual numerical baseline remains in private
research artifacts; it is not used for live inference.

LiteRT 2.3 executes this model through its modern CompiledModel C API with one
CPU thread. The Mimic uses the framework's built-in optimized CPU kernels.
The XNNPACK delegate did not initialize on this device; built-in CPU execution
was explicitly selected and verified, rather than falling back to bespoke
matrix code. ARM predictions agreed on all 550 reference cases, with a maximum
probability difference below 0.000001. A tight 20,000-prediction benchmark
averaged approximately 46 microseconds per invocation, including buffer I/O.
This is neither worst-case timing nor end-to-end strike-to-MIDI latency.

Live detection is in a separate C++ acquisition worker. It verifies completed
DMA frames, maintains a quiet baseline, collects the same 32-frame onset
window, and invokes LiteRT only for onset candidates. Initial event policy
uses a minimum amplitude threshold, quiet rearming and a 128-frame holdoff.
Classification confidence must exceed 0.65. These are explicit event policies,
not learned timing guarantees. Velocity currently follows signal amplitude;
it is not a learned force estimator. Unmapped notes remain silent.

The acquisition service starts live MIDI at boot unless the owner's persisted
engine setting disables it. The touchscreen provides Start MIDI / Stop MIDI;
calibration capture pauses emission. The MIDI service owns note-off timers,
releases obsolete timers on a retrigger, and releases pending notes on shutdown.
Single-zone pads retain both physical channels but only their primary sensor
participates in detection and classification. The known crosstalk limitations
above still apply: enabling the engine does not constitute validation for all
pads, trigger models, rolls or simultaneous playing patterns.


Per-pad response profiles now calibrate sensor thresholds and amplitude-to-velocity
ranges from verified guided recordings. These settings are separate from this
classifier's weights. Uncalibrated pads retain the prior event/velocity policy;
classification confidence and the known crosstalk limitations still apply.
