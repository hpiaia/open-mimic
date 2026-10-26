# Application architecture

The instrument uses separate processes for control and time-sensitive work:

```text
Goodix touchscreen -> mimic -> local control API -> mimicd -> audio output
                           ^                         ^
                           |                         |
                     status snapshots          ADC / trigger input
```

`mimic` owns display rendering, touch interactions, navigation and user edits.
The JUCE application shows network and resource status, audio test and Aux
controls, and placeholders for calibration and trigger monitoring. Its
network polling runs on a worker thread. It does not open the ADC or audio
devices. Rendering uses JUCE on Xorg with the existing Pearl framebuffer; touchscreen
events come from the existing Goodix kernel driver through Xorg evdev.

`mimicd` will own acquisition, trigger inference, instrument state and audio.
It implements ADC probing/capture, offline DSP checks, manual analog output
tests through a custom JUCE AudioIODevice, and a continuous analog Aux
controller. Future continuous instrument acquisition and playback
paths need bounded buffers, explicit timing and priorities; simply running in
a background process does not guarantee real-time operation. Disk access,
networking, model loading and allocation should stay outside their hot paths.

The planned control API uses a Unix domain socket with versioned messages for
commands and status. The UI sends requests such as starting calibration or
selecting a kit. The daemon validates and applies them, then publishes status.
Waveforms and audio must not be transported through ordinary UI commands.
Current controls use atomically replaced files under `/run` for headphone
and Aux gains. Manual audio tests use a local datagram socket for cancellation.
The Aux controller starts independently through `S55mimic-audio`; the UI
launches finite tone tests on demand. The versioned instrument control API,
continuous PCM playback service, and inference engine remain to be implemented.

A UI crash should leave the daemon running. A daemon failure should produce
an explicit unavailable state in the UI. The UI must not claim that a
placeholder operation succeeded. Wi-Fi configuration and firmware installation
will eventually need dedicated control services rather than unrestricted
shell commands from widgets.

Both applications use C++17 and JUCE. Xorg, fonts and input handling are
userspace dependencies; kernel and device tree remain the validated original
versions. The migration decisions are recorded in [JUCE integration](juce-integration.md).
