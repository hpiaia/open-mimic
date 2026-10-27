#!/usr/bin/env python3
"""Report per-fold spread of gated recall across repeated runs that differ only by seed."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('runs', type=Path, nargs='+')
    parser.add_argument('--architecture', default='cnn')
    args = parser.parse_args()
    evaluations = [json.loads((run / 'evaluation.json').read_text()) for run in args.runs]
    seeds = [e['protocol']['seed'] for e in evaluations]
    models = [next(m for m in e['models'] if m['architecture'] == args.architecture) for e in evaluations]
    print(f'{args.architecture}: seeds {seeds}')
    print('pad macro', [round(m['mean_pad_gated_macro_recall'], 3) for m in models],
          'session macro', [round(m['mean_session_gated_macro_recall'], 3) for m in models])
    spreads = []
    for index, fold in enumerate(models[0]['folds']):
        recalls = np.asarray([[np.nan if v is None else v for v in m['folds'][index]['training_eligible_subset']['live_confidence_gate']['recall']]
                              for m in models])
        low, high = np.nanmin(recalls, axis=0), np.nanmax(recalls, axis=0)
        spread = high - low
        spreads.append(np.nanmax(spread))
        cells = ' / '.join('—' if np.isnan(l) else f'{l:.0%}-{h:.0%}' for l, h in zip(low, high))
        print(f"  {fold['kind']} {fold['name']:<6} evidenced min-max {cells}")
    print(f'largest per-class spread in any fold: {max(spreads):.1%}')


if __name__ == '__main__':
    main()
