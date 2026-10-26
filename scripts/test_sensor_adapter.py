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


if __name__ == '__main__':
    unittest.main()
