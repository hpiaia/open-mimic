# Instrument playback behavior: investigation ledger

This ledger describes bounded static observations from the original Mimic
application. It is not a declaration of complete playback compatibility.
The file-format investigation is separate: metadata, samples and execution
semantics must be correlated before a replacement sampler can preserve them.

## Target and evidence boundary

Original `mimic_pro` SHA-256:
`b12895171c9cb8fd7db067c816ba28b4ae4817869a34654175f3e6dfb683fa8c`.

The observations combine bounded GNU ARM objdump inspection and focused REA
Ghidra decompilation of that ELF. Separately identified comparisons execute
selected original functions in isolated ARM emulation. They are not
observations of the complete application running on the Mimic. Assembly
extracts and exported REA responses are retained locally under ignored
`build/library-research/`.

REA evidence identifiers:

- Selection and articulation loading:
  `ev_61c7efb9947050ef2d63efe248dd03c1d8af33f30433c5ac198b19df5225a43a`.
- Velocity lookup, articulation mapping and voice preparation:
  `ev_2494c4f5c6be62be645f1ab0ebbae670da2bdb1bfe2641d7e9298b8dae801b52`.
- Velocity-layer loading and queue initialization:
  `ev_168cda52f2319e6476ae300dc1129710c5a03063498f7837ff08fdfc75ae7665`.
- Hi-hat selection and pedal processing:
  `ev_43a4755fbdf4ccfd532855984627e506a51e516c9117d0d82f27c891894ea698`.
- Hi-hat table construction and voice rendering/envelope wrappers:
  `ev_771d55b6b404931fd29956911ec1b3ef2e8040b2f995d5f02c5ad5b29da8985d`.
- Single-channel envelope implementation:
  `ev_1f14f73292abef25862ef7e3e43713d33ce522b9a1469740274e3f52dead8333`.
- Parametric and muter release wrappers:
  `ev_f5ed0593b69e8b2b597074ebb253485329a667ce9ac5a31468e2b93426edade3`.
- Snare humanization, cymbal swell and choke dispatch:
  `ev_1db9b4ccdb39597dfe1603f9ab1f0eebdfa1a75b15b9e51e6febb5fa1cb03758`.

## Confirmed static structures

| Component | Observation | Static location |
| --- | --- | --- |
| Velocity lookup | 128 signed-int entries, initialized to zero; input velocity is bounded to 0..127 | `CInstrArticulation::InitVelocityMap` 0x5c3c3c; `ReplyLayerByVelocity` 0x5c43e4 |
| Layer table | 128 allocated records, each 480 bytes, starting at articulation+0x304 | constructor 0x5c39d8; layer count at+0xf304 |
| Velocity bounds | Per-layer minimum and maximum velocity fields | layer+0x190/+0x194 |
| Layer gain bounds | Per-layer minimum and maximum gain fields | layer+0x198/+0x19c |
| Sample alternatives | Each layer starts with 16 sample-index slots; count follows at+0x40 | constructor 0x5c39d8; `PutSampleIdx` 0x5c8b04 |
| Sequential alternative state | A per-layer running index at+0x44 | `GetItemToPlay` 0x5c86e0 |
| Alternative selection workspace | Count/range fields at+0x48/+0x4c, with 80 integer slots between+0x50 and+0x18f | constructor and `GetItemToPlay` |
| Per-mic release | Eight maximum release values at+0x1a0 and eight minimum release values at+0x1c0 | setters0x5c3cac/0x5c3cc8 |
| Mic stereo handling | Up to eight mic records; a stereo mic causes its per-mic release settings to be duplicated for two audio channels | `prepareVoiceItemForTrigger` 0x5cf7a0 |

The table capacities are compiled limits inferred from actual constructor
loops and field arithmetic. They are not counts of populated samples or mics
in any particular library instrument.

## Velocity layers and gain

`BuildVelocityMap` (0x5c40a8) walks layers from highest to lowest. It fills
contiguous velocity lookup ranges using the layer minimum velocity, and writes
a derived upper bound ending at 127 for the highest layer. Starting with an
exclusive upper bound of 128, each descending layer receives
`maxVelocity = upperBound - 1`, fills the map from its minimum to that upper
bound, and makes its minimum the next upper bound. Duplicate minimum values
can therefore leave a layer with an empty lookup range. This establishes
selection of one layer for a given integer velocity.

