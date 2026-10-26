#!/usr/bin/env python3
"""Extract reproducible labeled onset candidates from guided continuous pad recordings.

Original files remain intact. Candidate segmentation is a heuristic, not measured
strike truth. Gap boundaries and clipping never become training windows.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import statistics

from drum_model import FRAMES, features
from inspect_pad_sequence import read_records

LABELS = {"ambient": 0, "head-varied": 1, "rim-varied": 2, "rimshot-varied": 3}


def load_sequence_trials(path: Path, mapping_override: dict | None = None) -> list[dict]:
    if path.stat().st_size > 65536:
        raise ValueError("sequence metadata exceeds size limit")
    metadata = json.loads(path.read_text())
    if metadata.get("format") != "open-mimic-pad-sequence-v1":
        raise ValueError("unsupported guided sequence metadata")
    phase = metadata["phase"]
    if phase not in LABELS:
        raise ValueError("unsupported guided sequence articulation")
    zones = metadata["zone_count"]
    if type(zones) is not int or zones not in (1, 2) or (zones == 1 and LABELS[phase] >= 2):
        raise ValueError("sequence articulation is incompatible with its sensors")
    original = {"head": metadata["head"], "rim": metadata["rim"]}
    mapping = mapping_override or original
    from train_drum_model import channel
    adc, primary = channel(mapping["head"])
    secondary_adc, secondary = channel(mapping["rim"]) if zones == 2 else (adc, None)
    if secondary_adc != adc or primary == secondary:
        raise ValueError("sequence needs distinct sensors on one ADC")
    name = metadata["capture"]
    if not isinstance(name, str) or Path(name).name != name or not name.endswith(".omseq"):
        raise ValueError("sequence must be a sibling OMSEQ file")
    capture = path.parent / name
    if capture.is_symlink() or not 0 < capture.stat().st_size <= 32 * 1024 * 1024:
        raise ValueError("sequence file exceeds size or path bounds")
    segments, current = [], []
    count = 0
    elapsed = 0
    gaps = 0
    for timestamp, batches, flags, _poll in read_records(capture):
        elapsed = timestamp
        if flags:
            gaps += 1
            if current:
                segments.append(current)
                current = []
            continue
        for frame in batches[adc]:
            channels = {word >> 12: word & 4095 for word in frame}
            current.append((channels[primary], channels[secondary] if secondary is not None else 0))
            count += 1
    if current:
        segments.append(current)
    if count < FRAMES * 4 or elapsed <= 0:
        raise ValueError("sequence has too few completed frames")
    raw_hash = hashlib.sha256(capture.read_bytes()).hexdigest()
    metadata_hash = hashlib.sha256(path.read_bytes()).hexdigest()
    rate = count * 1e9 / elapsed
    result = []
    for segment_index, samples in enumerate(segments):
        if len(samples) < FRAMES * 4:
            continue
        sorted_channels = [sorted(sample[c] for sample in samples) for c in range(2)]
        baseline = [statistics.median(value for value in values if value <= values[len(values) // 10] + 4)
                    for values in sorted_channels]
        quiet_values = [abs(sample[c] - baseline[c]) for sample in samples
                        for c in range(zones) if abs(sample[c] - baseline[c]) <= 4]
        noise = statistics.median(quiet_values) if quiet_values else 1.0
        threshold = max(8.0, 6.0 * noise + 4.0)
        amplitudes = [max(abs(v[0] - baseline[0]), abs(v[1] - baseline[1]) if zones == 2 else 0)
                      for v in samples]

        def append(start, label):
            raw = samples[start:start + FRAMES]
            if len(raw) != FRAMES or any(v >= 4090 for pair in raw for v in pair):
                return
            waveform = [[float(pair[c] - baseline[c]) for pair in raw] for c in range(2)]
            if zones == 1:
                waveform[1] = [0.0] * FRAMES
            identity = f"{raw_hash}:{adc}:{segment_index}:{start}"
            result.append({"metadata": str(path), "capture": str(capture), "session": metadata["session_id"],
                           "pad": metadata["pad"], "zone_count": zones, "phase": "ambient" if label == 0 else phase,
                           "source_phase": phase, "label": label, "features": features(waveform),
                           "waveform": waveform, "mapping": mapping, "original_mapping": original,
                           "hardware_gain": metadata.get("hardware_gain"), "estimated_frames_per_second": rate,
                           "capture_sha256": raw_hash, "metadata_sha256": metadata_hash,
                           "example_id": identity, "segment_index": segment_index, "source_frame_start": start,
                           "source_frame_count": FRAMES, "source_gap_records": gaps,
                           "segmentation": "offline_onset_candidates_v1", "extraction_threshold": threshold})

        if LABELS[phase] != 0:
            quiet = 0
            armed = False
            cooldown = 0
            for index, amplitude in enumerate(amplitudes):
                if cooldown:
                    cooldown -= 1
                if amplitude < threshold:
                    quiet += 1
                    if quiet >= 32 and cooldown == 0:
                        armed = True
                else:
                    quiet = 0
                    if armed and cooldown == 0:
                        append(index, LABELS[phase])
                        armed = False
                        cooldown = 128
        # Quiet views are spread through the recording, without model-generated labels.
        quiet_candidates = [i for i in range(0, len(samples) - FRAMES + 1, FRAMES)
                            if max(amplitudes[i:i + FRAMES]) < threshold / 2]
        limit = 24 if phase == "ambient" else 8
        if quiet_candidates:
            positions = sorted({int(i * (len(quiet_candidates) - 1) / max(1, limit - 1))
                                for i in range(min(limit, len(quiet_candidates)))})
            for position in positions:
                append(quiet_candidates[position], 0)
    if not result:
        raise ValueError("no usable onset or quiet candidates in sequence")
    return result
