#!/usr/bin/env python3
"""Check that gain augmentation is deterministic, bounded and training-only."""
import unittest

import numpy as np

from waveform_augmentation import OVERALL_GAIN_RANGE, RELATIVE_GAIN_RANGE, gain_augmented_rows


def rows():
    return [{'example_id': 'dual', 'group': 'a', 'session': '1', 'label': 2, 'zone_count': 2,
             'waveform': [[100.] * 32, [-3000.] * 32]},
            {'example_id': 'mono', 'group': 'b', 'session': '1', 'label': 1, 'zone_count': 1,
             'waveform': [[2500.] * 32, [0.] * 32]}]


class GainAugmentationTests(unittest.TestCase):
    def test_copies_are_deterministic_bounded_and_identified(self):
        first, second = gain_augmented_rows(rows()), gain_augmented_rows(rows())
        self.assertEqual(first, second)
        self.assertEqual(len(first), 4)
        self.assertEqual({r['example_id'] for r in first}, {'dual#gain0', 'dual#gain1', 'mono#gain0', 'mono#gain1'})
        for row in first:
            waveform = np.asarray(row['waveform'])
            self.assertLessEqual(np.abs(waveform).max(), 4095)
            self.assertEqual(row['augmentation'], 'sensor-gain')
            overall, rim = row['augmentation_gains']
            self.assertTrue(OVERALL_GAIN_RANGE[0] <= overall <= OVERALL_GAIN_RANGE[1])
            if row['zone_count'] == 1:
                self.assertEqual(rim, overall)
                self.assertTrue(np.all(waveform[1] == 0))
            else:
                self.assertTrue(RELATIVE_GAIN_RANGE[0] <= rim / overall <= RELATIVE_GAIN_RANGE[1])
                self.assertAlmostEqual(waveform[0][0], 100 * overall)
        self.assertNotEqual(gain_augmented_rows(rows(), seed=1), first)
        with self.assertRaisesRegex(ValueError, 'copies'):
            gain_augmented_rows(rows(), copies=0)

    def test_source_rows_are_not_modified(self):
        source = rows()
        gain_augmented_rows(source)
        self.assertEqual(source, rows())


if __name__ == '__main__':
    unittest.main()
