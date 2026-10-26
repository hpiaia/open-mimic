#!/usr/bin/env python3
"""Fit a pad response profile from one verified, complete guided recording session.

This is response calibration, not classifier training or measured strike force.
The most recent complete calibration replaces the pad's previous response;
recordings from all sessions remain available for shared-model training.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import statistics

from inspect_pad_sequence import read_records
from sequence_training import load_sequence_trials

class RecordingIntegrityError(ValueError):
    """The local recording no longer matches its verified source; never release SD data."""


ALGORITHM = 2
PHASES = {"head": "head-varied", "rim": "rim-varied", "rimshot": "rimshot-varied"}


def quantile(values, fraction):
    ordered = sorted(values)
    if not ordered:
        raise ValueError("No samples available")
    offset = (len(ordered) - 1) * fraction
    lower = int(offset)
    upper = min(lower + 1, len(ordered) - 1)
    return float(ordered[lower] + (ordered[upper] - ordered[lower]) * (offset - lower))


def file_hash(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            result.update(block)
    return result.hexdigest()


def global_channel(value):
    if not isinstance(value, dict) or type(value.get("adc")) is not int or type(value.get("channel")) is not int:
        raise ValueError("Missing physical sensor assignment")
    if value["adc"] not in (0, 1) or not 0 <= value["channel"] < 16:
        raise ValueError("Invalid physical sensor assignment")
    return value["adc"] * 16 + value["channel"]


def verified_session(directory: Path):
    report = json.loads((directory / "download.json").read_text())
    files = report["verified_files"]
    if not files:
        raise RecordingIntegrityError("The recording has not been verified")
    for name, expected in files.items():
        path = directory / name
        if Path(name).name != name or path.is_symlink() or not path.is_file() or file_hash(path) != expected:
            raise RecordingIntegrityError("Recording checksum differs: " + name)
    fingerprint = hashlib.sha256(json.dumps(files, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    records = {}
    for name in sorted(files):
        path = directory / name
        if path.suffix != ".json" or name == "ready.json":
            continue
        if path.stat().st_size > 65536:
            raise ValueError("Recording metadata is too large")
        metadata = json.loads(path.read_text())
        if metadata.get("format") != "open-mimic-pad-sequence-v1":
            continue
        phase = metadata["phase"]
        if phase in records:
            raise ValueError("Duplicate articulation in calibration session")
        if metadata.get("channel_addressing") != "ads7953-tag-v1":
            raise ValueError("Calibration requires physical ADC channel IDs")
        if metadata["capture"] not in files:
            raise RecordingIntegrityError("Recording payload is absent from its verification manifest")
        records[phase] = (path, metadata)
    return records, fingerprint


def noise_profile(ambient_path: Path, ambient: dict):
    zones = ambient["zone_count"]
    if type(zones) is not int or zones not in (1, 2):
        raise ValueError("Invalid single/dual-zone setting")
    head = global_channel(ambient["head"])
    rim = global_channel(ambient.get("input_rim", ambient.get("rim")))
    if head // 16 != rim // 16 or head == rim:
        raise ValueError("Calibration needs a physical head/rim pair on one ADC")
    samples = [[], []]
    gaps = 0
    for _timestamp, batches, flags, _poll in read_records(ambient_path.parent / ambient["capture"]):
        if flags:
            gaps += 1
            continue
        for frame in batches[head // 16]:
            channels = {word >> 12: word & 4095 for word in frame}
            samples[0].append(channels[head % 16])
            if zones == 2:
                samples[1].append(channels[rim % 16])
    if len(samples[0]) < 2560:
        raise ValueError("Too little uninterrupted quiet-kit data; repeat calibration")
    sensors = []
    for index in range(2):
        if index >= zones:
            sensors.append({"baseline": 0.0, "noise_mean": 0.25, "noise_limit": 0.0, "threshold": 2.0})
            continue
        baseline = float(statistics.median(samples[index]))
        deviations = [abs(value - baseline) for value in samples[index]]
        noise_limit = quantile(deviations, .999)
        threshold = max(2.0, noise_limit + 2.0)
        if threshold > 1024 or max(samples[index]) >= 4090:
            raise ValueError("Quiet-kit recording contains excessive noise or clipping; repeat calibration")
        sensors.append({"baseline": baseline, "noise_mean": max(.25, statistics.mean(deviations)),
                        "noise_limit": noise_limit, "threshold": threshold})
    return {"head": head, "rim": rim, "sensors": sensors, "gaps": gaps}


def fit_session(directory: Path):
    records, fingerprint = verified_session(directory)
    if "ambient" not in records:
        raise ValueError("A quiet-kit recording is required")
    ambient_path, ambient = records["ambient"]
    pad, session, zones = ambient["pad"], ambient["session_id"], ambient["zone_count"]
    if type(zones) is not int or zones not in (1, 2):
        raise ValueError("Invalid single/dual-zone setting")
    names = ["head"] if zones == 1 else ["head", "rim", "rimshot"]
    for name in names:
        if PHASES[name] not in records:
            raise ValueError(f"Complete the {name} recording before applying a response profile")
    quiet = noise_profile(ambient_path, ambient)
    head, rim, sensors, gaps = quiet["head"], quiet["rim"], quiet["sensors"], quiet["gaps"]
    articulations = {}
    ignored = {}
    for name in names:
        path, metadata = records[PHASES[name]]
        if (metadata["pad"], metadata["session_id"], metadata["zone_count"], metadata["head"], metadata["rim"]) != (
                pad, session, zones, ambient["head"], ambient["rim"]):
            raise ValueError("Recording sessions or sensor assignments do not match")
        rows = load_sequence_trials(path, sensor_calibration=sensors)
        label = {"head": 1, "rim": 2, "rimshot": 3}[name]
        peaks = []
        ignored[name] = 0
        for row in rows:
            if row["label"] != label:
                continue
            sensor_peaks = [max(abs(sample) for sample in channel) for channel in row['waveform']]
            required = (0, 1) if name == 'rimshot' else (0 if name == 'head' else 1,)
            if any(sensor_peaks[index] < sensors[index]['threshold'] for index in required):
                ignored[name] += 1
                continue
            channels = row["waveform"] if name == "rimshot" else [row["waveform"][0 if name == "head" else 1]]
            peaks.append(max(abs(sample) for channel in channels for sample in channel))
        if len(peaks) < 8:
            raise ValueError(f"Only {len(peaks)} usable {name} hits; record at least 8, varying force")
        minimum, maximum = quantile(peaks, .05), quantile(peaks, .95)
        if maximum - minimum < 8:
            raise ValueError(f"Too little {name} dynamic range; include very light and strong hits")
        noise_limit = max(sensor["noise_limit"] for sensor in sensors) if name == "rimshot" else sensors[0 if name == "head" else 1]["noise_limit"]
        if minimum <= noise_limit:
            raise ValueError(f"The lightest {name} hits overlap the measured noise; repeat the quiet-kit recording")
        articulations[name] = {"minimum": minimum, "maximum": maximum, "exponent": 1.0, "samples": len(peaks)}
    return {"format": "open-mimic-pad-response-v1", "algorithm": ALGORITHM, "pad": pad, "session": session,
            "head": head, "rim": rim, "zones": zones, "source_hash": fingerprint,
            "sensors": sensors, "articulations": articulations, "ambient_gap_records": gaps,
            "ignored_candidates_without_articulation_sensor_evidence": ignored,
            "velocity_percentiles": [5, 95], "scope": "Amplitude response, not measured physical strike force"}


def native_profile(profile):
    lines = ["OPEN_MIMIC_RESPONSE 1", "pad " + profile["pad"], "session " + profile["session"],
             f"route {profile['head']} {profile['rim']} {profile['zones']}", "source " + profile["source_hash"]]
    for index, sensor in enumerate(profile["sensors"]):
        lines.append(f"sensor {index} {sensor['noise_mean']:.9g} {sensor['noise_limit']:.9g} {sensor['threshold']:.9g} {sensor['baseline']:.9g}")
    for index, name in enumerate(("head", "rim", "rimshot"), 1):
        value = profile["articulations"].get(name, {"minimum": 0, "maximum": 2048, "exponent": 1, "samples": 0})
        lines.append(f"zone {index} {value['minimum']:.9g} {value['maximum']:.9g} {value['exponent']:.9g} {value['samples']}")
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("session", type=Path)
    parser.add_argument("--native", action="store_true", help="print the module's response profile format")
    args = parser.parse_args()
    try:
        profile = fit_session(args.session)
        print(native_profile(profile) if args.native else json.dumps(profile, indent=2))
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"Calibration unavailable: {error}\n")
