#!/usr/bin/env python3
"""Training-only sensor gain augmentation for the generic waveform classifier.

Pads differ in head/rim sensor gain (for example, one tom's rim sensor peaks at
a third of its head sensor while others peak at three times). Random per-sensor
gain copies teach the classifier not to rely on one fixed amplitude ratio. The
copies never enter evaluation, keep their source window identity and cannot
change which windows are training-eligible. Runtime input is unchanged.
"""
from __future__ import annotations

import math

import numpy as np

RELATIVE_GAIN_RANGE = (1 / 3, 3.)
OVERALL_GAIN_RANGE = (.5, 2.)


def gain_augmented_rows(rows, copies=2, seed=1729):
    if not 1 <= copies <= 8:
        raise ValueError('copies must be in 1..8')
    rng = np.random.default_rng(seed)
    result = []
    for row in rows:
        zones = row.get('zone_count', 2)
        for copy in range(copies):
            overall = math.exp(rng.uniform(*map(math.log, OVERALL_GAIN_RANGE)))
            relative = math.exp(rng.uniform(*map(math.log, RELATIVE_GAIN_RANGE))) if zones == 2 else 1.
            gains = np.asarray([overall, overall * relative])
            waveform = np.clip(np.asarray(row['waveform'], dtype=np.float64) * gains[:, None], -4095., 4095.)
            if zones == 1:
                waveform[1] = 0
            result.append({**row, 'waveform': waveform.tolist(), 'augmentation': 'sensor-gain',
                           'augmentation_gains': gains.tolist(), 'augmentation_copy': copy,
                           'example_id': f"{row['example_id']}#gain{copy}"})
    return result
