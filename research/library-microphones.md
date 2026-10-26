# Instrument microphones and mixing

This note links the decoded library fields to consumers in the shipped ARM
application. It covers all 873 microphone definitions in the 373 inspected
instruments. It describes observed behavior, not a claim of complete playback
equivalence. Container and inventory details are in
[instrument-library-files.md](instrument-library-files.md).

The analyzed `mimic_pro` SHA-256 is
`b12895171c9cb8fd7db067c816ba28b4ae4817869a34654175f3e6dfb683fa8c`.
Function addresses below refer to that executable. REA/Ghidra exports and
per-instrument JSON remain in the ignored `build/library-research` directory;
neither proprietary samples nor decompiled source are included here.

## Field layout and defaults

`CInstrumentMicInfo::loadStateInformation` at `0x5c4dfc` loads a record with a
`0x150` byte stride. Offsets below are relative to that record, not file offsets.
Names in the first column are suffixes of keys such as `INST0micinf0micv`.

| Key | Offset | Evidence and observed values |
| --- | ---: | --- |
| `mict` | `0x000` | Integer routing class: 0 for observed direct mics; 1 for overhead/room. |
| `isst` | `0x004` | Stereo flag: one PCM channel when false, two consecutive channels when true. |
| `micn` | `0x008` | Name string; native loader strips trailing spaces. |
| `micv` | `0x10c` | Linear microphone gain; observed 0.2512–1.0. |
| `micp` | `0x110` | Linear pan/balance; observed 0.298–0.728; 763 definitions use 0.5. |
| `mictn` | `0x114` | Loaded float, always 0.5 in this corpus. Tuning is a plausible label; an active consumer has not been established. |
| `micpz` | `0x118` | Loaded float, always 1.0. Preserve verbatim; pitch/phase semantics are not established by the available consumer trace. |
| `micrs` | `0x11c` | Reverb send, confirmed by named getter and mixer consumer; always 0.0 in the corpus. |
| `micen` | `0x120` | Loaded Boolean, always true. Exact runtime gating requires a separate consumer trace. |
| `enven` | `0x121` | Loaded Boolean, always true. Do not equate it with every global envelope-enable switch. |
| `micm` | — | Present and false in all metadata, but this mic loader does not read the key. Runtime mute exists separately at record offset `0x122`. |
| `micpos` | `0x124` | Logical microphone role used by damping; not a channel offset. |
| `att`, `sus`, `rel`, `dmp` | `0x128`–`0x134` | First envelope bank: all observed values are 0, 1, 1, 0. |
| `att2`, `sus2`, `rel2`, `dmp2` | `0x138`–`0x144` | Second envelope bank: all observed values are 0, 1, 1, 0. |

The native loader falls back to the corresponding first-bank value if a `*2`
key is absent. The suffix `2` does **not** denote the right channel of a stereo
microphone. Both stereo channels receive the same selected envelope bank.

The 495 overhead/room mic definitions have gain 0.55. Other frequent gains are
0.631 for 267 definitions and 1.0 for 24. These are stored linear coefficients;
applying another decibel-to-linear conversion would change the mix.

## Channel order and alignment

Microphone definitions are traversed in their stored order. Each mono mic
consumes one PCM channel and each stereo mic consumes two consecutive channels.
This expansion equals `nchn` for every inspected instrument. Current instruments
contain one to four microphone definitions and one to six PCM channels, although
the native loops have capacity beyond the observed maximum.

`CInstrumentSlot::prepareVoiceItemForTrigger` at `0x5cf7a0` selects one sample
item for the complete microphone group. It expands per-mic layer values such as
`minrtl`/`maxrtl` to both channels of a stereo mic. Sample choice, frame cursor,
and pitch advance belong to the multichannel voice, rather than independent
random choices or clocks for each microphone.

A replacement sampler must preserve that common sample choice and frame
alignment. Independent round robin, trimming, resampling clocks, or start times
per mic would alter the relationships captured in the recordings.

## Gain, pan, and sends

`CMixerStripDescriptor::GetMicVol` (`0x55967c`), `GetMicPan` (`0x5596d8`),
and `GetMicReverbSend` (`0x559720`) resolve the field offsets independently of
their abbreviated storage names. The mixer descriptor embeds these mic records
at its own offset `0x1c` with the same `0x150` stride.

`CMixerEngine::buildMixNodes_phones_and_master` at `0x54d6a8` applies the
following coefficients to class-0 direct microphones. `g` includes the strip,
bus, and other runtime factors; `v` and `p` are the mic gain and pan:

```text
mono:   outL += sample * g * v * (1 - p)
        outR += sample * g * v * p

stereo: outL += sampleL * g * v * (1 - p)
        outR += sampleR * g * v * p
```

There is no equal-power pan curve in this consumer. At center, each applicable
side gets coefficient 0.5 before the remaining runtime gains. Stereo balance
does not cross-mix the recorded left and right channels. Other mixer stages
can supply additional gain, so this is not a claim that the complete output
path attenuates every centered instrument by 6 dB.

