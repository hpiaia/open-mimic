#!/usr/bin/env python3
"""Response calibration tests using synthetic tagged ADC recordings, never owner data."""
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest

from calibrate_pad import fit_session, native_profile


def session_fixture(root, zones=2, flat=False, missing=None, quiet_noise=0, misdirected_rim=False):
    folder = root / "tom1" / ("a" * 32)
    folder.mkdir(parents=True)
    phases = ["ambient", "head-varied"] + (["rim-varied", "rimshot-varied"] if zones == 2 else [])
    for index, phase in enumerate(phases):
        if phase == missing:
            continue
        identifier = f"{index + 1:032x}"
        capture = folder / (identifier + ".omseq")
        with capture.open("wb") as stream:
            stream.write(struct.pack("<8sII", b"OMSEQ001", 1, 2000))
            records = 200 if phase == "ambient" else 400
            for record in range(records):
                stream.write(struct.pack("<QHHI", record * 2000000 + 1, 16, 16, 0))
                for adc in range(2):
                    for frame in range(16):
                        time = record * 16 + frame
                        position = time % 400
                        amplitude = (400 if flat else 80 + 40 * (time // 400))
                        value = round(amplitude * .8 ** (position - 100)) if 100 <= position < 140 else 0
                        channels = [2] * 16
                        if adc == 1:
                            if phase == "ambient":
                                channels[10] += quiet_noise * (time % 3)
                                channels[11] += quiet_noise * ((time + 1) % 3)
                            elif phase == "head-varied":
                                channels[10] += value; channels[11] += value // 8
                            elif phase == "rim-varied":
                                if misdirected_rim and (time // 400) % 2 == 0:
                                    channels[10] += value
                                else:
                                    channels[10] += value // 8; channels[11] += value
                            else:
                                channels[10] += value * 3 // 4; channels[11] += value
                        stream.write(struct.pack("<16H", *[((c << 12) | channels[c]) for c in range(16)]))
        metadata = {"format": "open-mimic-pad-sequence-v1", "capture": capture.name,
                    "channel_addressing": "ads7953-tag-v1", "phase": phase, "zone_count": zones,
                    "head": {"adc": 1, "channel": 10}, "rim": {"adc": 1, "channel": 11} if zones == 2 else None,
                    "input_rim": {"adc": 1, "channel": 11}, "session_id": "a" * 32, "pad": "tom1",
                    "quality": {"usable": True}}
        (folder / (identifier + ".json")).write_text(json.dumps(metadata))
    (folder / "ready.json").write_text('{}')
    hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in folder.iterdir()}
    (folder / "download.json").write_text(json.dumps({"verified_files": hashes}))
    return folder


class ResponseTests(unittest.TestCase):
    def test_dual_zone_profile_has_separate_articulation_ranges(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = session_fixture(Path(temporary))
            profile = fit_session(folder)
            self.assertEqual((profile["head"], profile["rim"], profile["zones"]), (26, 27, 2))
            self.assertEqual(set(profile["articulations"]), {"head", "rim", "rimshot"})
            for response in profile["articulations"].values():
                self.assertGreaterEqual(response["samples"], 8)
                self.assertGreater(response["maximum"] - response["minimum"], 8)
            self.assertLess(profile["sensors"][0]["threshold"], 16)
            native = native_profile(profile)
            self.assertTrue(native.startswith("OPEN_MIMIC_RESPONSE 1\n"))
            self.assertIn("route 26 27 2\n", native)
            self.assertIn("zone 3 ", native)

    def test_training_uses_the_same_measured_onset_threshold(self):
        from train_shared_drum_model import collect_manifest
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            folder = session_fixture(root)
            manifest = {"format": "open-mimic-shared-training-v1", "sources": [{"directory": str(folder), "pads": {
                "tom1": {"mapping": {"head": {"adc": 1, "channel": 10}, "rim": {"adc": 1, "channel": 11}},
                         "physical_pad_group": "tom1", "identity_basis": "synthetic response fixture"}}}]}
            path = root / "manifest.json"
            path.write_text(json.dumps(manifest))
            trials, excluded = collect_manifest(path)
            self.assertFalse(excluded)
            self.assertTrue(all(row["extraction_threshold"] == 2 for row in trials))

    def test_single_zone_needs_only_head_and_retains_physical_pair(self):
        with tempfile.TemporaryDirectory() as temporary:
            profile = fit_session(session_fixture(Path(temporary), zones=1))
            self.assertEqual(profile["rim"], 27)
            self.assertEqual(set(profile["articulations"]), {"head"})
            self.assertIn("zone 2 0 2048 1 0\n", native_profile(profile))

    def test_head_only_activity_during_rim_take_cannot_define_rim_velocity(self):
        with tempfile.TemporaryDirectory() as temporary:
            profile = fit_session(session_fixture(Path(temporary), misdirected_rim=True))
            self.assertEqual(profile['articulations']['rim']['samples'], 8)
            self.assertEqual(profile['ignored_candidates_without_articulation_sensor_evidence']['rim'], 8)
            self.assertGreater(profile['articulations']['rim']['minimum'], profile['sensors'][1]['noise_limit'])

    def test_flat_hits_and_incomplete_articulations_do_not_replace_response(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaisesRegex(ValueError, "dynamic range"):
                fit_session(session_fixture(root / "flat", flat=True))
            with self.assertRaisesRegex(ValueError, "Complete the rimshot"):
                fit_session(session_fixture(root / "partial", missing="rimshot-varied"))

    def test_noisy_ambient_raises_threshold_and_corruption_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = session_fixture(Path(temporary), quiet_noise=3)
            profile = fit_session(folder)
            sensor = profile["sensors"][0]
            self.assertGreaterEqual(sensor["threshold"], sensor["noise_limit"] + 1)
            self.assertGreater(sensor["threshold"], 2)
            payload = next(folder.glob('*.omseq'))
            with payload.open('ab') as stream:
                stream.write(b'bad')
            with self.assertRaisesRegex(ValueError, "checksum"):
                fit_session(folder)


if __name__ == "__main__":
    unittest.main()
