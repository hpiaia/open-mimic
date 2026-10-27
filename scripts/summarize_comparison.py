#!/usr/bin/env python3
"""Summarize a model-comparison run against a matched control run.

Checks that both runs evaluated the same windows and fold sizes before printing
gated recall. Development comparison only; no promotion decision is made here.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path


def load(directory):
    evaluation = json.loads((directory / 'evaluation.json').read_text())
    provenance = json.loads((directory / 'provenance.json').read_text())
    return evaluation, provenance


def identity(rows):
    return {r['example_id']: (r['capture_sha256'], r['group'], r['session'], r['label'], r['positive_training_evidence']) for r in rows}


def fmt(values):
    return ' / '.join('—' if v is None else f'{v:.1%}' for v in values)


def summarize(candidate, control):
    c_eval, c_prov = load(candidate)
    k_eval, k_prov = load(control)
    if identity(c_prov) != identity(k_prov):
        raise ValueError('Candidate and control evaluated different query windows')
    print(f'Identical {len(c_prov):,} query windows. Candidate: {c_eval["protocol"].get("calibration_adapter")} / '
          f'{c_eval["protocol"].get("training_augmentation", "none")}; control: {k_eval["protocol"].get("calibration_adapter")} / '
          f'{k_eval["protocol"].get("training_augmentation", "none")}')
    for c_model in c_eval['models']:
        k_model = next(m for m in k_eval['models'] if m['architecture'] == c_model['architecture'])
        print(f"\n{c_model['architecture']}: pad macro {k_model['mean_pad_gated_macro_recall']:.1%} -> {c_model['mean_pad_gated_macro_recall']:.1%}; "
              f"session macro {k_model['mean_session_gated_macro_recall']:.1%} -> {c_model['mean_session_gated_macro_recall']:.1%}; "
              f"gate passed: {c_model['numerical_promotion_gate_passed']}")
        for c_fold, k_fold in zip(c_model['folds'], k_model['folds']):
            if (c_fold['kind'], c_fold['name'], c_fold['held_out_windows']) != (k_fold['kind'], k_fold['name'], k_fold['held_out_windows']):
                raise ValueError('Fold mismatch between runs')
            for subset in ('all', 'training_eligible_subset'):
                tag = 'all' if subset == 'all' else 'evidenced'
                print(f"  {c_fold['kind']} {c_fold['name']:<6} {tag:<9} control {fmt(k_fold[subset]['live_confidence_gate']['recall']):<36} "
                      f"candidate {fmt(c_fold[subset]['live_confidence_gate']['recall'])}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('control', type=Path)
    args = parser.parse_args()
    summarize(args.candidate, args.control)


if __name__ == '__main__':
    main()
