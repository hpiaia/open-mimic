# Trigger input on Mimic Pro 1.4.18

Static analysis of the original `sda2` executable (SHA-256
`b12895171c9cb8fd7db067c816ba28b4ae4817869a34654175f3e6dfb683fa8c`),
the Pearl `am5718-edrums-v3.dtb` on `sda1`, and ADC documentation.
Hopper/REA pseudocode is not source code and can reconstruct expressions
incorrectly. Calls and constants below are the stronger evidence. The
complete evidence bundle is in
`../../open-mimic-artifacts/original-ssd-2026-10-25/analysis/rea-trigger-input-evidence.json`;
the corresponding REA snapshot is `rea-trigger-input-snapshot.json` in the
same directory.

## Observed path

```text
Pads -> input conditioning -> two ADS7953 ADCs (SPI3/SPI4)
     -> Pearl drivers /dev/ads7953.0 and .1 -> mapped DMA
     -> DACTest::collectAdcData -> CEdrumsApp::AudioRenderCallback
     -> CEDrumEngine2::Render -> CTriggerEngine2::ProcessInputData
     -> crosstalk + pad-type processor + velocity/zone
     -> event queue -> sampler and MIDI
```

The DTB declares two active `ti,ads7953` controllers on McSPI3 and McSPI4,
with `spi-frequency = 3000000`, `dma-size = 4096`, `portions = 128`, and
`ads7953,range-5v = 1`. The ADS7953 has 16 analog inputs at 12-bit
resolution, so there are up to 32 physical ADC channels across both chips.
This does not imply 32 jacks or establish a channel-to-pad mapping.
[TI specifications](https://www.ti.com/product/ADS7953).

`ComputingThread::ComputingThread` constructs `DACTest`, and
`ComputingThread::run` calls `DACTest::run_sampeler_new` after setting
real-time scheduling. Despite its name, `DACTest` is therefore part of the
normal audio/trigger path (`ev_a50eaba7a722adf69c689c5805de972a62db1d1acd0dc33f6280a015ab71ff6f`,
`ev_0d88eb577e4c19081c3690946941fb31aa6e9a1e1318e2e156e492174d398cb2`).
Its constructor opens `/dev/mcasp.0` and two `CADC` instances
(`ev_ef38ea66997e2423c75ae9b914e5105d08ab5f950af7dabcc6f13d866916c0ad`).

`CADC::adcOpen` uses `open(path, 0x80000)` and `ioctl` requests `0x4101`,
`0x4102`, and `0x4103` to obtain mapping, DMA, and portion sizes. It then
calls `mmap` with `PROT_READ | MAP_PRIVATE`
(`ev_4be8253a62f32edf03e3d241c8225bd981fbefe610e37959a204ce2c3a54c8d3`).
`CADC::GetOffset` reads the published cursor after the DMA buffer, while
`GetSample` combines two bytes and masks the low 12 bits
(`ev_f7b671ea81e14cd7a87ba2dd8b6e3acb4674533e05316e1d0350662c71581f36`).
The `collectAdcData` loop follows both ADC circular offsets, handles ring
wraparound, and arranges samples before the callback. Its division by
`0x20` suggests 32-byte frames, consistent with 16 two-byte samples per
ADC. This layout still needs validation with a real capture
(`ev_a84e471cb7413affa6caf3c47b5999332424455f4a89ee1c013a4688f49660d4`).

`DACTest::run_sampeler_new` calls `collectAdcData`, masks values to 12 bits,
and passes the block to `CEdrumsApp::AudioRenderCallback`
(`ev_f1905c32882cb864d2dedd9168c41208387585c7576e3f7cebcc38ff8d40718c`).
That callback calls `CEDrumEngine2::Render` directly
(`ev_f4d619d32715ab6f374d6b80b482a8dcfb2424d21ddda34911703bbe15c0a9aa`).
`Render` calls `CTriggerEngine2::ProcessInputData` and the optional trigger
data capture routine, as well as the sampler/mixer
(`ev_cc50d3fbd182adc41adf2308cdc55a85c3451a9e0b56b9e954c4100c8f294de7`).
`ProcessInputData` calls `CXtalkManager::ProcessAdcData`, dispatches to each
pad processor through virtual calls, computes velocity/zone, and orders
events (`ev_15221ec3e57b4d8a35281680c4f4b960b935f824a0169ff6317a96c504a8f5fb`).
The indirect dispatch means the direct-call list alone cannot identify the
algorithm used by each pad model.

The application also configures input gain, impedance, and decoupling via
`/dev/regulators`. The ioctls and state layout are in
[hardware-api-map.md](./hardware-api-map.md). This physical calibration
should be preserved in an initial prototype.

## Incremental reconstruction

An initial prototype can reuse the original kernel, DTB, and drivers from
SD while replacing only userspace. Separate the work into:

1. Read-only ADC reader: open both nodes, query the layout through three
   ioctls, and observe offsets and samples without writing to the SSD.
2. Channel map: strike one pad at a time and record channel, zone,
   amplitude, waveform, and timing. Repeat for hi-hat, ride, cymbals, and
   pads from different makers.
3. Independent trigger engine: threshold/noise, time windows, zones,
   crosstalk, velocity, and MIDI events. Compare against captures exported
   from the original firmware.
4. Audio and graphical interface after validating the input path. A complete
   replacement also requires reconstructing preset and instrument formats,
   sample streaming, mixer, latency, UI, and updates.

## Open hardware questions

- A photo of the SD shell confirmed four character devices:
  `/dev/ads7953.0` (`10,61`), `/dev/ads7953.1` (`10,60`),
  `/dev/mcasp.0` (`10,63`), and `/dev/regulators` (`10,59`). The
  `/mnt/user/trig_data` directory was empty at that time.
- Measure offsets, actual cadence, and channel order with the original
  application stopped; avoid competing reads during normal boot.
- Find the channel and configuration of each jack/zone, including the
  continuous hi-hat input.
- Obtain real files from `/mnt/user/trig_data` and presets from
  `/mnt/settings` to correlate samples with calibration parameters.
- Validate every ioctl and McASP output buffer before any permanent
  replacement.

## First physical channel test (2026-10-25)

The owner ran `ADCPROBE 10` twice in the SD shell, striking tom 1 and then
tom 2. The terminal photo shows:

| Pad | ADC1 channel | Minimum | Maximum | Range |
| --- | ---: | ---: | ---: | ---: |
| Tom 1 | 11 | 0 | 999 | 999 |
| Tom 1 | 12 | 0 | 271 | 271 |
| Tom 2 | 13 | 0 | 647 | 647 |
| Tom 2 | 14 | 0 | 212 | 212 |

Each ADC's channels are numbered 0–15 by the monitor. Other visible
channels changed little in this test. On the second run, the ioctls
returned `map=8192`, `DMA=4096`, and `portion=32` for each ADC. There were
58 and 68 rejected offset reads respectively. The photo does not show the
start of the first run, so its total frames and offsets were not recorded.
The monitor samples the cursor periodically and may miss peaks; the observed
range is not a calibrated force measurement. The cause of rejected offsets
still needs investigation.

This test suggests pairs of channels for toms. In a later tom 1 test,
separate hits produced the stronger response on **ADC1 channel 11 for head**
and **ADC1 channel 12 for rim**. This establishes an observed functional map
for that pad; we do not yet have time-series captures to measure crosstalk,
thresholds, or velocity curves. For tom 2, the owner also separated the
zones: **ADC1 channel 13 for head** and **ADC1 channel 14 for rim**.

In additional tests reported from the same shell, each dual-zone pad
activated its own pair of channels across the ADCs. The one single-zone pad
tested activated only one channel. The other channel numbers were not
recorded, so the full map remains open. The pattern fits one analog input
per zone, but isolated center and rim hits are needed to distinguish zone
response from mechanical coupling between sensors.

To document the map, record for each jack: pad name, strike region, ADC,
channel index (0–15), minimum, maximum, delta, and whether another region
also changes that channel. Repeat with light and strong hits to avoid
confusing a missed peak with an absent signal.
