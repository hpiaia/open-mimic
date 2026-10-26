# Analog Aux input control

The Aux input is audible without a running PCM playback or capture stream.
Open Mimic controls its existing analog attenuation path through the vendor
regulator device, rather than introducing a second digital monitoring path.
The user confirmed that the level control and both independent/linked modes
work correctly on the instrument.

## Control semantics

The UI's **Aux input** panel has its own level and a **Follow headphone
volume** checkbox. The checkbox is off by default (independent).

- Independent: effective Aux gain = Aux gain.
- Linked: effective Aux gain = Aux gain × headphone gain.

Thus -6 dB Aux plus -10 dB headphones gives -16 dB when linked. Headphone
mute affects Aux only when linked. Aux at zero always mutes its own signal.
The default Aux gain is unity, and controls are restricted to attenuation.

The existing headphone volume adjusts only the digital headphone bus at DMA
indices 1/3. The analog Aux path is separate, so the service computes its
linked gain explicitly. Master output gain is not involved.

## Recovered interface

Original application `mimic_pro`, SHA-256
`b12895171c9cb8fd7db067c816ba28b4ae4817869a34654175f3e6dfb683fa8c`,
contains `CRegulator::SetAuxAttenuatorVol` at 0x55c19c. Static GNU objdump
inspection confirms three ioctl 0x5203 calls with payloads `[0, value]`,
`[1, value]`, `[2, value]`. Its old control-position mapping is
`saturate7((1 - position) × 127)`; that position is not linear signal gain.

REA through the Ghidra 12.1.4 MCP provider decompiled `regulators_ioctl` in
a derived kernel ELF slice. Evidence:
`ev_88a02a9f571c0c60f537835f54640f4a9edc5b786819da0e9016f58ba85c1097`.
The ioctl copies two bytes, lowers the latch, transmits each byte MSB first,
and raises the latch. DTS and disassembly identify GPIO217 latch, GPIO17
clock and GPIO16 data. This confirms the serial transport, not electrical
attenuation or the physical identity of the connected chip.

The three channel addresses and serial format strongly match an LM1973.
Its package marking has not been confirmed. The implementation uses the
[manufacturer's LM1973 datasheet](https://www.marutsu.co.jp/contents/shop/marutsu/ds/lm1973.pdf)
for the compatible attenuation curve:

| Code | Attenuation |
| --- | --- |
| 0–31 | code × 0.5 dB |
| 32–63 | code − 16 dB |
| 64–78 | code × 2 − 80 dB |
| 79 and above | Mute |

Desired combined gain is converted to dB and quantized to the nearest
supported step. Zero gain sends mute code127. Unity sends code0, matching
the original software's maximum level irrespective of the curve hypothesis.
All three addresses are updated, as in the original software; their exact
physical L/R/third-channel assignments remain unconfirmed.

## Implementation

- `software/common/AuxInputSettings.h`: independent/linked gain composition
  and attenuation conversion, without hardware access.
- `software/mimicd/src/AuxControl.cpp`: `mimicd aux-control` service.
- `software/mimic/src/AuxInputPanel.cpp`: touch UI.
- `platform/buildroot/overlay/etc/init.d/S55mimic-audio`: startup/stop script.

The UI atomically replaces `/run/mimic-aux-input` with two fields: linear
Aux gain0..1 and follow flag0/1. `/run/mimic-headphone-volume` contains the
headphone gain. The service polls at20ms and writes hardware only when the
quantized attenuation changes. Parsing errors retain the previous valid
settings. These runtime settings reset to defaults on reboot.

An exclusive lock prevents two Aux controllers. Stop restores signal handlers
and closes its descriptors, leaving the last analog level unchanged. Startup
enables the original GPIO176/GPIO131 board sequence, shared with the PCM
output adapter, so Aux does not require pressing Play after boot.

## Validation

Offline tests cover independent mute behavior, linked dB composition,
invalid headphone input isolation in independent mode, unity and mute codes,
piecewise range boundaries, and monotonic attenuation through the whole range.

On the instrument, UI events changed the control file and the active service
reported code127 with linked headphone mute, then code0 after unmute. With
independent Aux, headphone mute left code0 unchanged. The service accepted
ioctls, its duplicate-start protection was checked, and the UI was inspected
through a framebuffer capture. These are software/transport observations;
the user then confirmed the audible behavior of the controls. Exact electrical
attenuation and the physical chip identity remain inferred rather than measured.
