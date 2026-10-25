# mimicd

This is the first Rust component of the Open Mimic application. It currently
probes the two Pearl trigger ADC character devices and captures diagnostic,
read-only DMA snapshots. It does not yet detect hits, play audio, run a model,
or modify the original SSD.

The hardware interface is documented in
[`research/hardware-api-map.md`](../../research/hardware-api-map.md). The
snapshot format is intentionally raw because the driver's cursor semantics
and exact frame ordering still require validation against live captures.

## Usage on the Mimic

Build the Buildroot image and boot it, then over SSH:

```sh
mimicd probe
mimicd capture /tmp/tom1-head.omadc 5
```

The capture command creates a new file; it refuses to overwrite an existing
one. Buildroot does not currently include an SFTP server, so transfer a capture
to the development machine with SSH output redirection:

```sh
ssh root@192.168.31.215 'cat /tmp/tom1-head.omadc' > tom1-head.omadc
```

Keep captures outside the public repository because pad recordings may be
large.

Inspect a transferred capture with
`python3 scripts/inspect_adc_capture.py tom1-head.omadc`.
To extract complete frames for a known ADC and channels, use
`python3 scripts/reconstruct_adc_capture.py tom1-head.omadc tom1-head.csv --adc 1 --channels 11 12`.
The CSV contains frame indices and the upper-bound timestamp of the snapshot
that supplied each frame; it does not assign an exact hardware timestamp to
each sample.

Each little-endian `.omadc` file contains:

- 8-byte magic `OMADC002`;
- one `u32` requested polling interval in microseconds;
- for ADC0 and ADC1, three `u32` values: map size, DMA size, portion size;
- repeated records: elapsed nanoseconds (`u64`), cursor values before and
  after copying each ADC (`u32` each), then the full DMA ring bytes of ADC0
  and ADC1.

Snapshots are best effort. Cursor movement is recorded rather than causing a
record to be dropped. A snapshot can contain data from more than one ring
generation; neither gap-free capture nor sample timing is established.
The file preserves evidence for decoding the driver contract before a
real-time trigger engine is added.

Run host-side tests with `cargo test --manifest-path software/mimicd/Cargo.toml`.
