#!/usr/bin/env python3
"""Check manifest discovery, the pad model sidecar contract and adaptation ordering."""
import json
from pathlib import Path
import tempfile
import unittest

from pad_model_adaptation import build_manifest, sidecar, source_identity
from review_rimshots import REVIEW_FORMAT, apply_reviews, assess, review_path
from receive_pad_measurements import Receiver
from test_pad_response import session_fixture


class PadModelAdaptationTests(unittest.TestCase):
    def test_manifest_lists_only_released_sessions_with_their_recorded_mapping(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            folder = session_fixture(root)
            with self.assertRaisesRegex(ValueError, 'No archived'):
                build_manifest(root)
            report = json.loads((folder / 'download.json').read_text())
            report['device_captures_removed'] = True
            (folder / 'download.json').write_text(json.dumps(report))
            manifest = build_manifest(root)
            self.assertEqual(manifest['format'], 'open-mimic-shared-training-v1')
            self.assertEqual(len(manifest['sources']), 1)
            source = manifest['sources'][0]
            self.assertEqual(source['directory'], str(folder.resolve()))
            self.assertEqual(source['pads']['tom1']['mapping'], {'head': {'adc': 1, 'channel': 10}, 'rim': {'adc': 1, 'channel': 11}})

    def test_sidecar_contract_and_source_identity(self):
        text = sidecar('tom4', 'a' * 64, 'b' * 64, 'c' * 64, (0.65, 0.6, 0.55))
        self.assertEqual(text.splitlines(), ['OPEN_MIMIC_PAD_MODEL 1', 'pad tom4', 'base ' + 'a' * 64, 'source ' + 'b' * 64,
                                             'sha256 ' + 'c' * 64, 'gate 0.65 0.6 0.55'])
        with self.assertRaisesRegex(ValueError, 'gate'):
            sidecar('tom4', 'a' * 64, 'b' * 64, 'c' * 64, (1.5, 0.6, 0.6))
        with self.assertRaisesRegex(ValueError, 'hex'):
            sidecar('tom4', 'A' * 64, 'b' * 64, 'c' * 64, (0.6, 0.6, 0.6))
        rows = [{'capture_sha256': 'x'}, {'capture_sha256': 'y'}, {'capture_sha256': 'x'}]
        self.assertEqual(source_identity(rows, 'a' * 64), source_identity(list(reversed(rows)), 'a' * 64))
        self.assertNotEqual(source_identity(rows, 'a' * 64), source_identity(rows, 'b' * 64))

    def test_adaptation_runs_after_the_archive_acknowledgment_and_never_blocks_it(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            folder = session_fixture(root)
            report = json.loads((folder / 'download.json').read_text())
            receiver = Receiver('root@module', root, Path('/usr/bin/false'), True)
            calls = []
            receiver.response_result = lambda *args, **kwargs: calls.append('profile')
            receiver.command = lambda *args: calls.append(args[0])
            receiver.remote = lambda *args, **kwargs: 'ack'
            receiver.adapt_model = lambda pad, destination, document: calls.append(('adapt', pad, document['device_captures_removed']))
            receiver.acknowledge('tom1', 'a' * 32, folder, report)
            self.assertEqual(calls, ['profile', 'archive-ack', ('adapt', 'tom1', True)])
            disabled = Receiver('root@module', root, None, True)
            disabled.adapt_model('tom1', folder, report)  # no TensorFlow interpreter: silently skipped

    def test_label_reviews_exclude_only_listed_windows(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            rows = [{'example_id': 'keep'}, {'example_id': 'drop'}]
            self.assertEqual(apply_reviews(rows, root), (rows, 0))
            target = review_path(root, 'tom1', 'a' * 32)
            target.parent.mkdir(parents=True)
            target.write_text(json.dumps({'format': REVIEW_FORMAT, 'excluded': [{'example_id': 'drop', 'reason': 'test'}]}))
            kept, removed = apply_reviews(rows, root)
            self.assertEqual(([r['example_id'] for r in kept], removed), (['keep'], 1))
            target.write_text(json.dumps({'format': 'other'}))
            with self.assertRaisesRegex(ValueError, 'Unsupported'):
                apply_reviews(rows, root)

    def test_assessment_flags_single_zone_lookalikes_and_model_disagreement(self):
        rows = [{'example_id': str(i), 'source_frame_start': i, 'segment_index': 0,
                 'waveform': [[100.] * 32, [v] * 32]} for i, v in enumerate((5., 70., 400.))]
        probabilities = [[0, 0, 0, 1], [0, 0.9, 0, 0.1], [0, 0, 0, 1]]
        items, limits = assess(rows, probabilities, head_ratios=[0.05, 0.1, 0.12], rim_ratios=[2.0, 3.0, 4.0])
        self.assertEqual(limits, {'head_q90': 0.12, 'rim_q10': 2.0})
        self.assertTrue(any('pure-head' in f for f in items[0]['flags']))
        self.assertTrue(any('pad model says head' in f for f in items[1]['flags']))
        self.assertTrue(any('pure-rim' in f for f in items[2]['flags']))
        self.assertEqual(items[1]['model'], {'label': 'head', 'probability': 0.9})


if __name__ == '__main__':
    unittest.main()
