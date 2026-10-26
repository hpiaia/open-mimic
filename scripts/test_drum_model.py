#!/usr/bin/env python3
"""Synthetic, asset-free checks for waveform extraction and the pilot trainer."""
from __future__ import annotations

import json
import math
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

from drum_model import FEATURES, evaluate, features, fit, predict, save_model
from train_drum_model import load_trial, train


def waveform(label: int, amplitude: int = 600) -> list[list[float]]:
    if label == 0:
        return [[float((index + channel) % 3) for index in range(32)] for channel in range(2)]
    main = [amplitude * math.exp(-index / 8) for index in range(32)]
    secondary = [value * 0.08 for value in main]
    return [main, secondary] if label == 1 else [secondary, main]


def trial_files(folder: Path, phase: str = "head-soft", clipped: bool = False) -> Path:
    folder.mkdir()
    capture = folder / "trial.omadc"
    with capture.open("wb") as output:
        output.write(struct.pack("<8s7I", b"OMADC002", 2000, 8192, 4096, 32, 8192, 4096, 32))
        for record in range(25):
            completed = 64 + record * 8
            cursor = (completed * 32 - 1) % 4096 + 1
            output.write(struct.pack("<Q4I", record * 2_000_000, cursor, cursor, cursor, cursor))
            for adc in range(2):
                ring = bytearray(4096)
                for frame in range(max(0, completed - 128), completed):
                    values = [2] * 16
                    if adc == 1 and 129 <= frame < 161 and phase.startswith("head"):
                        values[11] += round(600 * math.exp(-(frame - 129) / 8))
                        values[12] += round(48 * math.exp(-(frame - 129) / 8))
                    if clipped and adc == 1 and frame == 129:
                        values[11] = 4095
                    struct.pack_into("<16H", ring, frame % 128 * 32, *values)
                output.write(ring)
    metadata = {"format": "open-mimic-pad-trial-v1", "capture": capture.name,
                "trial_id": "trial", "session_id": "run1", "pad": "tom1", "phase": phase,
                "zone_count": 2,
                "snapshot_count": 25,
                "head": {"adc": 1, "channel": 11}, "rim": {"adc": 1, "channel": 12},
                "onset_record_index": 8, "threshold": 20, "hardware_gain": None,
                "quality": {"usable": True, "reason": ""}}
    path = folder / "trial.json"
    path.write_text(json.dumps(metadata))
    return path


