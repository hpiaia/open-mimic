# New capture training evaluation — 2026-10-26

## Dataset

All 21 verified sessions (three per pad: kick, snare, Tom 1–4 and Aux 2) were included as sources. Every archived file checksum was verified before training. Kick and Aux 2 are configured as single-zone. All other pads are dual-zone. No old recordings or checkpoints were used.

The current extractor produced 10,242 candidate windows: background 2,135, head 4,624, rim 2,079 and rimshot 1,404. These are heuristic onset candidates, not independently counted physical strikes. Six recordings yielded no eligible windows; original files remain intact. All three Tom 1 ambient recordings yielded no quiet windows, so that fold cannot assess background rejection.

## Method

TensorFlow/Keras linear softmax classifier; 69 features, four classes, 400 epochs, seed 1729. Each validation fold holds out an entire pad slot and all of its sessions. Training normalization and class weights use training data only. Masked single-zone augmentation stays on the training side. Physical pad models and serial numbers were not recorded; slot holdout does not prove generalization to every trigger type.

## Held-out pad results

Values are class recall on extracted guided windows, using the largest output probability. They do not include the live 0.65 confidence gate, velocity response or end-to-end triggering.

| Held-out pad | Background | Head | Rim | Rimshot |
| --- | ---: | ---: | ---: | ---: |
| aux2 | 60.2% | 90.3% | — | — |
| kick | 90.7% | 100.0% | — | — |
| snare | 100.0% | 98.0% | 55.2% | 29.6% |
| tom1 | — | 33.1% | 93.9% | 46.6% |
| tom2 | 100.0% | 37.6% | 86.2% | 37.1% |
| tom3 | 100.0% | 23.0% | 86.3% | 28.0% |
| tom4 | 100.0% | 30.4% | 38.9% | 36.7% |

## Promotion decision

**Not promoted.** Head and rimshot generalization is inadequate. Even final training-set accuracy is only 71.9%; additional captures alone are not established as the solution. Investigate onset alignment/rearming, weak/noise candidates, the short feature window and classifier capacity before requesting more recordings. Quiet-window extraction also needs review because valid ambient recordings can yield no examples. These results do not establish that the owner played the wrong articulations.

Per-pad response fitting is independent: kick, Aux 2 and Tom 1–3 each had three accepted sessions; Tom 4 had one accepted session; snare had none. Snare rim references and two Tom 4 attempts overlapped measured noise. Previous valid responses were retained. No threshold or profile was changed by this training run.

## Export and device validation

- Candidate SHA-256: `6ae47bea84993f96f1e66c5dcb63c4d38b229a1171014198322eaad5c38948f7`.
- LiteRT file: 4,312 bytes, fixed `[1,69] -> [1,4]` float32.
- TensorFlow/export agreement: 15,568 cases; maximum difference 3.58e-07.
- Actual Mimic ARM/LiteRT agreement: 15,568 cases; zero class disagreements; maximum difference 3.88e-07.
- Candidate ran through the standalone classifier from `/tmp`; the active model and services were not replaced.

## Local reproduction and artifacts

```sh
build/ml-venv/bin/python scripts/train_tensorflow_model.py \
  build/new-capture-training/manifest.json \
  --output-dir NEW_OUTPUT_DIRECTORY --epochs 400
```

Private local artifacts are in `build/new-capture-training/`: the verified source manifest, capture audit, training log, Keras checkpoint, LiteRT candidate, full fold/confusion reports, per-window provenance, conversion cases and ARM comparison. Raw captures remain under `measurements/`. Build artifacts and measurements are ignored by Git and require a separate backup.
