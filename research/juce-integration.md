# JUCE integration decisions

The application standardizes on C++17 and pinned JUCE 9.0.4. The initial Rust
ADC and framebuffer prototypes supplied the hardware contracts and reference
captures; their functionality is migrated to the C++ applications.

## Framework use

| Need | JUCE facility | Implementation scope |
| --- | --- | --- |
| Build targets | Official CMake helpers, GUI/console app targets | Application build |
| UI | Component, Label, TextButton, LookAndFeel_V4, Grid | Home screen |
| Input and rendering | Linux X11 backend, software rendering | Xorg fbdev / evdev |
| Background status | Thread, ChildProcess, Timer | Bounded network queries |
| DSP | AudioBuffer, dsp::AudioBlock, dsp::Gain | Offline engine self-test |
| Instruments | Synthesiser, SynthesiserVoice, AudioFormatManager | Planned |
| Effect routing | AudioProcessor / AudioProcessorGraph | Planned; benchmark first |
| Processor parameters | AudioProcessorValueTreeState with ParameterLayout | Planned |
| Persistence | ValueTree and versioned application schema | Planned |
| MIDI | MidiMessage, MidiBuffer and device APIs | Planned |
| Plugin hosting | AudioPluginFormatManager and processor modules | Planned; ARM Linux binaries required |
| Pearl audio device | AudioIODevice / AudioIODeviceType adapter | Planned after McASP contract validation |
| Machine learning | Dedicated inference backend behind trigger interface | Runtime selection pending measurements |

JUCE is not itself an ML inference runtime. Host support does not make desktop
x86 plugins run on ARM. The kernel's Pearl device APIs still need explicit
adapters. Adding JUCE does not establish real-time safety or audio latency.

The audio path will preallocate storage during preparation and process bounded
blocks. Network calls, file access, blocking locks, allocation and GUI operations
must stay outside processing callbacks. UI state changes must cross the process
boundary as commands, then become callback-safe parameter updates. An APVTS
attachment cannot directly connect controls to a processor in another process.

The current daemon remains a probe/capture CLI with an offline DSP test. The
control service, sampler, effect graph, model runtime and physical audio output
are subsequent work. Placeholder buttons explicitly report this state.

## Sources reviewed

- [Features](https://juce.com/features/)
- [Pinned CMake API](https://github.com/juce-framework/JUCE/blob/9.0.4/docs/CMake%20API.md)
- [Pinned Linux dependencies](https://github.com/juce-framework/JUCE/blob/9.0.4/docs/Linux%20Dependencies.md)
- [GUI application example](https://github.com/juce-framework/JUCE/tree/9.0.4/examples/CMake/GuiApp)
- [AudioProcessorGraph](https://docs.juce.com/master/classjuce_1_1AudioProcessorGraph.html)
- [AudioProcessorValueTreeState](https://docs.juce.com/master/classjuce_1_1AudioProcessorValueTreeState.html)
- [AudioIODevice](https://docs.juce.com/master/classjuce_1_1AudioIODevice.html)
- [DSP processing](https://docs.juce.com/master/group__juce__dsp.html)

Current online API pages may describe a newer release; pinned headers and
examples are the implementation authority. JUCE modules use AGPLv3 or a
commercial licence. New application sources use AGPL-3.0-only; the kernel and
other third-party components keep their existing licences.

## Buildroot host-tool integration

JUCE bootstraps `juceaide` for the build host during cross compilation.
Buildroot exports a target pkg-config sysroot; it must not leak into that
native bootstrap. The package hook adds the missing sysroot reset beside
JUCE's existing host-environment resets and checks the expected source marker.
`PATH_ORIG` selects native host tools for that step. Application targets still
use the ARM toolchain and target dependencies. No JUCE runtime code is patched.

## Legacy input ABI

The Pearl ARM32 kernel emits 16-byte evdev records. Its 4.1 UAPI header embeds
libc `timeval`, which makes that structure 24 bytes with musl time64. The
[mimic-input-abi package](../platform/compat/input-abi/) backports the later
Linux input-event timestamp definition and provides an explicit dependency for
input libraries and Xorg. This is a userspace header compatibility fix; the
Pearl kernel and its drivers remain unchanged.
