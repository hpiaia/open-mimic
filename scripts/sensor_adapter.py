#!/usr/bin/env python3
"""Fit sensor coupling from a separate support session, outside classifier weights.

This is an offline experiment. The live engine does not apply these matrices.
NumPy provides matrix inversion/conditioning; no custom learning optimizer.
"""
from __future__ import annotations

from collections import defaultdict
import numpy as np


GAIN_REFERENCE = 512.


def support_normalize(rows, eligible, mode='demix', keep_support=False):
    """Reserve each pad's first session as support; transform the query sessions.

    ``demix`` inverts the head/rim coupling prototype matrix. ``gain`` only
    rescales each sensor so the 95th-percentile peak of its own articulation
    equals GAIN_REFERENCE, mirroring the response profile's per-articulation
    ``maximum`` that the live engine already stores for velocity.

    With ``keep_support`` the transformed support windows are returned too,
    tagged ``training_only_support``; they model a user's calibration recording
    entering training and must never be evaluated.
    """
    if mode not in ('demix', 'gain'):
        raise ValueError('mode must be demix or gain')
    grouped = defaultdict(list)
    for row in rows:
        grouped[row['group']].append(row)
    adapters, query = {}, []
    for group, members in sorted(grouped.items()):
        sessions = sorted({r['session'] for r in members})
        if len(sessions) < 3:
            raise ValueError('Sensor adaptation needs one support and two query sessions per pad')
        support = [r for r in members if r['session'] == sessions[0]]
        zones = {r.get('zone_count', 2) for r in members}
        if len(zones) != 1:
            raise ValueError('Sensor type changed within a physical pad group')
        zones = zones.pop()
        matrix = np.eye(2)
        prototypes = []
        condition = 1.
        if mode == 'gain':
            references = []
            for label in range(1, zones + 1):
                selected = [r for r in support if r['label'] == label and eligible[r['example_id']]]
                if len(selected) < 8:
                    raise ValueError(f'{group}: support needs eight sensor-evidenced examples of label {label}')
                peaks = sorted(max(abs(v) for v in r['waveform'][label - 1]) for r in selected)
                references.append(float(np.quantile(peaks, .95)))
            if min(references) < 8:
                raise ValueError(f'{group}: support articulation peaks are too small for gain calibration')
            matrix = np.diag([GAIN_REFERENCE / value for value in references] + [1.] * (2 - zones))
            prototypes = [np.asarray(references)]
        elif zones == 2:
            for label in (1, 2):
                selected = [r for r in support if r['label'] == label and eligible[r['example_id']]]
                if len(selected) < 8:
                    raise ValueError(f'{group}: support needs eight sensor-evidenced examples of label {label}')
                peaks = np.asarray([[max(abs(v) for v in channel) for channel in r['waveform']] for r in selected])
                unit = peaks / np.maximum(peaks.max(axis=1, keepdims=True), 1.)
                prototypes.append(np.median(unit, axis=0))
            mixing = np.column_stack(prototypes)
            condition = float(np.linalg.cond(mixing))
            if not np.isfinite(condition) or condition > 20:
                raise ValueError(f'{group}: support sensor prototypes are too similar to separate safely')
            matrix = np.linalg.inv(mixing)
            # Infinity norm <= 1 preserves the centered 12-bit input bound and
            # avoids amplifying quiet noise in absolute ADC units.
            matrix /= max(1., float(np.abs(matrix).sum(axis=1).max()))
        report = {'support_session': sessions[0], 'zones': zones, 'mode': mode, 'matrix': matrix.tolist(),
                  'condition_number': condition, 'normalized_head_rim_prototypes': [p.tolist() for p in prototypes] if mode == 'demix' else [],
                  'support_articulation_peak_references': prototypes[0].tolist() if mode == 'gain' else None,
                  'support_capture_sha256': sorted({r['capture_sha256'] for r in support}),
                  'scope': 'Support-only prototype calibration; not independently verified physical strike labels.'}
        adapters[group] = report
        for row in members:
            is_support = row['session'] == sessions[0]
            if is_support and not keep_support:
                continue
            transformed = matrix @ np.asarray(row['waveform'], dtype=np.float64)
            transformed = np.clip(transformed, -4095., 4095.)
            if zones == 1:
                transformed[1] = 0
            query.append({**row, 'waveform': transformed.tolist(), 'sensor_adapter_support_session': sessions[0],
                          'training_only_support': is_support})
    support_hashes = {value for adapter in adapters.values() for value in adapter['support_capture_sha256']}
    if support_hashes & {r['capture_sha256'] for r in query if not r['training_only_support']}:
        raise ValueError('A support raw recording leaked into query data')
    return query, adapters
