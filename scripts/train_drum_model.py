#!/usr/bin/env python3
"""Train an experimental pad waveform classifier from guided calibration trials.

Requires independent sessions and valid configured channels on one ADC.
Single-zone models use background/head; dual-zone models add rim. No model is
installed or enabled on the Mimic. Source captures are never modified.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import statistics

from drum_model import FRAMES, LABELS, evaluate, features, fit, save_model
from reconstruct_adc_capture import FRAME, HEADER, interval_offsets, read_record

PHASE_LABELS = {"noise": 0, "xtalk": 0,
                "head-soft": 1, "head-medium": 1, "head-hard": 1,
                "rim-soft": 2, "rim-medium": 2, "rim-hard": 2,
                "rimshot-soft": 3, "rimshot-medium": 3, "rimshot-hard": 3}


def channel(value: dict) -> tuple[int, int]:
    adc, index = value["adc"], value["channel"]
    if type(adc) is not int or type(index) is not int or adc not in (0, 1) or not 0 <= index < 16:
        raise ValueError("invalid ADC/channel assignment")
    return adc, index


def load_trial(path: Path, mapping_override: dict | None = None) -> dict:
    if path.stat().st_size > 65536:
        raise ValueError("trial metadata exceeds size limit")
    metadata = json.loads(path.read_text())
    if not isinstance(metadata, dict) or metadata.get("format") != "open-mimic-pad-trial-v1":
        raise ValueError("not a guided pad trial")
    quality = metadata.get("quality")
    if not isinstance(quality, dict) or quality.get("usable") is not True:
        raise ValueError("capture quality rejected: " + str(quality.get("reason", "unknown") if isinstance(quality, dict) else "invalid quality metadata"))
    phase = metadata["phase"]
    if phase not in PHASE_LABELS:
        raise ValueError("unsupported training phase")
    zone_count = metadata.get("zone_count")
    if type(zone_count) is not int or zone_count not in (1, 2):
        raise ValueError("invalid or missing zone count")
    original_mapping = {"head": metadata["head"], "rim": metadata["rim"]}
    if mapping_override is not None:
        if set(mapping_override) != {"head", "rim"}:
            raise ValueError("mapping correction may only replace head/rim channels")
        metadata = dict(metadata, **mapping_override)
    head_adc, head_channel = channel(metadata["head"])
    rim_adc, rim_channel = channel(metadata["rim"]) if zone_count == 2 else (head_adc, None)
    if zone_count == 1 and (metadata.get("rim") is not None or phase.startswith("rim")):
        raise ValueError("single-zone capture must not contain a rim assignment or label")
    if head_adc != rim_adc or head_channel == rim_channel:
        raise ValueError("pilot needs two distinct head/rim channels on the same ADC")
    name = metadata["capture"]
    if not isinstance(name, str) or Path(name).name != name or not name.endswith(".omadc"):
        raise ValueError("capture must be a sibling OMADC file")
    capture = (path.parent / name).resolve()
    if capture.parent != path.parent.resolve() or not 0 < capture.stat().st_size <= 2 * 1024 * 1024:
        raise ValueError("invalid capture location or size")
    records = []
    with capture.open("rb") as source:
        header = source.read(HEADER.size)
        if len(header) != HEADER.size:
            raise ValueError("truncated capture header")
        magic, poll, map0, dma0, portion0, map1, dma1, portion1 = HEADER.unpack(header)
        if magic != b"OMADC002" or (portion0, portion1) != (32, 32) or not 100 <= poll <= 100000:
            raise ValueError("unsupported capture layout")
        sizes = (dma0, dma1)
        if any(size < 32 or size > 65536 or size % 32 for size in sizes) or map0 < dma0 + 4 or map1 < dma1 + 4:
            raise ValueError("invalid DMA sizes")
        while (record := read_record(source, sizes)) is not None:
            records.append(record)
            if len(records) > 128:
                raise ValueError("too many snapshots in trial window")
    if len(records) < 5 or any(second[0] <= first[0] for first, second in zip(records, records[1:])):
        raise ValueError("capture needs ordered snapshot timestamps")
    if metadata.get("snapshot_count") != len(records):
        raise ValueError("capture snapshot count differs from its completed-trial metadata")
    addressing = metadata.get("channel_addressing", "dma-position-v1")
    if addressing not in ("dma-position-v1", "ads7953-tag-v1"):
        raise ValueError("unsupported channel addressing")
    samples = []
    snapshot_indices = []
    for index in range(1, len(records) - 1):
        previous, current, following = records[index - 1:index + 2]
        ring, next_ring = current[3][head_adc], following[3][head_adc]
        # A complete wrap is invisible in modulo cursors. Reject scheduling gaps
        # exceeding this board's conservative collection bound, without guessing
        # an exact number of lost ADC frames.
        if current[0] - previous[0] > 8_000_000:
            raise ValueError("poll gap makes ring reconstruction ambiguous")
        for offset in interval_offsets(previous[1][head_adc], current[1][head_adc], sizes[head_adc]):
            raw = ring[offset:offset + FRAME.size]
            if raw != next_ring[offset:offset + FRAME.size]:
                raise ValueError("completed frame changed in following snapshot")
            values = FRAME.unpack(raw)
            if addressing == "ads7953-tag-v1":
                decoded = {word >> 12: word & 0xfff for word in values}
                if len(decoded) != 16:
                    raise ValueError("ADC frame contains duplicate or missing channel IDs")
                values = [decoded[channel] for channel in range(16)]
            samples.append((values[head_channel] & 0xfff,
                            values[rim_channel] & 0xfff if rim_channel is not None else 0))
            snapshot_indices.append(index)
    if len(samples) < FRAMES * 2:
        raise ValueError("not enough verified completed frames")
    baselines = [statistics.median(value[index] for value in samples[:FRAMES]) for index in range(2)]
    centered = [[sample[index] - baselines[index] for sample in samples] for index in range(2)]
    label = PHASE_LABELS[phase]
    if label:
        threshold = metadata.get("threshold")
        if type(threshold) not in (int, float) or not 1 <= threshold < 4095:
            raise ValueError("invalid onset threshold")
        onset = next((index for index in range(FRAMES, len(samples))
                      if max(abs(centered[0][index]), abs(centered[1][index])) >= threshold), None)
        if onset is None:
            raise ValueError("no target-pad onset above the recorded collection threshold")
    else:
        onset_record = metadata.get("onset_record_index")
        if type(onset_record) is not int or not 1 <= onset_record < len(records) - 1:
            raise ValueError("invalid onset snapshot index")
        onset = next((index for index, snapshot in enumerate(snapshot_indices)
                      if index >= FRAMES and snapshot >= onset_record), None)
        if onset is not None and phase == "xtalk":
            # Other-pad onset can precede vibration at the target pad. Retain
            # the strongest target response, not an earlier silent window.
            stop = min(onset + 192, len(samples) - FRAMES + 1)
            if stop > onset:
                peak = max(range(onset, stop), key=lambda index:
                           max(abs(centered[0][index]), abs(centered[1][index])))
                onset = max(onset, peak - 4)
    if onset is None or onset + FRAMES > len(samples):
        raise ValueError("incomplete post-onset waveform window")
    if any(value >= 4090 for sample in samples for value in sample):
        raise ValueError("clipped waveform cannot be used for pilot training")
    waveform = [values[onset:onset + FRAMES] for values in centered]
    elapsed = (records[-2][0] - records[0][0]) / 1e9
    if elapsed <= 0:
        raise ValueError("invalid capture duration")
    session = metadata.get("session_id")
    pad = metadata.get("pad")
    if not isinstance(session, str) or not session or not isinstance(pad, str) or not pad:
        raise ValueError("missing pad/session identity")
    return {"metadata": str(path), "capture": str(capture), "session": session, "pad": pad,
            "zone_count": zone_count,
            "phase": phase, "label": label, "features": features(waveform), "waveform": waveform,
            "mapping": {"head": metadata["head"], "rim": metadata["rim"]},
            "original_mapping": original_mapping,
            "hardware_gain": metadata.get("hardware_gain"),
            "estimated_frames_per_second": len(samples) / elapsed,
            "capture_sha256": hashlib.sha256(capture.read_bytes()).hexdigest(),
            "metadata_sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def collect(directory: Path, pad: str) -> tuple[list[dict], list[dict]]:
    trials, excluded = [], []
    for path in sorted(directory.rglob("*.json")):
        try:
            if path.stat().st_size > 65536:
                continue
            document = json.loads(path.read_text())
            if not isinstance(document, dict) or document.get("format") != "open-mimic-pad-trial-v1" or document.get("pad") != pad:
                continue
            trials.append(load_trial(path))
        except (OSError, ValueError, KeyError, TypeError) as error:
            excluded.append({"metadata": str(path), "reason": str(error)})
    return trials, excluded


def train(trials: list[dict], validation_session: str | None, epochs: int) -> tuple[dict, dict]:
    sessions = sorted({trial["session"] for trial in trials})
    if len(sessions) < 2:
        raise ValueError("Record a second independent session using New session; one session cannot validate itself")
    held_out = validation_session or sessions[-1]
    if held_out not in sessions:
        raise ValueError("validation session not found")
    validation = [trial for trial in trials if trial["session"] == held_out]
    reference = validation[0]
    mapping = reference["mapping"]
    gains = reference["hardware_gain"]
    zone_count = reference["zone_count"]
    class_count = zone_count + 1
    def compatible(trial):
        return (trial["mapping"] == mapping and trial["hardware_gain"] == gains
                and trial["zone_count"] == zone_count and trial["pad"] == reference["pad"])
    if not all(compatible(trial) for trial in validation):
        raise ValueError("validation session contains incompatible channel/gain/zone configurations")
    eligible = [trial for trial in trials if compatible(trial)]
    excluded_configurations = [{"session": trial["session"], "phase": trial["phase"]}
                               for trial in trials if not compatible(trial)]
    if any(trial["label"] == 3 for trial in eligible):
        raise ValueError("Rimshot data requires train_tensorflow_model.py")
    rates = [trial["estimated_frames_per_second"] for trial in eligible]
    cadence = statistics.median(rates)
    if not 1000 <= cadence <= 20000 or max(abs(rate / cadence - 1) for rate in rates) > 0.10:
        raise ValueError("recording cadence differs too much between trials")
    training = [trial for trial in eligible if trial["session"] != held_out]
    if not training:
        raise ValueError("Record a second independent session with the same pad/channel/zone configuration")
    if {trial["capture_sha256"] for trial in training} & {trial["capture_sha256"] for trial in validation}:
        raise ValueError("identical capture payload occurs in training and validation; session labels alone do not prevent leakage")
    for name, part, minimum in (("training", training, 8), ("validation", validation, 4)):
        counts = Counter(trial["label"] for trial in part)
        if any(counts[index] < minimum for index in range(class_count)):
            raise ValueError(f"{name} needs at least {minimum} usable trials per class; counts={dict(counts)}")
    model = fit([(trial["features"], trial["label"]) for trial in training], epochs=epochs,
                zone_count=zone_count)
    report = {"format": "open-mimic-drum-evaluation-v1", "pilot_only": True,
              "training_sessions": sorted({trial["session"] for trial in training}),
              "validation_session": held_out, "training_trials": len(training),
              "mapping": mapping, "hardware_gain": gains,
              "pad": reference["pad"], "zone_count": zone_count,
              "excluded_incompatible_configurations": excluded_configurations,
              "hardware_gain_verified": gains is not None,
              "estimated_frames_per_second": cadence,
              "observation_window_ms_estimate": FRAMES / cadence * 1000,
              "sample_timestamps_are_snapshot_bounds": True,
              "training": evaluate(model, [(trial["features"], trial["label"]) for trial in training]),
              "held_out": evaluate(model, [(trial["features"], trial["label"]) for trial in validation]),
              "held_out_by_phase": {phase: evaluate(model, [(trial["features"], trial["label"])
                                                            for trial in validation if trial["phase"] == phase])
                                    for phase in sorted({trial["phase"] for trial in validation})},
              "scope": "classification only; no velocity estimator, retrigger policy, MIDI authorization or latency guarantee"}
    return model, report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dataset", type=Path)
    parser.add_argument("--pad", required=True, help="stable configured pad identifier")
    parser.add_argument("--inspect", action="store_true")
    parser.add_argument("--validation-session")
    parser.add_argument("--epochs", type=int, default=400)
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()
    try:
        if not re.fullmatch(r"[a-z][a-z0-9_-]{0,31}", args.pad):
            raise ValueError("invalid pad identifier")
        if not args.dataset.is_dir():
            raise ValueError("dataset directory does not exist")
        trials, excluded = collect(args.dataset, args.pad)
        counts = {}
        for trial in trials:
            per_session = counts.setdefault(trial["session"], Counter())
            per_session[LABELS[trial["label"]]] += 1
        if args.inspect:
            print(json.dumps({"sessions": counts, "excluded": excluded}, indent=2))
            return 0
        if args.output_dir is None:
            parser.error("--output-dir is required when training")
        model, report = train(trials, args.validation_session, args.epochs)
        report["excluded"] = excluded
        report["provenance"] = [{key: trial[key] for key in ("metadata", "capture", "capture_sha256", "metadata_sha256",
                                                           "session", "phase", "label")} for trial in trials]
        args.output_dir.mkdir(parents=True, exist_ok=False)
        save_model(model, args.output_dir / "model.omdrum")
        (args.output_dir / "model.json").write_text(json.dumps(model, indent=2) + "\n")
        (args.output_dir / "evaluation.json").write_text(json.dumps(report, indent=2) + "\n")
        print(f"Pilot trained on {report['training_trials']} trials; held out session {report['validation_session']}")
        print(f"Held-out balanced accuracy: {report['held_out']['balanced_accuracy']:.3f}")
        if report["excluded_incompatible_configurations"]:
            print(f"Skipped {len(report['excluded_incompatible_configurations'])} trials from incompatible pad configurations")
        print("Not installed: validate real playing, clipping, timing and event policy before enabling MIDI.")
        return 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f"error: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
