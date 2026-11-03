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
import math
from pathlib import Path
import statistics

from inspect_pad_sequence import read_records
from sequence_training import load_sequence_trials

class RecordingIntegrityError(ValueError):
    """The local recording no longer matches its verified source; never release SD data."""


ALGORITHM = 3
# Default player threshold offset of the live engine (TriggerSettings::threshold).
DEFAULT_THRESHOLD_OFFSET = 4.0
RETRIGGER_FACTOR = 1.05 + 1.95 * 30 / 100
# Zone rule percentiles: the strongest cross-coupling seen in single-zone takes
# defines the rimshot levels (Mimic Pro "hit hardest, set just above").
RATIO_PERCENTILES = (.95, .05)
RIMSHOT_PERCENTILE = .99
RIMSHOT_MARGIN = 1.15
FRAMES_PER_MS = 11.25
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


def decay_profile(path: Path, metadata: dict, sensors, sensor: int, minimum_peak: float):
    """Envelope time constant of a strike's tail, in milliseconds.

    Uses the continuous recording, not the 32-frame windows. The first spike decays
    much faster than the ringing tail, so the constant is fitted from the time the
    peak follower needs to reach a low level (2x threshold or 2% of the peak) and
    the 75th percentile across strikes is kept: a generous envelope suppresses the
    tail; a short one retriggers on it.
    """
    head = global_channel(metadata["head"])
    rim = global_channel(metadata.get("input_rim", metadata.get("rim")))
    channel = (head if sensor == 0 else rim) % 16
    baseline = sensors[sensor]["baseline"]
    threshold = sensors[sensor]["threshold"]
    samples = []
    for _timestamp, batches, flags, _poll in read_records(path.parent / metadata["capture"]):
        if flags:
            samples.append(None)
            continue
        for frame in batches[head // 16]:
            samples.append(abs({word >> 12: word & 4095 for word in frame}[channel] - baseline))
    times = []
    index = 0
    while index < len(samples):
        value = samples[index]
        if value is None or value < threshold:
            index += 1
            continue
        # Candidate strike: locate its peak within 3 ms, then follow the envelope down.
        window = [v for v in samples[index:index + 34] if v is not None]
        if not window:
            index += 1
            continue
        peak = max(window)
        if peak < minimum_peak:
            index += 34
            continue
        start = index + window.index(peak)
        target = max(2.0 * threshold, peak / 50.0)
        envelope = peak
        position = start
        while position < len(samples) and samples[position] is not None:
            envelope = max(samples[position], envelope * .97)
            if envelope <= target:
                times.append((position - start) / FRAMES_PER_MS / math.log(peak / target))
                break
            position += 1
        else:
            position = start + 1
        # Skip the rest of this strike: resume once the signal is quiet for 32 frames.
        quiet = 0
        while position < len(samples) and quiet < 32:
            value = samples[position]
            quiet = 0 if value is None or value >= threshold else quiet + 1
            position += 1
        index = position
    if len(times) < 8:
        raise ValueError("Too few strong hits to measure the sensor decay; include strong strokes")
    return min(500.0, max(2.0, quantile(times, .75)))


def zone_rule(records, sensors, hits, ratios, decays):
    """Head/rim separation for the live rule; None for single-zone pads."""
    head_ceiling = quantile(ratios["head"], RATIO_PERCENTILES[0])
    rim_floor = quantile(ratios["rim"], RATIO_PERCENTILES[1])
    narrow = rim_floor <= head_ceiling
    if narrow:
        # Sensors overlap (weak rim piezo); keep a thin band around their geometric mean.
        centre = math.sqrt(max(head_ceiling, 1e-3) * max(rim_floor, 1e-3))
        head_ceiling, rim_floor = centre / 1.05, centre * 1.05
    rimshot_rim = max(2.0 * sensors[1]["threshold"], quantile(hits["head"], RIMSHOT_PERCENTILE) * RIMSHOT_MARGIN)
    rimshot_head = max(2.0 * sensors[0]["threshold"], quantile(hits["rim"], RIMSHOT_PERCENTILE) * RIMSHOT_MARGIN)
    return {"head_ratio_ceiling": head_ceiling, "rim_ratio_floor": rim_floor,
            "rimshot_head": min(4095.0, rimshot_head), "rimshot_rim": min(4095.0, rimshot_rim),
            "decay_ms": decays, "narrow_separation": narrow,
            "ratio_percentiles": list(RATIO_PERCENTILES), "rimshot_percentile": RIMSHOT_PERCENTILE,
            "rimshot_margin": RIMSHOT_MARGIN, "records": records}


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
    decays = [decay_profile(records[PHASES[name]][0], records[PHASES[name]][1], sensors, index,
                            max(40.0, 8.0 * sensors[index]["threshold"]))
              for index, name in enumerate(("head", "rim")[:zones])]
    articulations = {}
    ignored = {}
    other_peaks = {"head": [], "rim": []}
    ratios = {"head": [], "rim": []}
    strong = {"head": 0, "rim": 0}
    for name in names:
        path, metadata = records[PHASES[name]]
        if (metadata["pad"], metadata["session_id"], metadata["zone_count"], metadata["head"], metadata["rim"]) != (
                pad, session, zones, ambient["head"], ambient["rim"]):
            raise ValueError("Recording sessions or sensor assignments do not match")
        rows = load_sequence_trials(path, sensor_calibration=sensors)
        label = {"head": 1, "rim": 2, "rimshot": 3}[name]
        peaks = []
        ignored[name] = 0
        tail_blips = 0
        envelope = [0.0, 0.0]
        last_segment, last_start = None, None
        for row in sorted((r for r in rows if r["label"] == label), key=lambda r: (r["segment_index"], r["source_frame_start"])):
            sensor_peaks = [max(abs(sample) for sample in channel) for channel in row['waveform']]
            # Mirror the live stage-one policy: candidates riding the decaying tail of a
            # previous strike, or below the default player threshold, are not hits.
            if last_segment == row["segment_index"]:
                elapsed = (row["source_frame_start"] - last_start) / FRAMES_PER_MS
                envelope = [envelope[c] * math.exp(-elapsed / decays[min(c, len(decays) - 1)]) for c in range(2)]
            else:
                envelope = [0.0, 0.0]
            floor = [sensors[c]["threshold"] + DEFAULT_THRESHOLD_OFFSET for c in range(2)]
            onset = any(sensor_peaks[c] >= floor[c] and sensor_peaks[c] >= envelope[c] * RETRIGGER_FACTOR
                        for c in range(zones))
            envelope = [max(envelope[c], sensor_peaks[c]) for c in range(2)]
            last_segment, last_start = row["segment_index"], row["source_frame_start"]
            if not onset:
                tail_blips += 1
                continue
            required = (0, 1) if name == 'rimshot' else (0 if name == 'head' else 1,)
            if any(sensor_peaks[index] < sensors[index]['threshold'] for index in required):
                ignored[name] += 1
                continue
            channels = row["waveform"] if name == "rimshot" else [row["waveform"][0 if name == "head" else 1]]
            peaks.append(max(abs(sample) for channel in channels for sample in channel))
            if name in other_peaks and sensor_peaks[0 if name == "head" else 1] >= 3 * sensors[0 if name == "head" else 1]["threshold"]:
                other_peaks[name].append(sensor_peaks[1 if name == "head" else 0])
                ratios[name].append(sensor_peaks[1] / max(1.0, sensor_peaks[0]))
                strong[name] += 1
        if len(peaks) < 8:
            raise ValueError(f"Only {len(peaks)} usable {name} hits; record at least 8, varying force")
        minimum, maximum = quantile(peaks, .05), quantile(peaks, .95)
        if maximum - minimum < 8:
            raise ValueError(f"Too little {name} dynamic range; include very light and strong hits")
        noise_limit = max(sensor["noise_limit"] for sensor in sensors) if name == "rimshot" else sensors[0 if name == "head" else 1]["noise_limit"]
        if minimum <= noise_limit:
            raise ValueError(f"The lightest {name} hits overlap the measured noise; repeat the quiet-kit recording")
        articulations[name] = {"minimum": minimum, "maximum": maximum, "exponent": 1.0, "samples": len(peaks),
                               "tail_candidates_excluded": tail_blips}
    rule = None
    if zones == 2:
        if min(strong.values()) < 8:
            raise ValueError("Too few strong head or rim hits to separate the zones; include strong strokes")
        rule = zone_rule(strong, sensors, other_peaks, ratios, decays)
    return {"format": "open-mimic-pad-response-v1", "algorithm": ALGORITHM, "pad": pad, "session": session,
            "head": head, "rim": rim, "zones": zones, "source_hash": fingerprint,
            "sensors": sensors, "articulations": articulations, "zone_rule": rule, "ambient_gap_records": gaps,
            "ignored_candidates_without_articulation_sensor_evidence": ignored,
            "velocity_percentiles": [5, 95], "scope": "Amplitude response, not measured physical strike force"}


def native_profile(profile):
    lines = ["OPEN_MIMIC_RESPONSE 2", "pad " + profile["pad"], "session " + profile["session"],
             f"route {profile['head']} {profile['rim']} {profile['zones']}", "source " + profile["source_hash"]]
    for index, sensor in enumerate(profile["sensors"]):
        lines.append(f"sensor {index} {sensor['noise_mean']:.9g} {sensor['noise_limit']:.9g} {sensor['threshold']:.9g} {sensor['baseline']:.9g}")
    for index, name in enumerate(("head", "rim", "rimshot"), 1):
        value = profile["articulations"].get(name, {"minimum": 0, "maximum": 2048, "exponent": 1, "samples": 0})
        lines.append(f"zone {index} {value['minimum']:.9g} {value['maximum']:.9g} {value['exponent']:.9g} {value['samples']}")
    rule = profile.get("zone_rule")
    if rule:
        lines.append("rule " + " ".join(f"{value:.9g}" for value in (
            rule["head_ratio_ceiling"], rule["rim_ratio_floor"], rule["rimshot_head"], rule["rimshot_rim"],
            rule["decay_ms"][0], rule["decay_ms"][1])))
    else:
        lines.append("rule none")
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
