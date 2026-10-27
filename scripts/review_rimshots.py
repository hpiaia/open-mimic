#!/usr/bin/env python3
"""Review rimshot candidates of one archived session before they train a pad model.

Rimshot takes are the least reliable labels: a stroke that misses the rim or the
head produces a window identical to a pure head or rim hit, and no model can
learn from it. This tool lists every sensor-evidenced rimshot candidate with its
rim/head peak ratio and the installed pad model's opinion, flags the suspicious
ones, and records the owner's decisions outside the verified archive in
``measurements/.reviews/<pad>/<session>.json``. Training honours exclusions;
nothing is relabeled and the recording stays untouched.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import sys

REVIEW_FORMAT = 'open-mimic-label-review-v1'


def review_path(measurements: Path, pad: str, session: str) -> Path:
    return measurements / '.reviews' / pad / (session + '.json')


def load_reviews(measurements: Path) -> dict[str, dict]:
    """Map excluded example_id -> reason across every stored review."""
    excluded = {}
    for path in sorted((measurements / '.reviews').glob('*/*.json')) if (measurements / '.reviews').exists() else []:
        document = json.loads(path.read_text())
        if document.get('format') != REVIEW_FORMAT:
            raise ValueError(f'Unsupported review file: {path}')
        for item in document.get('excluded', []):
            excluded[item['example_id']] = item.get('reason', 'excluded by review')
    return excluded


def apply_reviews(rows, measurements: Path):
    excluded = load_reviews(measurements)
    kept = [r for r in rows if r['example_id'] not in excluded]
    return kept, len(rows) - len(kept)


def ratio(row) -> float:
    return max(abs(v) for v in row['waveform'][1]) / max(1., max(abs(v) for v in row['waveform'][0]))


def candidates(session_dir: Path):
    from calibrate_pad import noise_profile, verified_session
    from compare_drum_models import training_evidence
    from sequence_training import load_sequence_trials
    records, _ = verified_session(session_dir)
    if 'rimshot-varied' not in records:
        raise ValueError('This session has no rimshot take')
    sensors = noise_profile(*records['ambient'])['sensors'] if 'ambient' in records else None
    path, metadata = records['rimshot-varied']
    rows = [r for r in load_sequence_trials(path, sensor_calibration=sensors) if r['label'] == 3]
    head = [ratio(r) for r in load_sequence_trials(records['head-varied'][0], sensor_calibration=sensors) if r['label'] == 1 and training_evidence(r, 3.)] if 'head-varied' in records else []
    rim = [ratio(r) for r in load_sequence_trials(records['rim-varied'][0], sensor_calibration=sensors) if r['label'] == 2 and training_evidence(r, 3.)] if 'rim-varied' in records else []
    return metadata, [r for r in rows if training_evidence(r, 3.)], head, rim


def predictions(model_path: Path, rows):
    import numpy as np
    from drum_model import shared_features
    from train_tensorflow_model import tf
    runtime = tf.lite.Interpreter(model_path=str(model_path), num_threads=1)
    runtime.allocate_tensors()
    inputs, outputs = runtime.get_input_details()[0], runtime.get_output_details()[0]
    result = []
    for row in rows:
        runtime.set_tensor(inputs['index'], np.asarray([shared_features(row['waveform'], True)], np.float32))
        runtime.invoke()
        result.append(runtime.get_tensor(outputs['index'])[0].tolist())
    return result


def quantile(values, q):
    if not values:
        return None
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(round(q * (len(ordered) - 1))))]


def assess(rows, probabilities, head_ratios, rim_ratios):
    labels = ['background', 'head', 'rim', 'rimshot']
    head_limit, rim_limit = quantile(head_ratios, .9), quantile(rim_ratios, .1)
    items = []
    for index, row in enumerate(rows):
        value = ratio(row)
        reasons = []
        if head_limit is not None and value <= head_limit:
            reasons.append(f'rim/head {value:.2f} is inside this pad\'s pure-head range (<= {head_limit:.2f})')
        if rim_limit is not None and value >= rim_limit:
            reasons.append(f'rim/head {value:.2f} is inside this pad\'s pure-rim range (>= {rim_limit:.2f})')
        opinion = None
        if probabilities:
            top = max(range(4), key=probabilities[index].__getitem__)
            opinion = {'label': labels[top], 'probability': probabilities[index][top]}
            if top != 3:
                reasons.append(f'pad model says {labels[top]} ({probabilities[index][top]:.0%})')
        items.append({'index': index, 'example_id': row['example_id'], 'frame': row['source_frame_start'],
                      'segment': row['segment_index'], 'rim_over_head': value,
                      'peaks': [max(abs(v) for v in row['waveform'][c]) for c in range(2)],
                      'model': opinion, 'flags': reasons})
    return items, {'head_q90': head_limit, 'rim_q10': rim_limit}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('session', type=Path, help='Archived session directory: measurements/<pad>/<session>')
    parser.add_argument('--measurements', type=Path, default=Path(__file__).resolve().parents[1] / 'measurements')
    parser.add_argument('--model', type=Path, help='LiteRT model used for opinions (default: the pad\'s installed model)')
    parser.add_argument('--exclude-flagged', action='store_true', help='Record every flagged candidate as excluded without prompting')
    parser.add_argument('--interactive', action='store_true', help='Ask keep/exclude for each flagged candidate')
    args = parser.parse_args()
    session_dir = args.session.resolve()
    pad, session = session_dir.parent.name, session_dir.name
    try:
        metadata, rows, head_ratios, rim_ratios = candidates(session_dir)
        if metadata['pad'] != pad or metadata['session_id'] != session:
            raise ValueError('Session directory does not match its metadata')
        model = args.model
        if model is None:
            latest = args.measurements / '.models' / 'pads' / (pad + '.json')
            if latest.exists():
                model = Path(json.loads(latest.read_text())['tflite'])
        probabilities = predictions(model, rows) if model and model.exists() else []
        items, limits = assess(rows, probabilities, head_ratios, rim_ratios)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f'Review failed: {error}\n')
    flagged = [item for item in items if item['flags']]
    print(f'{pad}/{session[:8]}: {len(items)} evidenced rimshot candidates, {len(flagged)} flagged'
          + (f' (model: {model})' if probabilities else ' (no pad model opinion)'))
    print(f"pure-head ratio <= {limits['head_q90']}, pure-rim ratio >= {limits['rim_q10']}")
    for item in items:
        mark = '!' if item['flags'] else ' '
        opinion = f"{item['model']['label']} {item['model']['probability']:.0%}" if item['model'] else '—'
        print(f"{mark} #{item['index']:<3} frame {item['frame']:<7} rim/head {item['rim_over_head']:6.2f}  peaks {item['peaks'][0]:6.0f}/{item['peaks'][1]:<6.0f} model {opinion:<14} {'; '.join(item['flags'])}")
    decisions = []
    if args.interactive:
        for item in flagged:
            answer = input(f"Exclude candidate #{item['index']} ({'; '.join(item['flags'])})? [y/N] ").strip().lower()
            if answer == 'y':
                decisions.append(item)
    elif args.exclude_flagged:
        decisions = flagged
    if not decisions:
        print('No exclusions recorded. Use --interactive or --exclude-flagged to record decisions.')
        return 0
    target = review_path(args.measurements, pad, session)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps({'format': REVIEW_FORMAT, 'pad': pad, 'session': session,
                                  'utc': datetime.now(timezone.utc).isoformat(), 'model': str(model) if probabilities else None,
                                  'limits': limits, 'candidates': len(items),
                                  'excluded': [{'example_id': item['example_id'], 'reason': '; '.join(item['flags'])} for item in decisions]},
                                 indent=2) + '\n')
    print(f'Recorded {len(decisions)} exclusion(s) in {target}. Rerun the receiver to re-adapt {pad}.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
