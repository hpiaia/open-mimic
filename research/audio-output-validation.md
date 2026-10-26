# Analog output and touch validation

## Physical playback and routing

After the original board-enable GPIO sequence was added, the user confirmed
that analog playback works. Listening corrected the initial static routing
inference:

| Physical output | Left DMA frame index | Right DMA frame index |
| --- | --- | --- |
| Headphones | 1 | 3 |
| Master | 7 | 5 |

The former headphone route (5 left, 7 right) actually fed Master with its
sides reversed. Current code shares the corrected mapping through
`software/common/AudioRouting.h`. Headphone gain now affects only 1 and 3;
Master stays at unity gain. Headphone gain defaults to unity (0 dB).
The manual diagnostic sine now defaults to 0 dBFS at the user's request,
with lower levels available. Other physical line jack mappings remain open.

The tests below preserve the earlier investigation. Their references to the
old headphone candidate 5/7 describe the initial hypothesis, subsequently
corrected by physical listening.

## Static evidence

Original application SHA-256:
`b12895171c9cb8fd7db067c816ba28b4ae4817869a34654175f3e6dfb683fa8c`.
REA evidence `ev_b72ef892dcdab1414b60beb3c9697abe92fe2255c0eaad936b793d9ea39c24e2`
covers the DACTest constructor and sampler. The constructor initializes the
PLL using `/dev/regulators`, ioctl `0x5201`, value `0x7021`, and opens McASP.
Queries `0x4d01`–`0x4d04` return layout values directly.

Renderer assembly evidence
`ev_de6a96accfc3824a7cfad5fc6403b58ff4485f83e01727203e3aa44221142e82`
shows clipped floats multiplied by 8388608 and converted to signed integers.
The renderer has separate gains for frame positions 1/3 and 5/7. The original
interpretation assigned headphone gain to 5/7; listening showed those were
Master outputs. Physical listening takes precedence over that inference.

The PCM1690 register definitions are from the
[TI datasheet](https://www.ti.com/lit/ds/symlink/pcm1690.pdf).
Register 0x44 provides per-channel soft mute; 0x45 reports digital zero detection.

## Board-enable initialization recovered with Ghidra

REA using Ghidra 12.1.4 decompiled the original `CMainPane` constructor at
0x45a344, evidence
`ev_cbfed3113dfb7b6a5910bf856df2ce7b84f730d0669d57c433e71c2a41f86e69`.
The constructor calls `CExtPortControl::gpioFunc` for strings `6.16` and `5.3`.
GNU objdump confirms both boolean arguments are 1 at call sites 0x45bd2c and
0x45bd40; these map to Linux GPIO176 and GPIO131, configured output-high.
Commented commands in the original `start.sh` independently name the same
GPIOs and levels. Ghidra's pseudocode omits one argument at these sites, so
the boolean values are corroborated by the machine instructions.

These pins were unexported in the replacement OS. `PearlAudioDevice::open`
now reproduces the original sequence before starting its playback worker.
GPIO direction `high` sets direction and level together. A silent hardware
validation (both DACs temporarily soft-muted, headphone gain zero) confirmed
both read back `out` and `1`; cancellation and restoration completed normally.
Their exact board wiring, including whether either is a DAC AMUTEI signal or
an analog power enable, has not been established from a schematic.

The PCM1690 external active-low AMUTEI input can silence analog output while
digital zero detection continues to work. Thus changing ZERO flags did not
exclude an external analog mute. This is a hypothesis for the silent output,
not a confirmed electrical identification of either GPIO.

## Observed hardware tests

- Layout: 4 bytes/sample, 16 channels, 64 frames/block, 4096 bytes/write.
- A three-second raw writer took approximately three seconds to complete.
- Both DACs at I2C addresses 0x4c and 0x4e are bound to the kernel driver.
  Their registers report TDM format 0x06, no soft mute, and unity attenuation.
- A diagnostic test placed both DACs in soft mute before transmitting data,
  and restored the previous unmuted state after stopping the stream. No
  audible playback was intended during this validation.
- With a signal on all 16 raw channels, register 0x45 read 0x00 on both DACs.
- With only raw channels 5 and 7 selected, headphone gain zero produced zero
  detection 0xff/0xff. Changing gain to 0.25 during the same stream produced
  0xf3/0xff. Thus those two raw positions reach DAC 0x4c channels 3 and 4,
  and live volume changes reach its digital input. This does not establish
  which physical jack those DAC outputs feed.
- The cancellation command stopped the stream successfully and flushed silence.

## Software and interface checks

The offline test checks JUCE gain processing, invalid buffer rejection, sine
peak amplitude, isolation of unselected channels, and headphone mute after
its smoothing ramp. It opens no audio hardware.

The graphical selector covers the entire application window, including its
global header, with a full-screen list, 60-pixel rows and
Viewport drag scrolling for touch and emulated mouse sources. Movement beyond
8 pixels suppresses selection. Synthetic X11 input on the instrument verified
selection closes the list immediately and dragging scrolls without closing it.
The user confirmed the updated list was better; the full-screen revision
was also checked in a live framebuffer capture.

The global header includes headphone volume and mute. Synthetic button input
verified the control file changes between 0 and 0.25. The user confirmed wheel rotation and click work correctly. Wheel implementation
and its hardware contract are described in [wheel-input.md](wheel-input.md).

## Listening test

Playback always requires **Play test tone**. Select Headphones or Master,
then use the left-only and right-only options to verify the corrected sides.
The default test level is 0 dBFS; -40, -30 and -20 dBFS remain available.
The headphone control defaults to 0 dB, and its mute must leave Master audible.
Wheel rotation and click were confirmed working by the user.
