#!/usr/bin/env python3
"""Check temporal export/masking and leakage boundaries without owner recordings."""
from pathlib import Path
import tempfile
import unittest

import numpy as np

from compare_drum_models import check_split, training_evidence, validation_splits
from train_tensorflow_model import export, fit_model
from test_drum_model import waveform


def rows():
    result = []
    for group in ('a', 'b', 'c'):
        for session in ('1', '2', '3'):
            for label in range(4):
                signal = waveform(label)
                if label == 3:
                    signal = [waveform(1)[0], [v * .7 for v in waveform(1)[0]]]
                identity = group + session + str(label)
                result.append({'group': group, 'pad': group, 'session': session, 'label': label,
                               'zone_count': 2, 'capture_sha256': identity, 'example_id': identity,
                               'waveform': signal, 'extraction_thresholds': [2., 3.]})
    return result


class TensorFlowComparisonTests(unittest.TestCase):
    def test_whole_sessions_and_pad_boundaries(self):
        examples = rows()
        splits = list(validation_splits(examples))
        self.assertEqual(len(splits), 6)
        for kind, name, training, held in splits:
            check_split(training, held)
            if kind == 'pad':
                self.assertTrue(all(row['group'] == name for row in held))
            else:
                self.assertEqual({row['session'] for row in held}, {name})
        with self.assertRaisesRegex(ValueError, 'leaked'):
            check_split(examples, examples[:1])

    def test_training_evidence_checks_the_articulation_sensor(self):
        row = rows()[2]
        row['waveform'] = [[100.] * 32, [1.] * 32]
        self.assertFalse(training_evidence(row, 3))
        row['label'] = 1
        self.assertTrue(training_evidence(row, 3))
        row['label'] = 3
        self.assertFalse(training_evidence(row, 3))
        row['label'] = 0
        self.assertTrue(training_evidence(row, 3))

    def test_cnn_export_matches_keras_and_preserves_single_zone_gate(self):
        import contextlib
        import io
        from drum_model import shared_features
        examples = rows()
        model = fit_model(examples, epochs=2, classes=4, architecture='cnn', balance_sessions=True)
        self.assertEqual(model.input_shape, (None, 69))
        self.assertEqual(model.output_shape, (None, 4))
        values = np.asarray([shared_features(row['waveform'], False) for row in examples], dtype=np.float32)
        predictions = model(values, training=False).numpy()
        np.testing.assert_array_equal(predictions[:, 2:], 0)
        np.testing.assert_allclose(predictions.sum(axis=1), 1, atol=1e-6)
        with tempfile.TemporaryDirectory() as temporary, contextlib.redirect_stdout(io.StringIO()):
            result = export(model, Path(temporary) / 'cnn', examples)
            self.assertGreater(result['cases'], len(examples))
            self.assertLess(result['maximum_probability_difference'], 2e-5)


if __name__ == '__main__':
    unittest.main()
