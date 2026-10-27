#!/usr/bin/env python3
"""Verify calibration/query isolation, coupling correction and numerical guards."""
import copy
import unittest

import numpy as np

from sensor_adapter import support_normalize


def fixture(collinear=False):
    rows = []
    mixing = np.asarray([[1., 1.], [.07, .07 if collinear else .35]])
    for session in ('1', '2', '3'):
        for label in (1, 2):
            for index in range(8):
                raw = mixing[:, label - 1:label] @ np.full((1, 32), 300. + index)
                identity = session + str(label) + str(index)
                rows.append({'group': 'tom', 'session': session, 'label': label, 'zone_count': 2,
                             'example_id': identity, 'capture_sha256': session + str(label), 'waveform': raw.tolist()})
    return rows


class SensorAdapterTests(unittest.TestCase):
    def test_support_corrects_coupling_and_never_enters_query(self):
        rows = fixture()
        query, adapters = support_normalize(rows, {r['example_id']: True for r in rows})
        self.assertEqual({r['session'] for r in query}, {'2', '3'})
        self.assertEqual(adapters['tom']['support_session'], '1')
        matrix = np.asarray(adapters['tom']['matrix'])
        self.assertLessEqual(np.abs(matrix).sum(axis=1).max(), 1.000001)
        for row in query:
            inactive = 2 - row['label']
            np.testing.assert_allclose(row['waveform'][inactive], 0, atol=1e-8)
        changed = copy.deepcopy(rows)
        for row in changed:
            if row['session'] != '1':
                row['waveform'] = np.full((2, 32), 4000).tolist()
        _, other = support_normalize(changed, {r['example_id']: True for r in rows})
        self.assertEqual(adapters, other)

    def test_unseparable_or_insufficient_support_is_rejected(self):
        rows = fixture(collinear=True)
        with self.assertRaisesRegex(ValueError, 'too similar'):
            support_normalize(rows, {r['example_id']: True for r in rows})
        rows = fixture()
        with self.assertRaisesRegex(ValueError, 'eight'):
            support_normalize(rows, {r['example_id']: False for r in rows})

    def test_gain_mode_equalizes_articulation_peaks_from_support_only(self):
        rows = fixture()
        query, adapters = support_normalize(rows, {r['example_id']: True for r in rows}, mode='gain')
        self.assertEqual(adapters['tom']['mode'], 'gain')
        matrix = np.asarray(adapters['tom']['matrix'])
        self.assertEqual(matrix[0, 1], 0)
        self.assertEqual(matrix[1, 0], 0)
        references = adapters['tom']['support_articulation_peak_references']
        # Head support peaks are 300..307 and rim support peaks .35 * (300..307).
        self.assertAlmostEqual(references[0], np.quantile(range(300, 308), .95))
        self.assertAlmostEqual(references[1], .35 * np.quantile(range(300, 308), .95))
        for row in query:
            peak = max(abs(v) for v in row['waveform'][row['label'] - 1])
            self.assertLess(abs(peak - 512) / 512, .03)
        changed = copy.deepcopy(rows)
        for row in changed:
            if row['session'] != '1':
                row['waveform'] = np.full((2, 32), 4000).tolist()
        _, other = support_normalize(changed, {r['example_id']: True for r in rows}, mode='gain')
        self.assertEqual(adapters, other)
        with self.assertRaisesRegex(ValueError, 'mode'):
            support_normalize(rows, {r['example_id']: True for r in rows}, mode='other')


if __name__ == '__main__':
    unittest.main()
