# Manual reference for pad development

Reviewed all 100 PDF pages, including illustrated controls and appendices,
of the [Pearl manual](https://pearldrum.com/sites/default/files/image_folder/SUPPORT/MANUAL/ELECTRONICS/2019-mimic-pro-user-manual.pdf),
version 1.0.9, on 26 October 2026. Page numbers below are one-based PDF pages.

Source SHA-256: `cb1d4000a883f05f31fb882eff171903b361c382dfd505da45a67a73607c0a3e`.
The downloaded PDF stays outside Git in
`../open-mimic-artifacts/research/manuals/`; review renders stay under ignored
`build/manual-review/`.

| Subject | Pages |
| --- | --- |
| Pad selection | 18–20 |
| Hi-hat calibration | 21–27 |
| Velocity and rim detection | 28–32 |
| Timing and input gain | 33–34 |
| Assignments and response curves | 35–39 |
| MIDI mapping and octave labels | 40, 44 |
| Crosstalk and ride splitting | 41–43 |
| Presets and setup examples | 45–48 |

The MIDI illustration separates incoming and outgoing assignments. Gain,
zone detection, velocity response and sound volume are distinct controls.
This user manual does not establish internal algorithms or later-firmware
equivalence.

## Proposed Open Mimic work

These are engineering proposals, not descriptions of Pearl's implementation.

1. Extend the existing ADC acquisition with synchronized, timestamped windows,
   hardware-gain snapshots, clipping checks and explicit lost-frame reporting.
   Use the [measured channel map](mimic-trigger-input-reconstruction.md) as
   evidence; do not infer unmeasured jack assignments from names.
2. Build a guided collection workflow for labeled head, rim and rimshot hits,
   varied playing strengths, quiet intervals and neighboring-pad activity.
   Keep mislabeled captures removable. Split evaluation by recording session;
   reserve rolls, ghost notes and simultaneous strikes for acceptance tests.
3. Compare a transparent baseline with waveform-model candidates. Evaluate
   missed/duplicate hits, zone confusion, velocity consistency, crosstalk and
   measured end-to-end latency. Subjective soft/hard labels are not calibrated
   force measurements. Training can run on the development machine; inference
   must be benchmarked on the Mimic before choosing a model/runtime.
4. Publish semantic hit events independently of the destination. Resolve those
   events through editable MIDI profiles before sending them through the
   existing [MIDI transport](midi-io.md). Keep raw captures and evaluation
   independent of the chosen Superior Drummer note map.

No trigger, calibration, ML or MIDI-map implementation is added by this review.
