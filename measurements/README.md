# Pad measurements

Raw measurements live here, grouped as `<pad>/<session>/`. Each verified session
contains conversion data (`.omadc` or `.omseq`), guided labels (`.json`), a sealing
marker (`ready.json`) and `download.json` with SHA-256 hashes and source provenance.
Large recordings, receiver logs and temporary transfers are intentionally excluded
from Git. Back up this directory separately before deleting or moving the project.

Run the receiver directly in a terminal from the project directory:

```sh
python3 scripts/receive_pad_measurements.py
```

Keep the terminal open during capture. Completed pad sessions transfer
and verify automatically; SCP displays transfer progress in the terminal.
Press **Ctrl+C** to stop. The receiver installs no background service and
reconnects to the module while this command is running. When it is stopped,
recordings remain on the module until the next run.

For a single pass over already completed sessions:

```sh
python3 scripts/receive_pad_measurements.py --once
```

`--import-existing` also seals inactive legacy/partial sessions for archiving.
It never interrupts an active wizard. It is used once when migrating old captures.

The receiver checks source hashes before and after transfer, checks every local
file, flushes local data, then atomically publishes the session. Only after that
does it acknowledge the module so it can remove the verified SD copy. Failed
transfers leave the source intact. The wizard waits for this acknowledgment before
enabling **Next pad**; reconnect the Mac to resume a pending transfer.

Each session is directly usable as a verified source directory in the training
manifest. Legacy `.omadc` windows have guided articulation/strength labels. The current
wizard records two seconds of ambient noise, then twenty seconds per articulation
(head, rim only, rimshot). `.omseq` files preserve rapid playing, timing bounds and
gaps; `sequence_training.py` extracts reproducible labeled onset candidates for
the classifier while preserving the source recordings. See
`research/calibration-wizard.md` for the format and evaluation limits.

Response calibration is fitted automatically from each complete verified session
before the module releases its SD copy. The latest successful per-pad response
and per-session diagnostics are stored in `.profiles/`; the module persists its
own copy for live operation without the Mac. Missing hits or insufficient force
variation retain the previous profile and are reported on screen. See
`research/pad-response-calibration.md` for formulas and validation limits.

Inspect the latest session for one pad in the official Rerun viewer:

```sh
scripts/inspect_pads.sh --pad tom1 --open
```

Use `--all-sessions` to export every verified session. See
`research/pad-inspection.md` for the viewer controls, output files and timing limits.
