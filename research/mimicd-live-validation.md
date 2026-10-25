# First `mimicd` hardware validation

On 2026-10-25, the Rust binary was cross-compiled with the Open Mimic
Buildroot toolchain and run over SSH on the Mimic Pro. The original SSD
remained mounted read-only.

`mimicd probe` returned the same layout for both ADCs:

| Device | Mapped bytes | DMA bytes | Portion bytes |
| --- | ---: | ---: | ---: |
| `/dev/ads7953.0` | 8192 | 4096 | 32 |
| `/dev/ads7953.1` | 8192 | 4096 | 32 |

The first Rust attempt passed an output pointer to each ioctl and received
zeroes. The original `CADC::adcOpen` assembly stores each ioctl's return
register directly; after matching that calling convention, the live probe
returned the values above. The API table in
[hardware-api-map.md](hardware-api-map.md) has been clarified accordingly.

A one-second `OMADC002` diagnostic capture produced 288 records, each with
both 4096-byte DMA rings. The cursor moved during every copy. In this
capture, both cursors were 32-byte aligned and their observed range was
32–4096. The file is a sequence of approximate ring snapshots, not a
gap-free sample stream. It is sufficient to investigate cursor and frame
layout, but should not yet be treated as training data with exact timing.

The raw capture was kept in ignored local build artifacts, outside the public
repository. The next hardware task is to extract only confirmed complete
32-byte frames from the ring, measure gaps and capture latency, then record
labeled pad strikes for the trigger model.
