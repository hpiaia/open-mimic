# MIDI input and output

The DIN ports use `/dev/ttyS9`, the AM57x UART at `0x4ae2b000`. The archived
Pearl application names this interface in `CMidiPortHW`; existing disassembly
establishes 31,250 baud. Open Mimic configures and verifies 31,250 baud, 8 data
bits, no parity, and one stop bit through Linux `TCGETS2`/`TCSETS2` with
`BOTHER`. It disables terminal echo, newline conversion, software flow control,
and hardware flow control.

The running Pearl kernel has `CONFIG_SND_SEQUENCER` disabled. JUCE's Linux
MIDI backend depends on that sequencer, so enabling JUCE ALSA alone would not
expose these ports. A small Linux transport handles the UART and ALSA raw MIDI
character devices while keeping the existing custom audio device unchanged.

Class-compliant USB MIDI peripherals attached to the Mimic's USB host port
can expose `/dev/snd/midiCxDy` through the archived `snd-usb-audio`,
`snd-usbmidi-lib`, and `snd-rawmidi` modules. Discovery runs once per second;
I/O uses nonblocking reads and writes. USB output queues disable ALSA's
automatic active-sensing byte on close. Each raw device currently represents
its first available subdevice; individually selectable cables on multi-port
USB interfaces require additional subdevice enumeration. No USB MIDI device
was connected during the initial investigation, so USB device compatibility
requires hardware validation.

## Service and application controls

`mimicd midi-service` owns the ports and command FIFO. The MIDI page selects
an input and an output independently, shows message counters and recent
events, and offers manual note tests and panic. There is no automatic MIDI
thru. Inputs currently feed the monitor; connecting them to a sampler and
mapping pad triggers to MIDI output are separate integration work.

```sh
mimicd midi-config --input din --output din --channel 0
mimicd midi-test --channel 10 --note 36 --velocity 96 --ms 250
mimicd midi-panic
```

The input channel filter accepts 0 for omni or 1–16. Test-note channels are
1–16. Every test schedules its own note-off; switching configuration or
stopping the service finishes pending test notes. Panic explicitly sends
All Notes Off and All Sound Off on all channels of the selected output.
Port identifier `none` disables a direction; USB identifiers resemble
`usb-C1D0`. Settings persist in `/etc/mimic-midi.conf`.

The daemon publishes `/run/mimic-midi/status.json` every 200 ms using atomic
replacement. It includes port capabilities, connection errors, selected
directions, accepted input-message and queued output-message counts, and the
last 12 displayed events. Events retain raw bytes, source port, direction,
sequence number, and a monotonic timestamp in microseconds. Clock and Active
Sensing count toward RX but do not flood the display. This status file is a
diagnostic snapshot, not a lossless real-time event transport for a sampler.

CLI clients submit bounded commands to `/run/mimic-midi/command` without
blocking on an absent service. The runtime directory and FIFO are restricted
to the service's root account. A lock prevents duplicate service instances.

## Parser behavior and tests

`software/common/MidiStream` decodes all MIDI 1.0 channel-message lengths,
running status, system-common messages, interleaved real-time messages, and
SysEx up to 1,024 bytes including framing. Oversized or interrupted SysEx is
discarded with a counter, and parsing recovers at the next valid status.
The parser allocates no memory and keeps each input's state separate.
`midi-stream-test` covers channel statuses, running-status lengths, clock
interleaving, system-common cancellation of running status, SysEx framing,
the exact size boundary, overflow recovery, and reset.

For a physical DIN loopback test, connect MIDI OUT to MIDI IN with a standard
MIDI cable, choose DIN for both directions, leave input filtering at Omni,
and press the manual note-test button. RX should show the same channel,
note, and velocity as TX, followed by the matching note-off. This checks both
UART directions and the board's MIDI electrical interfaces without requiring
a sound generator. A successful software queue write alone does not prove
that a connected instrument received the message.

## Boot and isolated service validation

`/etc/init.d/S56mimic-midi` loads the existing USB host MIDI modules through
`modprobe snd-usb-audio` and starts `mimicd midi-service`. Driver load failure
is reported while the independent DIN transport can still start. Service
startup diagnostics go to `/var/log/mimic-midi.log`.

`scripts/test_midi_service.py` exercises the actual daemon, CLI, FIFO, and
UART wire framing through a Linux pseudo-terminal. It deliberately requires
a fresh isolated Docker container without MIDI device passthrough. The
explicit `midi-service --din-device PATH` override supplies the PTY instead
of the physical UART; normal boot continues to use `/dev/ttyS9`.

The integration test covers 31250 baud setup, no unsolicited output,
running-status and real-time input, SysEx, channel filtering, oversized IPC
rejection, exact note-on/note-off bytes, note-off timing, output-switch release,
16-channel panic, invalid output values, and graceful shutdown. A pseudo-
terminal verifies software behavior; the physical DIN electrical circuit and
USB adapters still need hardware tests.

```sh
docker run --rm --platform linux/arm64 \
  --mount type=bind,src="$PWD",dst=/project -w /project \
  open-mimic:buildroot-host sh -c \
  'cmake -S software -B build/midi-tests -DMIMIC_BUILD_UI=OFF -DMIMIC_USE_JUCE=OFF &&
   cmake --build build/midi-tests --parallel 4 &&
   ctest --test-dir build/midi-tests --output-on-failure &&
   python3 scripts/test_midi_service.py --mimicd build/midi-tests/mimicd'
```

The frontend exposes transport diagnostics and manual tests. A future sampler
should consume parsed MIDI events through the audio engine's event queue,
and pad-generated notes should use the same output transport. Polling the
JSON status snapshot would lose events and add UI-scale latency.

## Live installation, 26 October 2026

The complete application and daemon were cross-compiled with the Buildroot
ARMv7 musl toolchain and installed on the running SD system. The graphical
application restarted successfully. The MIDI service reported DIN input and
output connected, no configuration error, and zero received/transmitted
messages after startup. `snd-usb-audio`, `snd-usbmidi-lib`, and `snd-rawmidi`
loaded successfully. These checks establish service startup and UART
configuration, not a physical MIDI round trip or USB-device compatibility.

The original SSD partitions remained mounted read-only. Previous application
and daemon binaries are retained on the SD system under
`/root/open-mimic-backup-before-midi-20261026`. The ignored local build log is
`build/midi-arm-build.log`; `build/midi-live-validation.txt` retains the
service snapshot and mount checks.

The owner subsequently confirmed physical MIDI output into Superior Drummer,
with the computer's audio returning correctly through AUX IN. This establishes
that external-instrument workflow by owner observation. Physical MIDI input
and USB MIDI adapters still need separate acceptance checks.

## Primary API references

- [Linux terminal attribute ioctls and arbitrary baud rates](https://man7.org/linux/man-pages/man2/TCSETS.2const.html).
- [Linux 4.1 ALSA raw MIDI UAPI](https://github.com/torvalds/linux/blob/v4.1/include/uapi/sound/asound.h).
- [ALSA raw MIDI hardware implementation](https://github.com/alsa-project/alsa-lib/blob/master/src/rawmidi/rawmidi_hw.c).