An independent bounded ARM comparison executes the original builder and
compares its 128-entry map plus every derived layer maximum with the native
`InstrumentSelection`. All 76 distinct threshold layouts from the 1,417
articulations in all 373 instruments match. Including ten synthetic cases
(some also present in the corpus) gives 84 compared layouts. Synthetic cases
include duplicate/unsorted thresholds, out-of-range minima and the compiled
128-layer capacity. The fixture replaces only the builder's memory-zeroing
call with a range-checked `memset` hook. This validates lookup and derived
maximums, not the separate amplitude interpolation below.

`prepareVoiceItemForTrigger` computes a within-layer fraction:

```text
t = (velocity - layerMinVelocity) / max(1, layerMaxVelocity - layerMinVelocity + 1)
gain = layerMinGain + t × (layerMaxGain - layerMinGain)
```

Depending on event and instrument state, the application squares `t` before
interpolation. In the decompilation, the unsquared path is retained for event
zone 0x29, or for the ordinary sample path with multiple layers and the
instrument flag at+0x269588 unset. The flag's user-facing name is unresolved.
The interpolated result is multiplied by a gain stored in the selected
`CSampleItem` (at offset+0x150). Additional slot, articulation and humanization
conditions alter gain later; those branches still need complete correlation.
This is a layer gain interpolation, not evidence that adjacent velocity
samples are crossfaded. Hi-hat processing explicitly uses a separate two-voice
triggering path described below.

## Sample alternatives and round robin

`CInstrumentData::GetItemToPlay` (0x5c86e0) first validates the articulation,
clamps velocity to 0..127, and uses the articulation's velocity lookup to find
one layer. For a layer with one or two alternatives it increments the running
index and wraps at the alternative count. The initial index is zero: one
alternative always selects index 0; two alternatives start with index 1, then
index 0. This path does not advance the shared random-table position.

`CVeloLayer::loadStateInformation` (0x5c5b50) initializes a queue containing
`[0, 1, ..., N-1]`, sets its count to `N`, and sets the random range to
`floor(N/2)`. For more than two alternatives, selection advances a shared
instrument-level cursor through a fixed 100-float table and computes:

```text
cursor = (cursor + 1) % 100
position = max(0, trunc(randomTable[cursor] * floor(N/2) - 0.5))
alternative = queue[position]
remove queue[position] and append alternative to the end
```

The ARM multiply-add is represented with `std::fma` in the replacement
primitive to retain rounding behavior. The table's values lie strictly
between zero and one. Selection is therefore from the first `floor(N/2)`
queue entries; the last `ceil(N/2)` selected alternatives are excluded from
immediate reuse. For three alternatives this degenerates to a repeating
0,1,2 cycle. The count never decreases, so this is not a shuffle bag.

The cursor starts at zero; its first random selection uses table entry 1.
It belongs to the instrument, while queues belong to individual velocity
layers. Interleaving different articulations or layers advances the same
random sequence. The explicit force-first parameter changes the selected
alternative to index 0 after queue/counter state has advanced. If a selected
sample is not ready, the original code tries the layer's first sample, then
returns no sample if that is also unavailable.

The selection produces a single `CSampleItem` before per-mic settings are
expanded in `prepareVoiceItemForTrigger`. All its microphone channels share
the selected take and frame cursor. The decoded payload is interleaved in
stored mic order; details and the critical partial-frame streaming seam are
in [library-sample-codec.md](library-sample-codec.md).

The portable selector was also compared with the original ARM function in
isolated Unicorn emulation: 200 hits for each of 1..16 alternatives, both
without forcing and with forced-first every 13th hit, totaling 6,400 exact
matches. That fixture has one articulation, one velocity layer, velocity 64
and all sample-ready flags set. It establishes the alternative-selection
trace for that declared setup, including random-table wrapping. It does not
independently validate all multi-layer maps, readiness failures or event gain.

One real layer has zero active alternatives: the `Snare Rolls` entry in
`Snare 02 NRG`, with all 16 indices set to -1. The old selection routine can
still reach its first-index read when the active count is zero; safe behavior
for directly invoking that malformed/empty entry is not established. The
replacement intentionally returns no sample for an empty layer. This is an
explicit validation policy, not a claim to reproduce an unchecked negative
pool index from the original.

## Hi-hat control and articulation mapping

