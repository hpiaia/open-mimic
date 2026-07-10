# Mimic Pro Hardware API Map

Date: 2026-07-08

This note tracks the userspace hardware interfaces recovered from the Pearl Mimic Pro 1.4.18 application. Evidence is from symbols, strings, and targeted disassembly of the extracted ARM ELF unless otherwise marked.

The main conclusion is that the application is not talking only to stock Linux APIs. Trigger ADC, aux ADC/audio DMA, LCD configuration, and input regulator control use custom character-device APIs that Open Mimic will need to probe on real hardware or reimplement at the kernel/compatibility layer.

## Compute Module And Storage

The board photos show a removable Hoodisk `HDSESB-128GB` mSATA SSD and a green 204-pin SO-DIMM compute module. A user forum comment points to Compulab's CL-SOM-AM57x module family, and the visible module shape/features are consistent with that direction.

Practical implications:

- Image the mSATA SSD before probing unknown headers. The filesystem should reveal bootloader/kernel configuration, init scripts, and whether SSH/telnet/getty can be enabled cleanly.
- Use a real mSATA-to-USB/SATA adapter. M.2 SATA and NVMe adapters are different.
- Keep the first image read-only and make all experiments on a cloned image or replacement SSD.
- If UART is still needed, consult the CL-SOM-AM57x reference guide and Pearl carrier-board routing. Do not connect random pads directly; the module uses 3.3 V digital I/O, and the carrier board may route multiple UARTs.

Source-confirmed Compulab/TI facts are tracked in [compulab-cl-som-am57x-notes.md](./compulab-cl-som-am57x-notes.md).

## Device Nodes

Observed paths:

| Path | Probable role | Evidence |
| --- | --- | --- |
| `/dev/ads7953.0` | trigger ADC device 0 | `CADC` wrapper and ADS7953 strings |
| `/dev/ads7953.1` | trigger ADC device 1 | `CADC` wrapper and ADS7953 strings |
| `/dev/mcasp.0` | main McASP/audio DMA path, exact wrapper still unknown | device string |
| `/dev/mcasp.1` | aux ADC/audio capture DMA path | `CAuxAdc` wrapper |
| `/dev/lcd-conf` | LCD brightness/contrast control | `CLcdReg` |
| `/dev/regulators` | analog front-end regulator/gain/attenuator control | `CRegulator` |
| `/dev/ttyS9` | MIDI UART | `CMidiPortHW` sets 31,250 baud |
| `/sys/class/gpio/...` | legacy sysfs GPIO control | `gpio_*`, `fn_gpio`, EXT-port test/control |

Primary-source comparison is in [hardware-primary-source-notes.md](./hardware-primary-source-notes.md). The important implication is that upstream ADS7953 normally appears through Linux IIO and upstream McASP normally appears through ALSA/ASoC, so `/dev/ads7953.N` and `/dev/mcasp.N` are likely Pearl/vendor compatibility APIs.

## Trigger ADC: `CADC`

Symbols:

- `CADC::adcOpen(char const*)`
- `CADC::adcClose()`
- `CADC::GetOffset()`
- `CADC::GetBuffer()`
- `CADC::GetBufferSize()`
- `CADC::GetSample(unsigned char*, int)`

Open path:

- caller supplies the path, observed strings include `/dev/ads7953.0` and `/dev/ads7953.1`
- open flags: `0x80000`

Ioctls:

| Request | Stored field | Observed error text | Current interpretation |
| ---: | --- | --- | --- |
| `0x4101` | `this + 0x04` | `Unknown map size` | mmap length |
| `0x4102` | `this + 0x08` | `Unknown DMA size` | offset used by `GetOffset`, also returned by `GetBufferSize` |
| `0x4103` | `this + 0x0c` | `Unknown DMA portion size` | DMA portion size or related metadata |

Mapping:

```text
mmap(NULL, *(this + 0x04), PROT_READ, MAP_SHARED, fd, 0)
mapped pointer stored at this + 0x10
```

Read helpers:

- `GetBuffer()` returns the mapped pointer.
- `GetBufferSize()` returns `*(this + 0x08)`.
- `GetOffset()` returns `*(uint32_t *)(mapped + *(this + 0x08))`.
- `GetSample(buf, offset)` returns a 12-bit sample:

```c
sample = buf[offset] | ((buf[offset + 1] & 0x0f) << 8);
```

Open Mimic implication: a compatibility driver or shim probably needs to preserve these ioctl numbers and the mmap layout before the original `mimic_pro` binary can be rehosted against a replacement kernel.

## Aux ADC: `CAuxAdc`

Symbols:

- `CAuxAdc::adcOpen()`
- `CAuxAdc::adcClose()`
- `CAuxAdc::GetOffset()`
- `CAuxAdc::GetBuffer()`
- `CAuxAdc::GetBufferSize()`

Open path:

- fixed path: `/dev/mcasp.1`
- open flags: `0x80000`

