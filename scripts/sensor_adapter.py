#!/usr/bin/env python3
"""Fit sensor coupling from a separate support session, outside classifier weights.

This is an offline experiment. The live engine does not apply these matrices.
NumPy provides matrix inversion/conditioning; no custom learning optimizer.
"""
from __future__ import annotations

from collections import defaultdict
import numpy as np


def support_normalize(rows, eligible):
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
        if zones == 2:
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
        report = {'support_session': sessions[0], 'zones': zones, 'matrix': matrix.tolist(),
                  'condition_number': condition, 'normalized_head_rim_prototypes': [p.tolist() for p in prototypes],
                  'support_capture_sha256': sorted({r['capture_sha256'] for r in support}),
                  'scope': 'Support-only prototype calibration; not independently verified physical strike labels.'}
        adapters[group] = report
        for row in members:
            if row['session'] == sessions[0]:
                continue
            transformed = matrix @ np.asarray(row['waveform'], dtype=np.float64)
            transformed = np.clip(transformed, -4095., 4095.)
            if zones == 1:
                transformed[1] = 0
            query.append({**row, 'waveform': transformed.tolist(), 'sensor_adapter_support_session': sessions[0]})
    support_hashes = {value for adapter in adapters.values() for value in adapter['support_capture_sha256']}
    if support_hashes & {r['capture_sha256'] for r in query}:
        raise ValueError('A support raw recording leaked into query data')
    return query, adapters