`replyArtIdxByInstZone` (0x5c7960) maps a logical zone to an articulation ID
and then searches the articulation array for that ID. An ID is not generally
an array index. It returns index 0 on a failed lookup but also returns status
flags; callers must preserve those flags to distinguish failure from a valid
first articulation.

`CHatCtrlTab::calcArtIdxToPlay` (0x5c1d24) clamps normalized openness to
0..1, calculates `min(int(openness * articulationCount), articulationCount-1)`,
then resolves the resulting control-zone articulation ID to an array index.
The overload at 0x5c1da8 accepts an integer position.

`CHatCtrl::ProcessNoteOn` (0x5c2b94) uses a 100-entry table per control zone,
with `min(max(int(openness * 100), 0), 99)` choosing an entry. Each 36-byte
entry can request one voice or two voices through `TriggerSlot_2voices`, with
separate articulation and gain values. The function also has an explicit
closed-to-open transition path.

`CHatCtrlTab::Build` (0x5c2778) calls `BuildForZone3` (0x5c22d4) for up to
four control zones. Despite its name, the earlier `BuildForZone2` is not used
by this path. `BuildForZone3` generates these fixed hundredth-bin ranges:

| Inclusive bins | First ordinal | Second ordinal | First gain | Second gain |
| --- | --- | --- | --- | --- |
| 0..15 | 0 | none | 1 | unused |
| 16..39 | 1 | none | 1 | unused |
| 40..50 | 1 | 2 | sqrt(1-t) | t; t=(bin-40)/10 |
| 51..69 | 2 | none | 1 | unused |
| 70..85 | 2 | 3 | sqrt(1-t) | sqrt(t); t=(bin-70)/15 |
| 86..89 | 3 | none | 1 | unused |
| 90..95 | 3 | 4 | sqrt(1-t) | sqrt(t); t=(bin-90)/5 |
| 96..99 | 4 | none | 1 | unused |

The first blend is deliberately asymmetric: its second gain is linear. The
other two blends use equal-power square-root gains. There is a hard ordinal
change between bins 15 and 16. Both voices are requested at each blend's
endpoints, even when one gain is zero. Ordinals are bounded to the last
available control-zone entry before resolving the articulation ID. The
function writes no table if a zone has fewer than two entries; the portable
primitive rejects that unbuildable input explicitly.

The four table bytes at record+0x28..0x2b are retained without speculative
names. Their true ranges are, respectively: bins 0..85; 40..50; 0..85; 70..85.
They affect later hi-hat voice behavior and must not be discarded when
integrating the table with a replacement voice engine.

All 100 generated entries were independently checked by isolated ARM
emulation of the original function with Unicorn 2.1.4. The experiment supplied
a synthetic ordered control-zone list [0,1,2,3,4] and replaced only
`replyArtIdxByArtID` with an identity lookup. It retained the original ARM/VFP
instructions, including square-root rounding. The resulting gains, voice
counts, ordinals and four flags match the portable implementation exactly.
This is bounded emulated execution, not REA runtime Evidence or observation
on the Mimic hardware. The generated fixture records that distinction.

The decoded `Hi-Hat Slt 14 VTG` metadata provides a concrete ID mapping:

| Logical zone | Articulation IDs | Meaning in this instrument |
| --- | --- | --- |
| 81 / 0x51 | 5,6,7,8,9 | Tip: closed, quarter, half, three-quarter, open |
| 82 / 0x52 | 0,1,2,3,4 | Shank: closed through open |
| 80 / 0x50 | 12,13,14,15,16 | Generic closed through open |
| 83 / 0x53 | 10 | Pedal |
| 84 / 0x54 | 11 | Footsplash |
| Transition entry | 17 | Hi-hat transition; stored at articulation array index 5 |

These names come from metadata rather than inferred symbol names.
`CHatCtrl::ProcessCCChange` (0x5c31c4) updates the slot's hat controller and
calls `MuteAllLongHatTailVoices` below normalized openness 0.15. It can
generate pedal and footsplash events for zones 0x53 and 0x54. The pedal event
velocity uses a bounded movement-derived value multiplied by 127; one
footsplash branch uses velocity 100. Timing, movement units and the full
debounce/transition policy still require caller and table-builder analysis.

## Articulations, envelopes and choking

Distinct articulation flags are set by:

