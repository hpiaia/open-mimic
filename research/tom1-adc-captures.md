# Tom 1 ADC captures

Two ten-second captures were recorded on 2026-10-25 using the first Rust
`mimicd` build. The owner struck the tom 1 head at varied strengths and
confirmed one accidental rim strike in the first capture. No rim strikes
were intended in the second capture. Raw captures and extracted CSV files
are local ignored artifacts under `build/`, not public repository content.

| Capture | Snapshots | Reconstructed ADC1 frames | Approximate frames/s | ADC1 ch11 peak | ADC1 ch12 peak |
| --- | ---: | ---: | ---: | ---: | ---: |
| `tom1-head-01.omadc` | 2,889 | 112,755 | 11,278 | 635 | 242 |
| `tom1-head-02.omadc` | 2,890 | 112,775 | 11,278 | 715 | 161 |

Each ADC snapshot contained a 4096-byte circular DMA ring. The cursor was
32-byte aligned, ranged from 32 to 4096, and advanced during every full-ring
copy. Consecutive cursor positions differed by 1216–1536 bytes across the
captures, less than one full ring. Reconstructing frames from the previous
snapshot's cursor to the current snapshot's cursor yielded a continuous
sequence under the assumption that the cursor marks a completed 32-byte frame
boundary. Every extracted frame checked against the following snapshot was
identical in both captures; there were zero mismatches. The final interval
cannot be cross-checked because there is no following snapshot.

Decoding each frame as 16 little-endian 12-bit values confirmed that ADC1
channel 11 had the strongest activity and ADC1 channel 12 had a weaker
response. This matches the earlier observed tom 1 head/rim channel map.
All ADC0 channels stayed at values of 4 or below in these captures; the
remaining ADC1 channels stayed below 20. One event in the first capture,
near 5.9 seconds, was stronger on channel 12 (peak 242) than on channel 11
(peak 79). This is consistent with the owner's confirmed accidental rim
strike, although the exact strike time was not separately marked. Head hits
also produced channel 12 activity, so a simple channel threshold alone will
not be enough to classify the zone reliably.

The approximately 11.28 kframes/s figure is computed from reconstructed
frame count and the capture process's monotonic elapsed time. It is a first
measurement, not a calibrated hardware clock rate. The CSV timestamp is the
time at which the containing ring snapshot finished, not the exact ADC sample
time. Before training a model, label strike zones and strengths explicitly
and measure whether the reconstruction remains lossless during longer runs.
