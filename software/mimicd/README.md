# mimicd

This C++ component of the Open Mimic application currently
probes the two Pearl trigger ADC character devices and captures diagnostic,
read-only DMA snapshots, and plays diagnostic tones through the Pearl analog
output device. Hit detection and model inference are not implemented.

The hardware interface is documented in
[`research/hardware-api-map.md`](../../research/hardware-api-map.md). The
snapshot format is intentionally raw because the driver's cursor semantics
and exact frame ordering still require validation against live captures.

## Usage on the Mimic

Build the Buildroot image and boot it, then over SSH:

```sh
mimicd probe
mimicd capture /tmp/tom1-head.omadc 5
```

The capture command creates a new file; it refuses to overwrite an existing
one. Buildroot does not currently include an SFTP server, so transfer a capture
to the development machine with SSH output redirection:

```sh
ssh root@192.168.31.215 'cat /tmp/tom1-head.omadc' > tom1-head.omadc
```

Keep captures outside the public repository because pad recordings may be
large.

Inspect a transferred capture with
`python3 scripts/inspect_adc_capture.py tom1-head.omadc`.
To extract complete frames for a known ADC and channels, use
`python3 scripts/reconstruct_adc_capture.py tom1-head.omadc tom1-head.csv --adc 1 --channels 11 12`.
The CSV contains frame indices and the upper-bound timestamp of the snapshot
that supplied each frame; it does not assign an exact hardware timestamp to
each sample.

Each little-endian `.omadc` file contains:

- 8-byte magic `OMADC002`;
- one `u32` requested polling interval in microseconds;
- for ADC0 and ADC1, three `u32` values: map size, DMA size, portion size;
- repeated records: elapsed nanoseconds (`u64`), cursor values before and
  after copying each ADC (`u32` each), then the full DMA ring bytes of ADC0
  and ADC1.

Snapshots are best effort. Cursor movement is recorded rather than causing a
record to be dropped. A snapshot can contain data from more than one ring
generation; neither gap-free capture nor sample timing is established.
The file preserves evidence for decoding the driver contract before a
real-time trigger engine is added.

Run capture-format tests through CMake/CTest as described in the application README.

`mimicd audio-self-test` runs an offline JUCE DSP gain check. It does not open
an audio device or play sound.

## Analog output debug

Open **Audio test** in the graphical application and press **Play test tone**.
Playback is manual, lasts three seconds, and defaults to 0 dBFS with short
fade-in and fade-out ramps. Test levels -40, -30 and -20 dBFS are also selectable.
**Stop** cancels playback. The global headphone slider defaults to unity
(0 dB) and applies gain only to the headphone channels, with a smoothing ramp.

Physical output mapping confirmed by listening:

| Output | Left raw channel | Right raw channel |
| --- | --- | --- |
| Headphones | 1 | 3 |
| Master | 7 | 5 |

The interface provides stereo, left-only, and right-only choices for both
outputs, plus individual raw channels 0–15 for mapping the remaining jacks.
Routing constants are shared by the interface and engine in `AudioRouting.h`.

The custom JUCE `AudioIODevice` writes signed 24-bit samples in 32-bit slots
to `/dev/mcasp.0`: 16 channels, 44.1 kHz, 64 frames per block. The audio worker
uses preallocated buffers and flushes silence when stopped. An exclusive lock
prevents two Open Mimic test processes from owning the device simultaneously.

```sh
mimicd audio-probe
mimicd audio-test --channels 1,3 --frequency 440 --seconds 3 --amplitude 1.0
mimicd audio-stop
```

`audio-self-test` also checks sine amplitude, channel isolation, and headphone
mute without opening hardware.

`audio-probe` queries the device without playing a tone. The test initializes
the vendor PLL through `/dev/regulators` and enables GPIO176/GPIO131 using the
original board sequence. Outputs outside the selected channel mask stay silent.
This is a diagnostic playback path, not yet the instrument playback service.

## Aux input

`mimicd aux-control` controls the original analog Aux attenuator in the
background. Buildroot starts it through `S55mimic-audio`. The interface's
**Aux input** panel provides a level slider and **Follow headphone volume**.
The latter is off by default: Aux is independent of headphone gain and mute.
When enabled, both gains apply in sequence. Runtime controls reset on reboot.

See [Aux input research](../../research/aux-input.md) for the hardware protocol,
attenuation curve assumption, and validation limits.