- `SetAsMuter` (0x5c3c54): articulation+0xf314.
- `SetAsChoke` (0x5c3c74): articulation+0xf315.
- `SetIsFake` (0x5c3c64): articulation+0xf316.

`SetNoteOn` stores an integer at+0xf308, `SetArtId` at+0xf310, and
`SetVolume` stores a float at+0xf30c. Two named setters,
`EnableEnvelopeModelling` and `SetAsHatClamp`, are no-ops in this shipped
build. Their names alone do not establish active features.

### Single-channel envelope

`CSingleEnvelope` stores a linear level at+0x00, the current per-sample slope
at+0x04, attack/release slopes at+0x08/+0x0c, remaining attack/sustain/release
sample counts at+0x14/+0x18/+0x1c and the stage at+0x20. Stage codes are
0 attack, 1 sustain, 2 normal release, 3 parametric release, 4 finished.

`Start` (0x5d0fb4) starts at unity in sustain, with 441,000 sustain samples
and 441,000 release samples: ten seconds each at the hard-coded 44,100 Hz.
The setter behavior differs by stage:

- `SetAttackSecs` (0x5d1000) truncates `seconds * 44100`. A nonzero duration
  is at least 32 samples and starts at zero with slope `1 / duration`. A
  truncated zero skips attack and starts at unity in sustain.
- `SetSusSecs` (0x5d107c) truncates the same conversion. Values below one
  sample become 32 samples; positive durations are otherwise retained.
- `SetRelSecs` (0x5d10a0) uses slope `-1 / duration` for a positive duration;
  otherwise it installs 32 samples with slope `-1 / 32`.
- `SetEnvLenForCrossFadedSamples` (0x5d10ec) starts at unity with no attack,
  assigns 10% of the requested duration to sustain and the remaining 90% to
  release, with the same release fallback. Its sustain conversion is not
  passed through the ordinary sustain setter.

`ReplySamplesTillNextStage` (0x5d117c) returns the applicable remaining count.
`RunEnvelope` (0x5d12b0) selects the current stage's slope, subtracts the
requested render segment from its count and advances or finishes the stage.
It does **not** itself advance the stored level: the voice renderer performs
that integration. Reimplementing this as an ordinary sample-by-sample ADSR
would obscure the original segment-boundary behavior.

### Normal release versus parametric choking

`TriggerRelease` (0x5d1274) clears remaining attack/sustain, switches to stage
2 and sets a release slope from the current level and requested duration.
`TriggerParametricRelease` (0x5d11b4) instead switches to stage 3 and derives
duration from velocity and the layer's per-mic release limits:

```text
requested_samples = (min_release + (127 - velocity) *
                    (max_release - min_release) / 127) * 44100
duration = max(1, requested_samples bounded to remaining envelope length)
counter = trunc(duration)
slope = -current_level / duration
```

The fractional duration is retained in the slope even though the counter is
an integer. If the previous stage was normal release, the current level is
squared before calculating the new slope. This is necessary because
`renderVoice` (0x5d6cd0) normally squares the start and end envelope levels
when all eight channel slopes are nonpositive, unless the voice's parametric
flag at+0x1ac is set. It then linearly interpolates between those transformed
endpoints. This differs from evaluating a squared envelope separately at
every sample, and the slope gate applies across the envelope group.

`CMultiVoiceItem::TriggerParametricRelease` (0x5d1b1c) sets the parametric
flag for each child voice and passes release limits from the incoming voice.
The level conversion above therefore preserves continuity when moving from
the ordinary squared release path to the parametric linear path.
`TriggerMuterRelease` (0x5d1a24) uses the ordinary release operation and, unless
forced, only processes children whose muter-eligibility byte at+0x1ad is set.
The normal and parametric operations must remain distinct.

The renderer also applies the pool factor at+0x8410 twice, together with
voice gain, the pool factor at+0x8420, and per-channel mute coefficients.
These gain stages are separate from mic mix gains and the selected sample's
`svol`. Exported raw PCM alone does not reproduce those levels.

### Other event processing

`processSnareHumanize` (0x55ea70) acts on logical zone 1. Repeated maximum
velocity equal to the preceding hit is reduced using a separate engine-level
fixed-random cursor, with a resulting integer bounded to 3..127. It also
registers hits and periodically resets a slot counter and installs an
interval derived from `trunc(random * 4 + 1)`. This engine random state is
separate from the instrument-level round-robin cursor. Full caller gating
and the audible consequences of its returned flag remain to be correlated.

