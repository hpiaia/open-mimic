# Preserved research

Core references for developing the new system:

- [Observed memory layout](memory-layout.md): RAM exposed by the Pearl boot configuration.
- [Application architecture](application-architecture.md) and
  [JUCE integration](juce-integration.md): process ownership and framework choices.
- [Hardware API](hardware-api-map.md): Pearl devices and ioctls.
- [Analog outputs](audio-output-validation.md), [Aux input](aux-input.md), and
  [wheel control](wheel-input.md): recovered board interfaces and live validation.
- [MIDI input and output](midi-io.md): DIN UART, USB host MIDI, service controls,
  protocol tests and physical loopback validation.
- [Instrument libraries](instrument-library.md): recovered containers and sample
  codec, coherent multi-mic layout, velocity layers, round robin, hi-hat blends,
  and the remaining playback-compatibility checks.
- [Ghidra setup](ghidra-setup.md): REA provider and native decompiler configuration.
- [Trigger path](mimic-trigger-input-reconstruction.md): ADC, DMA, measured
  channels, and next experiments.
- [Manual trigger reference](manual-trigger-reference.md): reviewed manual,
  page index, and proposed calibration, waveform-model and MIDI-map work.
- [Calibration wizard](calibration-wizard.md): fixed ADC routing, SD3 notes,
  automatic guided collection, rapid continuous takes and verified Mac archival.
- [Pad response calibration](pad-response-calibration.md): automatic per-sensor
  sensitivity and per-articulation velocity profiles, separate from classifier weights.
- [New capture training evaluation](new-capture-training-2026-10-26.md): 21 fresh
  sessions, four-class TensorFlow training, whole-pad holdouts, ARM verification
  and reasons the candidate was not promoted.
- [Pad recording inspection](pad-inspection.md): official Rerun viewer, verified
  OMSEQ export, sensor waveforms, training windows and gap provenance.
- [Temporal classifier comparison](temporal-classifier.md): corrected quiet
  windows, TensorFlow CNN/linear development folds, ARM checks and promotion decision.
- [Pad calibration research](pad-calibration.md): recording format, model training
  and historical numerical experiments.
- [Boot and storage](cl-som-am57x-sd-boot-test.md): how SD boot was established
  and what was observed.
- [ZT9101 Wi-Fi](mimic-zt9101-wifi.md): adapter build and hardware test.
- [Original application](mimic-sda2-application-rea.md) and
  [1.4.18 update](mimic-pro-firmware-1.4.18-analysis.md): reverse-engineering
  evidence.
- [CompuLab](compulab-cl-som-am57x-notes.md) and
  [hardware sources](hardware-primary-source-notes.md): documentation.
- [USB device mode](mimic-usb-device-probe.md): unvalidated investigation.
- [Buildroot hardware validation](buildroot-hardware-validation.md): SD boot,
  keyboard, Wi-Fi, SSH, SSD mounts, and device nodes confirmed on the Mimic.
- [Rust ADC validation](mimicd-live-validation.md) and
  [tom 1 captures](tom1-adc-captures.md): live DMA layout, reconstructed frames,
  and head/rim channel observations.
- [Module settings format](module-settings-format.md): decoded storage container
  and the 32-by-32 crosstalk settings matrix.

Some documents preserve commands and paths from the exploratory phase. Those
old installers were removed; use the current root-level scripts for new
builds. Large original dumps remain outside Git in the sibling directory
`../open-mimic-artifacts/`.

- [Shared drum model](../software/models/shared-drum-pilot/MODEL_CARD.md): first joint training, whole-pad holdouts, sensor masks and known crosstalk limitations.
