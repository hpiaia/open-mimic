#!/usr/bin/env python3
"""Check shared-model masks, physical-pad splits and correction provenance."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from drum_model import fit, predict, save_model, shared_features
from test_drum_model import trial_files, waveform
from train_drum_model import load_trial
from train_shared_drum_model import collect_manifest, examples, partition


class SharedDrumTests(unittest.TestCase):
    def test_explicit_mask_and_shared_cpp_inference(self):
        training = [(shared_features(waveform(label, 300 + index * 70)), label)
                    for label in range(3) for index in range(8)]
        training += [(shared_features(waveform(label, 300 + index * 70), False), label)
                     for label in range(2) for index in range(8)]
        model = fit(training, epochs=100)
        self.assertEqual(model['format'], 'OMDRUM02')
        self.assertEqual(model['features'], 69)
        self.assertEqual(shared_features(waveform(1), False)[32:64], [0.0] * 32)
        self.assertEqual(shared_features(waveform(1), False)[68], 0)
        with self.assertRaises(ValueError):
            fit(training + [(shared_features(waveform(2), False), 2)], epochs=1)
        with self.assertRaises(ValueError):
            predict(model, [0.0] * 68 + [0.5])
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            save_model(model, directory / 'model.omdrum')
            executable = os.environ.get('DRUM_CLASSIFY_TOOL')
            for secondary in (True, False):
                for label in range(3 if secondary else 2):
                    samples = waveform(label, 555)
                    values = shared_features(samples, secondary)
                    expected = predict(model, values)
                    self.assertEqual(max(range(3), key=expected.__getitem__), label)
                    if not secondary:
                        self.assertEqual(expected[2], 0)
                    if executable:
                        (directory / 'waveform.txt').write_text(' '.join(str(x) for ch in samples[:2 if secondary else 1] for x in ch))
                        command = [executable, str(directory / 'model.omdrum'), str(directory / 'waveform.txt')]
                        if not secondary:
                            command.append('--single-zone')
                        actual = json.loads(subprocess.check_output(command, text=True))
                        self.assertEqual(actual['label'], ('background', 'head', 'rim')[label])
                        for first, second in zip(actual['probabilities'], expected):
                            self.assertAlmostEqual(first, second, places=5)

    def test_group_split_precedes_augmentation_and_rejects_raw_leakage(self):
        def trial(group, label, digest):
            return {'group': group, 'label': label, 'capture_sha256': digest, 'waveform': waveform(label)}
        trials = [trial(group, label, group + str(label)) for group in ('a', 'b', 'c') for label in range(3)]
        training, validation = partition(trials, 'a')
        self.assertEqual({t['group'] for t in training}, {'b', 'c'})
        self.assertEqual({t['group'] for t in validation}, {'a'})
        self.assertEqual(len(examples(training, True)), 10)
        self.assertEqual(len(examples(validation)), 3)
        with self.assertRaises(ValueError):
            partition(trials + [trial('b', 1, 'a1')], 'a')

    def test_mapping_correction_does_not_modify_raw_or_original_metadata(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = trial_files(Path(temporary) / 'capture')
            before = path.read_bytes()
            raw = path.with_suffix('.omadc').read_bytes()
            metadata = json.loads(before)
            correction = {'head': metadata['rim'], 'rim': metadata['head']}
            trial = load_trial(path, correction)
            self.assertEqual(trial['mapping'], correction)
            self.assertEqual(trial['original_mapping']['head'], metadata['head'])
            self.assertEqual(trial['waveform'][0][0], 48)
            self.assertEqual(trial['waveform'][1][0], 600)
            with self.assertRaises(ValueError):
                load_trial(path, dict(correction, phase="rim-hard"))
            self.assertEqual(path.read_bytes(), before)
            self.assertEqual(path.with_suffix('.omadc').read_bytes(), raw)

    def test_verified_archive_integrity_is_required(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            path = trial_files(directory / 'capture')
            metadata = json.loads(path.read_text())
            verified = {str(p.relative_to(directory)): hashlib.sha256(p.read_bytes()).hexdigest()
                        for p in (path, path.with_suffix('.omadc'))}
            (directory / 'download.json').write_text(json.dumps({'verified_files': verified}))
            manifest = directory / 'manifest.json'
            manifest.write_text(json.dumps({'format': 'open-mimic-shared-training-v1', 'sources': [
                {'directory': str(directory), 'pads': {'tom1': {'mapping': {'head': metadata['head'], 'rim': metadata['rim']},
                 'physical_pad_group': 'physical-a', 'identity_basis': 'synthetic fixture'}}}]}))
            trials, excluded = collect_manifest(manifest)
            self.assertEqual(len(trials), 1)
            self.assertFalse(excluded)
            path.write_text(path.read_text() + ' ')
            with self.assertRaises(ValueError):
                collect_manifest(manifest)


if __name__ == '__main__':
    unittest.main()