Ioctls:

| Request | Stored field | Observed error text |
| ---: | --- | --- |
| `0x4d01` | `this + 0x00` | `AUX ADC - Unknown sample size` |
| `0x4d05` | `this + 0x04` | `AUX ADC - Unknown map size` |
| `0x4d06` | `this + 0x0c` | `AUX ADC - Unknown DMA size` |
| `0x4d04` | `this + 0x08` | `AUX ADC - Unknown DMA portion size` |

Mapping:

```text
mmap(NULL, *(this + 0x04), PROT_READ, MAP_SHARED, fd, 0)
mapped pointer stored at this + 0x14
fd stored at this + 0x18
```

Read helpers:

- `GetBuffer()` returns the mapped pointer.
- `GetBufferSize()` returns `*(this + 0x0c)`.
- `GetOffset()` returns `*(uint32_t *)(mapped + *(this + 0x0c))`.

The naming is odd: the method called `GetBufferSize()` returns the field populated by the "DMA size" ioctl, while the mmap length comes from the "map size" ioctl. Preserve the observed behavior until live captures confirm the driver contract.

## LCD Config: `CLcdReg`

Symbols:

- `CLcdReg::CLcdReg()`
- `CLcdReg::~CLcdReg()`
- `CLcdReg::SetBrt(int)`
- `CLcdReg::SetContr(int)`

Open path:

- `/dev/lcd-conf`
- open flags: `2` (`O_RDWR`)

Defaults set by constructor:

- brightness: `75`
- contrast: `27`

Ioctls:

| Function | Request | Argument |
| --- | ---: | --- |
| `SetBrt(int)` | `0x40044c02` | brightness integer |
| `SetContr(int)` | `0x40044c01` | contrast integer |

Error strings:

- `lcd brightness/contrast driver open error`
- `brightness config error`
- `contrast config error`

Open Mimic implication: this can probably be replicated by a tiny compatibility driver or a userspace daemon if the real LCD controller path is identified from device tree and kernel logs.

## Regulator/Gain Control: `CRegulator`

Symbols:

- `CRegulator::CRegulator()`
- `CRegulator::init()`
- `CRegulator::SetRegState(int, char)`
- `CRegulator::SetInpState(int, ePadInput_impedance, ePadInput_amp, ePadInput_dc_decouple)`
- `CRegulator::SetAuxAttenuatorVol(float)`
- `CRegulator::WriteState()`
- helper: `set_level(int, int, int)`

Open path:

- `/dev/regulators`
- open flags: `2` (`O_RDWR`)
- global fd: `CRegulator::regHandler`
- global state buffer: `CRegulator::m_state`, 32 bytes

Aux attenuator ioctl:

```text
request = 0x5203
payload = two bytes: [channel, value]
channels written by SetAuxAttenuatorVol: 0, 1, 2
value = saturate_to_7_bits((1.0 - volume) * 127.0)
```

Input-to-state mapping:

`convertInp2Reg`, `SetRegState`, and `SetInpState` copy a 128-byte table from `.rodata` and treat it as 32 little-endian integers. The table maps logical input `n` to state byte offset `31 - n`:

```text
0:31  1:30  2:29  3:28  4:27  5:26  6:25  7:24
8:23  9:22 10:21 11:20 12:19 13:18 14:17 15:16
16:15 17:14 18:13 19:12 20:11 21:10 22:9  23:8
24:7  25:6  26:5  27:4  28:3  29:2  30:1  31:0
```

Before lookup, logical inputs `4` and `5` are swapped:

```text
4 -> 5
5 -> 4
others unchanged
```

The byte write then lands at:

```text
CRegulator::m_state[8 + mapped_offset] = state_byte
```

`SetInpState` computes `state_byte` from impedance, amp, and DC-decouple enums. The recovered pattern shows bitfields rather than a simple linear value:

- impedance families choose base masks such as `0x00`, `0x80`, `0x40`, `0xc0`, `0x20`, `0xa0`, `0x60`, `0xe0`
- amp choices add/select low-bit patterns including `0x00`, `0x08`, `0x04`, `0x0c`, `0x02`, `0x0a`, `0x06`, `0x0e`
- DC decouple can set bit `0x01`

`WriteState()` transforms the 32-byte `m_state` buffer using bit rearrangement/vectorized byte operations, then writes 32 bytes to `/dev/regulators`. This is a critical interface for pad gain/impedance behavior.

Open questions:

- exact enum labels for every impedance/amp value
- electrical meaning of each bit in the 32-byte state
- whether `/dev/regulators` is a GPIO/SPI/I2C-backed latch, codec control, or custom board driver

## MIDI UART: `CMidiPortHW`

Symbols:

- `CMidiPortHW::CMidiPortHW()`
- `CMidiPortHW::~CMidiPortHW()`
- `CMidiPortHW::setup_port(int)`
- `CMidiPortHW::write_data(unsigned char*, int)`
- `CMidiPortHW::read_data_array(unsigned char*, int)`
- `CMidiPortHW::read_data(unsigned char&)`

Open path:

- `/dev/ttyS9`
- open flags: `0x102`

Termios2 ioctls:

| Request | Role |
| ---: | --- |
| `0x802c542a` | `TCGETS2`-style read |
| `0x402c542b` | `TCSETS2`-style write |

Constructor speed:

- `0x7a12` decimal `31250`, the standard MIDI baud rate
- speed is written to the termios2 input and output speed fields

I/O:

- `write_data(buf, len)` calls `write(fd, buf, len)` and returns `0`
- `read_data_array(buf, len)` calls `read(fd, buf, len)`
- `read_data(byte&)` reads one byte

Higher-level MIDI symbols:

- `CMidiInOut`
- `MidiReceiveThread`
- `MidiSendThread`
- `moodycamel::ReaderWriterQueue<CEDMidiCmd, 512>`

The receive path recognizes MIDI status bytes including note-on `0x90`, control-change `0xb0`, and program-change `0xc0`.

## GPIO Helpers

Two GPIO helper families exist:

- low-level integer functions: `_gpio_open`, `_gpio_direction`, `_gpio_set_value`, `_gpio_get_value`, `_gpio_mode_out`, `_gpio_mode_in`, `_gpio_high`, `_gpio_low`
- string/bank helper: `fn_gpio(char const*, bool, bool)`, wrapped by `CExtPortControl::gpioFunc`

Integer GPIO paths:

```text
/sys/class/gpio/export
/sys/class/gpio/gpio%d/direction
/sys/class/gpio/gpio%d/value
```

Integer helpers:

- export writes decimal GPIO number to `/sys/class/gpio/export`
- direction writes `out` or `in`
- value writes one byte, `1` or `0`
- reads return the raw value byte

Observed concrete GPIO sysfs paths:

- `/sys/class/gpio/gpio190/direction`
- `/sys/class/gpio/gpio191/direction`
- `/sys/class/gpio/gpio192/direction`
- `/sys/class/gpio/gpio190/value`
- `/sys/class/gpio/gpio191/value`
- `/sys/class/gpio/gpio192/value`
- `/sys/class/gpio/gpio197/value`
- `/sys/class/gpio/gpio93/value`
- `/sys/class/gpio/gpio29/value`
- `/sys/class/gpio/gpio215/value`

`fn_gpio` parses strings of the form `bank.index`, using:

```text
linux_gpio = index + ((bank - 1) * 32)
```

Examples from EXT-port tests/control:

| String | Computed GPIO |
| --- | ---: |
| `1.26` | 26 |
| `1.27` | 27 |
| `6.10` | 170 |
| `7.7` | 199 |
| `7.9` | 201 |
| `7.10` | 202 |

`fn_gpio(pin, output, high)` behavior:

- exports the computed GPIO
- if `output` is true, sets direction `out`, writes `1` when `high` is true and `0` when `high` is false, sleeps 10 ms, and returns `0`
- if `output` is false, sets direction `in`, reads the value, and returns `0` for char `0` and `1` otherwise

`CExtPortControl::Enable_12volt(bool)` uses GPIO string `6.10`, sets it as output, and writes an inverted value:

```text
Enable_12volt(true)  -> GPIO 170 value '0'
Enable_12volt(false) -> GPIO 170 value '1'
```

That suggests the 12 V enable line is active-low, but this needs electrical confirmation.

## Safe Live-Device Capture

Use the non-destructive capture script before probing any ioctl behavior:

```sh
sh capture_mimic_state.sh /mnt/usb-flash/open-mimic-capture
```

Highest-value outputs:

- `/dev` major/minor numbers for custom nodes
- `/proc/devices`
- `dmesg` probe lines for ADC, McASP, LCD, regulators, GPIO, serial, and storage
- `/proc/modules`
- `/proc/device-tree`
- `/proc/config.gz` if present
- `/proc/asound`
- `/sys/bus/spi/devices`
- `/sys/bus/iio/devices`
- `/sys/class/gpio`
- `/sys/class/tty`

Do not run destructive strings found in the firmware, especially the embedded `parted`, `mkdosfs`, and `mkfs.ext4` commands used by internal format/update flows.

## Open Mimic Hardware Strategy

1. Preserve the original kernel, modules, device tree, and root filesystem before changing anything.
2. Capture device-node major/minor numbers and driver names from the running OS.
3. Build tiny read-only probes for the custom ioctls only after the capture confirms the nodes and the original app is stopped.
4. Decide whether Open Mimic should implement native Linux APIs directly, or first provide a compatibility layer that lets the original `mimic_pro` app run against new lower layers.
5. Reimplement trigger ADC, regulator/gain control, MIDI, audio, and LCD as separate modules with test fixtures based on captured device data.
