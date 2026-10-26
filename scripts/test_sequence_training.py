#!/usr/bin/env python3
"""Check continuous guided labels, boundaries, clipping, tags and example identity."""
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest

from inspect_pad_sequence import inspect
from sequence_training import load_sequence_trials


def fixture(root, phase="head-varied", gap=False, clipped=False):
    capture = root / ("a" * 32 + ".omseq")
    with capture.open("wb") as stream:
        stream.write(struct.pack("<8sII", b"OMSEQ001", 1, 2000))
        for record in range(160):
            if gap and record == 70:
                stream.write(struct.pack("<QHHI", record * 2_000_000 + 1, 0, 0, 1))
                continue
            stream.write(struct.pack("<QHHI", record * 2_000_000 + 1, 16, 16, 0))
            for adc in range(2):
                for frame in range(16):
                    absolute = record * 16 + frame
                    position = absolute % 400
                    amplitude = round(700 * .8 ** (position - 100)) if 100 <= position < 140 else 0
                    if phase == "ambient":
                        amplitude = 0
                    values = [2] * 16
                    if adc == 1:
                        values[10] += amplitude
                        values[11] += amplitude // 2
                        if clipped and amplitude:
                            values[10] = 4095
                    words = [((channel << 12) | values[channel]) for channel in [(slot + 15) % 16 for slot in range(16)]]
                    stream.write(struct.pack("<16H", *words))
    metadata = {"format": "open-mimic-pad-sequence-v1", "capture": capture.name,
                "channel_addressing": "ads7953-tag-v1", "phase": phase, "zone_count": 2,
                "head": {"adc": 1, "channel": 10}, "rim": {"adc": 1, "channel": 11},
                "session_id": "b" * 32, "pad": "tom1", "quality": {"usable": not gap}}
    path = root / ("a" * 32 + ".json")
    path.write_text(json.dumps(metadata))
    return path


class SequenceTests(unittest.TestCase):
    def test_guided_articulations_keep_distinct_examples_from_one_file(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for phase, expected in (("head-varied", 1), ("rim-varied", 2), ("rimshot-varied", 3)):
                path = fixture(root, phase)
                trials = load_sequence_trials(path)
                hits = [trial for trial in trials if trial["label"]]
                self.assertGreaterEqual(len(hits), 5)
                self.assertTrue(all(t["label"] == expected for t in hits))
                self.assertEqual(len({t["example_id"] for t in trials}), len(trials))
                self.assertEqual(len({t["capture_sha256"] for t in trials}), 1)
                self.assertEqual(hits[0]["waveform"][0][0], 700)
                summary = inspect(root / ("a" * 32 + ".omseq"))
                self.assertEqual(summary["adc_frames"], [2560, 2560])

    def test_gaps_do_not_join_windows_and_clipping_is_not_trained(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = fixture(root, gap=True)
            trials = load_sequence_trials(path)
            self.assertTrue(all(t["source_gap_records"] == 1 for t in trials))
            self.assertEqual({t["segment_index"] for t in trials}, {0, 1})
            path = fixture(root, clipped=True)
            self.assertTrue(all(t["label"] == 0 for t in load_sequence_trials(path)))

    def test_ambient_records_only_background(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = fixture(Path(temporary), "ambient")
            trials = load_sequence_trials(path)
            self.assertGreater(len(trials), 8)
            self.assertTrue(all(t["label"] == 0 for t in trials))

    def test_verified_manifest_does_not_deduplicate_distinct_windows(self):
        from train_shared_drum_model import collect_manifest
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fixture(root)
            files = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
            (root / "download.json").write_text(json.dumps({"verified_files": files}))
            manifest = {"format": "open-mimic-shared-training-v1", "sources": [{"directory": ".", "pads": {
                "tom1": {"mapping": {"head": {"adc": 1, "channel": 10}, "rim": {"adc": 1, "channel": 11}},
                         "physical_pad_group": "tom1", "identity_basis": "test fixture"}}}]}
            (root / "manifest.json").write_text(json.dumps(manifest))
            trials, excluded = collect_manifest(root / "manifest.json")
            self.assertFalse(excluded)
            self.assertGreater(sum(t["label"] == 1 for t in trials), 1)


if __name__ == "__main__":
    unittest.main()
