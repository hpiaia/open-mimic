# Front-panel wheel

The Pearl wheel is connected to GPIO inputs, rather than an evdev rotary
encoder device. The original application reads three legacy sysfs GPIO value
files.

## Original software evidence

REA/Hopper analysis of `mimic_pro`, SHA-256
`b12895171c9cb8fd7db067c816ba28b4ae4817869a34654175f3e6dfb683fa8c`:

- `CEncoderData::CEncoderData()` at `0x5a83b4` exports GPIO 190, 191 and 192,
  configures them as inputs, and opens their `value` files read-only.
- `CEncoderData::UpdateState()` at `0x5a81ec` detects a button press when
  GPIO 190 changes from high to low.
- A falling edge on GPIO 191 advances the encoder position by one when GPIO
  192 is high, or reduces it by one when GPIO 192 is low.

Evidence: `ev_cc22947fd48f98c7c955e4811e0390c0903f77ced6a9c7fa9320261c9ba36b08`.
These are static observations of the shipped application, not a runtime trace
of physical wheel movement.

## Open Mimic implementation

`WheelInput` samples the GPIO inputs on a dedicated worker with a nominal
1 ms interval. The worker accumulates position changes and debounces button
presses for 25 ms. A JUCE timer delivers accumulated rotation and click events
on the message thread at 50 Hz, independently of slower network monitoring.
This userspace polling approach can miss edges during scheduling delays or very
fast rotation; it is not a kernel interrupt-driven quadrature decoder.

The header exposes a headphone volume slider and mute button. Each encoder
step adjusts the displayed level by two percentage points; a click toggles
mute, preserving the previous nonzero level for unmute. Touch and wheel use
the same volume control.

The control file `/run/mimic-headphone-volume` contains a normalized linear
gain from `0.0` to `1.0`, with `0.0` representing mute and an initial default
of `1.0`. The UI replaces the file atomically when the value changes. The
file survives application restarts within a boot, but `/run` is volatile.

## Hardware validation boundary

On the running Buildroot system, GPIO 190–192 were successfully exported and
configured as inputs. All three read high while idle. No separate rotary
encoder appeared in `/proc/bus/input/devices`.

The user confirmed wheel rotation and click work correctly on the instrument.
Listening also established headphone DMA indices 1 (left) and 3 (right);
see [audio-output-validation.md](audio-output-validation.md).