`processCymballSwellModelling` (0x55ebc8) returns an attack override of 0.1
for logical zone 0x2b when the registered interval is below 0x2274; otherwise
it returns -1. Trigger preparation transforms a nonnegative override to
`override * 0.1 + 0.03` seconds, so this branch requests 40 ms of attack.
The counter's timebase and full enabling policy have not been validated.

`processCymbalChoke` (0x55ec20) checks the preceding voice's choke-related
flag, obtains cumulative RMS, and triggers a separate logical-zone 0x29
event at velocity 127. Thus choking can involve a recorded articulation in
addition to shortening existing voices. The complete choke target policy,
RMS scaling, voice stealing, per-voice EQ and hi-hat movement/transition
state still require original-render comparisons.

## Implemented selection primitives

`software/common/InstrumentSelection.h` and `.cpp` provide portable C++17
velocity lookup and alternative selection. They have no JUCE dependency and
allocate only while constructing an articulation, not while selecting a hit.
The exact 100-element float table is stored as hexadecimal floating-point
literals with original ELF hash/address provenance. One externally shared
`FixedRandomSequence` represents the instrument cursor; each
`InstrumentSelection` represents one articulation and its layer queues.

`software/tests/instrument_selection_test.cpp` checks velocity boundaries,
duplicate thresholds, the unusual two-alternative first hit, shared cursor
behavior, forced-first state advancement, fixed golden sequences, cursor
wraparound and anti-recent behavior for 3..16 alternatives. These tests
compare against recovered expectations and a separately emulated original
selection trace, not a live original sampler. Sample readiness,
gain/envelope rendering and hi-hat blending are outside this primitive's
scope. `scripts/emulate_mimic_selection.py` reproduces the isolated comparison
when supplied the original hash-matching application and a native trace
helper.

The trace helper's source is
`software/tests/instrument_selection_trace.cpp`. Build it as the
`instrument-selection-trace` test-support target, then reproduce the
comparison on the analysis host with optional `unicorn==2.1.4` installed:

```sh
python3 scripts/emulate_mimic_selection.py \
  --application /path/to/hash-matching/mimic_pro \
  --native-trace build/library-tests/instrument-selection-trace \
  --output build/library-research/selection-original-arm-comparison.json

python3 scripts/emulate_mimic_velocity_map.py \
  --application /path/to/hash-matching/mimic_pro \
  --native-trace build/library-tests/instrument-selection-trace \
  --metadata /path/to/copied-bin-metadata /path/to/copied-mci-prefixes \
  --output build/library-research/velocity-original-arm-comparison.json
```

The helper must be built for the machine running Python; an ARMv7 executable
cannot be launched directly by a macOS analysis process. The optional emulator
is a research-host dependency and is not shipped in the Mimic image.

`software/common/HatControl.h` and `.cpp` implement the recovered fixed
100-bin table and select a precomputed entry for normalized openness. They
accept ordered, already-resolved articulation indices, retain all four
behavior bytes, and allocate nothing during selection. The separate
`hat_control_test.cpp` compares every table entry against the ARM-emulated
fixture, including exact float gains, and tests short-zone clamping and input
bounds. Defining NaN controller input as fully closed is an explicit input
validation choice; the original function's NaN behavior is not established.
Pedal movement, transitions and tail muting remain separate responsibilities.

## Required parity checks

Before claiming functional equivalence, establish and test:

1. Every zone/articulation ID, including fake/muter/choke/transition entries.
2. Layer threshold and exact velocity-to-gain behavior at every boundary.
3. Alternative selection state, first-hit behavior, resets and repetition policy.
4. Per-hit multi-mic synchronization, mono/stereo channel order, gain and phase.
5. Hi-hat openness selection, crossfades, pedal/splash/clamp and transition tails.
6. Choke targets and release envelopes, including per-mic release times.
7. Polyphony limits, voice stealing, preload/streaming and missing-sample behavior.
8. Humanization, tuning/interpolation, sample/RMS normalization and mixer routing.
9. Factory and third-party library variants, malformed metadata and unsupported versions.

A catalog of symbols or a decoder that lists samples is insufficient to claim
100% feature preservation. These obligations require artifact-backed behavior
and comparison against the original sampler.