`buildMixNodes_reverbSend` at `0x55349c` multiplies `micv * micrs` and the
applicable strip/bus factors. Both channels of a stereo mic receive the same
send coefficient. This send matrix does not apply the direct-output mic pan
law. All library defaults have `micrs = 0`, but runtime kit/mixer settings may
change the send; retaining only library defaults would discard that behavior.

The direct phones/master function explicitly excludes class-1 microphones.
`buildMixNodes_oh_room` at `0x552c78` finds overhead and room by `micpos` 5
and 6 and creates separate bus nodes. Each channel of those stereo pairs gets
the same `strip factors * micv` coefficient, without the direct path's `p` or
`1 - p` factors. It also loads separate overhead/room strip gains for the
main, auxiliary, and individual-output paths. Treating every stored channel
as a direct channel would therefore change both gain and bus organization.

`checkForDataUpdates`' main body at `0x553bec` retrieves global overhead,
room, and reverb destinations before rebuilding those nodes, the direct
phones/master nodes, and individual-output nodes. These runtime destinations
are additional state needed beyond the instrument file.

## Envelope and damping behavior

The trigger preparation consumer chooses the second envelope bank for zone
codes 2, 4, and 42, and the first bank for other zone codes. This classification
comes from the native branch, not from guessing at names of recorded files.
Its relationship to every UI articulation name is a separate mapping.

For the chosen bank, the following equations describe the normal per-mic
path. Attack is passed to `SetAttackSecs` directly. Sustain and release are
times passed to `SetSusSecs` and `SetRelSecs`, not an ADSR sustain amplitude:

```text
q = max(0.01, (1 - damping)^3)
attack_seconds  = attack
sustain_seconds = sustain * 10 * sustain_factor * q
release_seconds = release * 10 * release_factor * q
```

For damping up to 0.3 both factors are 1. Above 0.3, let `t = damping - 0.3`:

| `micpos` | Observed role | Sustain factor | Release factor |
| ---: | --- | --- | --- |
| 5 | Overhead | `1 + t / 3` | `1 + t` |
| 6 | Room | `1 + t` | `1 + 8 * t / 3` |
| Other | Direct/bottom/etc. | 1 | 1 |

Global runtime flags can make all mics use mic 0's envelope values or disable
the envelope path. In the disabled path, attack becomes 0 and sustain/release
become 3,600 seconds. Those flags have not been fully mapped to saved setting
keys here. The damping role adjustment still uses the current mic's role when
the shared mic-0 parameter bank is selected.

An external attack override, when nonnegative, calls `SetAttackAllEnvelopes`
with `override * 0.1 + 0.03` seconds. Each stereo pair receives identical
envelope settings. The exact envelope curve and release/muter interactions
also depend on `CSingleEnvelope`/`CVoiceEnvelope` and voice-rendering state.

## Mute, tuning, and unresolved semantics

`CInstrumentSlot::UpdateMicMuteVolumes` at `0x5cefa4` reads the runtime byte at
mic offset `0x122` and writes a channel gain of either 0 or 1 through
`CVoiceItemPool::SetChanVol`. It applies the same state to both channels of a
stereo mic. This proves channel gating, but not that the unused `micm` metadata
key initializes it.

`CVoiceItemPool::renderVoice` at `0x5d6cd0` combines two voice tuning controls.
For finite ordinary values, a combined control strictly between 0.49 and 0.51
selects unity playback without interpolation; otherwise the observed ratio is
`max(0.5, 1 + 0.4 * (control - 0.5))`. The frame advance is shared by all
channels of the voice. The normalized strip tuning setter at `0x559780` writes
separate strip controls, not the mic record's `mictn` field. Connecting the
stored mic tuning/pitch-looking fields to this voice control remains open.

Neither a phase-inversion interpretation of `micpz` nor a per-mic independent
pitch implementation is justified by these observations. Preserve all loaded
fields and unknown keys, keep signed audio and stereo order intact, and avoid
inventing additional phase correction. Playback parity still requires tracing
overhead/room routing, kit overrides, effect processing, and comparison with
the original engine's output.

## Reproducible evidence

The local analysis exports are `rea-library-batch2.json` (loaders and pools),
`rea-library-batch6.json` (voice renderer and envelopes),
`rea-library-batch7.json` (mute and mixer helpers), and
`rea-library-batch8.json` (direct mixer and reverb send), and
`rea-library-batch10.json` (overhead/room routing and updates), under
`build/library-research`. `microphone-field-statistics.json` contains the
frequency counts across the complete inspected metadata corpus.

REA evidence IDs for batches 6–8 are respectively
`ev_771d55b6b404931fd29956911ec1b3ef2e8040b2f995d5f02c5ad5b29da8985d`,
`ev_094fdcf990bb27d09a4a26b139c01a1f5971fd871230a58ade617aab0bc1cc53`,
and `ev_fade52e2e0c5e019e1234466d0172b824564ddbfa53cf2c0a3ea59029f4873f9`.
Batch 10 adds
`ev_a5a8d3f42986a991097be943685a734793f0c863c1a3614ffeb47d3bc8f416d4`.
