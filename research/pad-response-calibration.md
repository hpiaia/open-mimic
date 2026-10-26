# Per-pad sensitivity and velocity response

Response calibration is independent of the shared TensorFlow/LiteRT classifier.
The classifier decides background/head/rim/rimshot. A pad response controls its
sensor noise thresholds and the amplitude-to-MIDI-velocity mapping after that
decision. No classifier weights are edited or retrained by response calibration.

## Automatic flow

Run `python3 scripts/receive_pad_measurements.py` in a Mac terminal and use the
normal twenty-second-per-articulation wizard. After a complete pad session is
verified and published on the Mac, the receiver fits and sends its response
profile to the module. The module validates the pad, physical route, sensor count,
finite values and sample counts before atomically persisting/applying it. The
receiver waits for an attempt-specific acknowledgment, then allows the normal
SD archive acknowledgment. The touchscreen displays **Calibrated** after both
response application and archival complete.

A new complete capture replaces the response for that pad; earlier sessions are
not silently merged into a different trigger's response. All recordings remain
available for training the shared model. Replacing a physical trigger or changing
single/dual zone therefore calls for another capture, not classifier retraining.
The profile is disabled after a sensor-count change until a matching calibration
is applied. Changing MIDI notes does not alter the measured physical response.

If data lacks an articulation, has too few usable hits, insufficient dynamic range
or unusable quiet data, the raw recording is still archived and the previous
working response remains active. The UI reports the reason. Network failures
leave the verified Mac archive and the SD copy available for retry. Profiles can
also be generated from existing verified sessions using:

```sh
python3 scripts/calibrate_pad.py measurements/tom1/SESSION
```

This inspection command prints the proposed response; the receiver handles transfer.
It also discovers the latest already-archived session for a pad when upgrading
from a receiver version that did not fit response profiles.

## Measured quantities and explicit policy

`calibrate_pad.py` uses only the current session's guided recordings. Every input
file is checked against its SHA-256 verification manifest. Quiet statistics use
at least 2,560 completed frames from the ambient take; gap records are excluded.

For each active sensor:

- Baseline: median raw ADC sample during the quiet recording.
- Noise mean: mean absolute deviation from that baseline, with a 0.25-count floor.
- Noise limit: 99.9th percentile of the absolute deviations.
- Initial onset threshold: `max(2, noise_limit + 2)` ADC counts above baseline.

The live threshold never falls below that initial floor. Quiet residual noise
can increase it by the ratio of current to recorded mean noise. The head and rim
sensors are gated separately; a dual-zone pad starts a candidate when either
sensor crosses its threshold. Profiles seed the quiet baseline during warm-up;
a hit during profile activation is excluded from that baseline update. Quiet
rearming, the existing 32-frame waveform and 128-frame holdoff remain event policy.
Classification confidence is still checked separately; a lower onset threshold
is not a guarantee that the current model will accept every ghost note.

Each articulation needs at least eight usable onset candidates and eight ADC
counts of observed dynamic range. Head velocity uses the primary sensor peak;
rim velocity uses the secondary sensor peak; rimshot velocity uses the maximum
of the two. This prevents cross-coupled secondary energy from defining a head
hit's loudness. These peaks use the same 32-frame window as live interpretation.

Response algorithm 2 requires the articulation's own sensor to cross its measured
threshold before using its peak: primary for head, secondary for rim, both for
rimshot. Onsets triggered only by the other sensor are excluded from that
articulation's references, not counted as zero-strength hits. The report records
the exclusions; at least eight qualifying candidates are still required. This
fixes cases where head activity during a rim take made its lower percentile
overlap the rim's quiet noise and rejected the entire response profile. It does
not independently verify articulation labels.

The manual receiver refreshes the latest verified session when its stored
response algorithm is older. The native profile format remains version 1;
algorithm versioning belongs to the host fitter, not the C++ file schema.

The fifth and 95th percentiles of each articulation's observed peaks define its
soft and strong references. The initial response exponent is 1.0:

```text
u = clamp((peak - soft_reference) / (strong_reference - soft_reference), 0, 1)
velocity = round(1 + 126 * u^exponent)
```

Values below/above the observed range saturate to 1/127. Soft references do not
reject hits: detection remains based on sensor noise. This is an empirical playing
response, not a measured physical force estimator. Eight samples and percentile
choices are explicit initial policies, not claims of universal pad accuracy.
Quiet recordings must actually be quiet, and the player must include light and
normal strong strokes for meaningful reference levels. Clipped waveform windows
are excluded, rather than using saturation as a force reference.

The response fitter and shared-model training use the same verified ambient data
for continuous onset extraction. Whole-source/pad holdouts still prevent derived
windows from leaking into validation. All raw recordings remain available for
later extraction refinements; the learned classifier is kept separate.

## Storage and runtime

The active module settings are `/var/lib/mimic/pads/profiles/PAD.profile` and
persist across service restarts and boots. They bind to the compiled physical
routing and sensor count. Mac-side diagnostics are in `measurements/.profiles/`;
per-session results and the latest successful response are retained separately.
Deleting training recordings does not implicitly erase a working module setting.

The strict text format begins `OPEN_MIMIC_RESPONSE 1`, followed by pad/session,
physical route and a source-manifest hash. Two sensor rows contain noise mean,
noise limit, threshold and baseline. Three articulation rows contain minimum,
maximum, exponent and sample count; inactive mono articulations have zero samples.
The daemon accepts bounded, staged files and acknowledges each transfer token in
its status JSON. Invalid replacements cannot overwrite the last valid profile.

Only POD response values enter `TriggerEngine`. Profile parsing, disk I/O and
transfer happen outside frame interpretation; the frame path performs arithmetic
and reuses existing state. Without a matching profile, the previous threshold and
velocity mapping remain in effect. Missing/bad profiles affect their own pad's
response settings, not the availability of the shared model or other pads.

Administrative recovery uses `mimicd pad-command profile-reset PAD`, which restores
the uncalibrated fallback without deleting recordings or changing MIDI notes.