class DrumModelTests(unittest.TestCase):
    def test_features_retain_waveform_and_channel_ratio(self):
        values = features([[200.0] + [0.0] * 31, [50.0] + [0.0] * 31])
        self.assertEqual(len(values), FEATURES)
        self.assertEqual(values[0], 1.0)
        self.assertEqual(values[32], 0.25)
        self.assertEqual(values[66:], [1 / 32, 0.25 / 32])
        with self.assertRaises(ValueError):
            features([[float("nan")] * 32, [0.0] * 32])

    def test_synthetic_training_and_cpp_export(self):
        examples = [(features(waveform(label, 300 + index * 70)), label)
                    for label in range(3) for index in range(8)]
        model = fit(examples, epochs=80)
        held_out = [(features(waveform(label, 555)), label) for label in range(3)]
        self.assertEqual(evaluate(model, held_out)["balanced_accuracy"], 1.0)
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            save_model(model, folder / "model.omdrum")
            with self.assertRaises(FileExistsError):
                save_model(model, folder / "model.omdrum")
            executable = os.environ.get("DRUM_CLASSIFY_TOOL")
            if executable:
                for label in range(3):
                    samples = waveform(label, 555)
                    (folder / "waveform.txt").write_text(" ".join(str(value) for channel in samples for value in channel))
                    output = subprocess.check_output([executable, str(folder / "model.omdrum"),
                                                      str(folder / "waveform.txt")], text=True)
                    actual = json.loads(output)
                    expected = predict(model, features(samples))
                    self.assertEqual(actual["label"], ("background", "head", "rim")[label])
                    for first, second in zip(actual["probabilities"], expected):
                        self.assertAlmostEqual(first, second, places=5)

    def test_capture_reconstruction_alignment(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = trial_files(Path(temporary) / "good")
            trial = load_trial(path)
            self.assertEqual(trial["label"], 1)
            self.assertEqual(trial["waveform"][0][0], 600)
            self.assertEqual(trial["waveform"][1][0], 48)
            self.assertEqual(trial["estimated_frames_per_second"], 4000)
            self.assertEqual(trial["features"][0], 1.0)

    def test_physical_channel_tags_survive_dma_rotation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            baseline = load_trial(trial_files(root / "baseline"))
            for rotation in (0, 1, 15):
                path = trial_files(root / f"rotated-{rotation}")
                data = json.loads(path.read_text())
                data["channel_addressing"] = "ads7953-tag-v1"
                path.write_text(json.dumps(data))
                capture = path.parent / data["capture"]
                payload = bytearray(capture.read_bytes())
                for record in range(25):
                    for adc in range(2):
                        ring = 36 + record * (24 + 8192) + 24 + adc * 4096
                        for offset in range(ring, ring + 4096, 32):
                            values = struct.unpack_from("<16H", payload, offset)
                            tagged = [((channel << 12) | values[channel])
                                      for channel in [(slot + rotation) % 16 for slot in range(16)]]
                            struct.pack_into("<16H", payload, offset, *tagged)
                capture.write_bytes(payload)
                self.assertEqual(load_trial(path)["waveform"], baseline["waveform"])
            data["channel_addressing"] = "unknown"
            path.write_text(json.dumps(data))
            with self.assertRaisesRegex(ValueError, "unsupported channel addressing"):
                load_trial(path)

    def test_rimshot_guided_label_is_distinct(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = trial_files(Path(temporary) / "shot", phase="head-medium")
            metadata = json.loads(path.read_text())
            metadata["phase"] = "rimshot-medium"
            path.write_text(json.dumps(metadata))
            self.assertEqual(load_trial(path)["label"], 3)

    def test_single_zone_capture_and_model(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = trial_files(root / "single")
            data = json.loads(path.read_text())
            data.update({"zone_count": 1, "rim": None, "pad": "kick"})
            path.write_text(json.dumps(data))
            extracted = load_trial(path)
            self.assertEqual(extracted["zone_count"], 1)
            self.assertEqual(extracted["waveform"][1], [0] * 32)
            examples = []
            for label in (0, 1):
                for index in range(8):
                    samples = waveform(label, 300 + index * 60)
                    samples[1] = [0.0] * 32
                    examples.append((features(samples), label))
            model = fit(examples, epochs=80, zone_count=1)
            self.assertEqual(model["labels"], ["background", "head"])
            self.assertEqual(evaluate(model, examples)["balanced_accuracy"], 1.0)
            executable = os.environ.get("DRUM_CLASSIFY_TOOL")
            if executable:
                save_model(model, root / "single.omdrum")
                samples = waveform(1, 555)[0]
                (root / "waveform.txt").write_text(" ".join(map(str, samples)))
                actual = json.loads(subprocess.check_output([executable, str(root / "single.omdrum"),
                                                            str(root / "waveform.txt")], text=True))
                self.assertEqual(actual["label"], "head")
                self.assertEqual(len(actual["probabilities"]), 2)

    def test_quality_clipping_and_capture_path_rejection(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaisesRegex(ValueError, "clipped"):
                load_trial(trial_files(root / "clipped", clipped=True))
            path = trial_files(root / "quality")
            data = json.loads(path.read_text())
            data["quality"]["usable"] = False
            path.write_text(json.dumps(data))
            with self.assertRaisesRegex(ValueError, "quality"):
                load_trial(path)
            data["quality"]["usable"] = True
            data["capture"] = "../outside.omadc"
            path.write_text(json.dumps(data))
            with self.assertRaisesRegex(ValueError, "sibling"):
                load_trial(path)

    def test_independent_session_and_payload_checks(self):
        trials = []
        for session, count in (("run1", 8), ("run2", 4)):
            for label in range(3):
                for index in range(count):
                    trials.append({"session": session, "label": label, "phase": ("noise", "head-soft", "rim-soft")[label],
                                   "zone_count": 2, "pad": "tom1",
                                   "features": features(waveform(label, 350 + index * 35)),
                                   "mapping": {"head": {"adc": 1, "channel": 11},
                                               "rim": {"adc": 1, "channel": 12}},
                                   "hardware_gain": None, "estimated_frames_per_second": 11280,
                                   "capture_sha256": f"{session}-{label}-{index}"})
        with self.assertRaisesRegex(ValueError, "second independent session"):
            train(trials[:24], None, 20)
        model, report = train(trials, "run2", 80)
        self.assertEqual(report["held_out"]["trials"], 12)
        self.assertEqual(report["training_trials"], 24)
        self.assertEqual(report["held_out"]["balanced_accuracy"], 1.0)
        self.assertFalse(report["hardware_gain_verified"])
        historical = dict(trials[0], session="old-run", zone_count=1,
                          mapping={"head": {"adc": 1, "channel": 11}, "rim": None})
        _, filtered = train(trials + [historical], "run2", 20)
        self.assertEqual(len(filtered["excluded_incompatible_configurations"]), 1)
        trials[-1]["capture_sha256"] = trials[0]["capture_sha256"]
        with self.assertRaisesRegex(ValueError, "leakage"):
            train(trials, "run2", 20)


if __name__ == "__main__":
    unittest.main()
