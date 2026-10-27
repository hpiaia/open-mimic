#!/usr/bin/env python3
"""Compare TensorFlow linear/CNN classifiers with whole-pad and session splits.

All held-out windows remain in evaluation, including weak/ambiguous candidates.
Positive training labels require sensor evidence; excluded candidates are not
relabeled as silence. No model is installed by this command.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import statistics

import numpy as np

from train_tensorflow_model import LABELS, evaluate, examples, export, fit_model, tf
from train_shared_drum_model import collect_manifest, partition


def training_evidence(row, minimum_ratio):
    if row['label'] == 0:
        return True
    thresholds = row.get('extraction_thresholds')
    if not thresholds or len(thresholds) != 2 or any(value < 2 for value in thresholds):
        raise ValueError('Comparison requires sensor thresholds from verified continuous recordings')
    ratios = [max(abs(value) for value in row['waveform'][c]) / thresholds[c] for c in range(2)]
    if row['label'] < 3:
        return ratios[row['label'] - 1] >= minimum_ratio
    return min(ratios) >= 1 and max(ratios) >= minimum_ratio


def check_split(training, held):
    if not training or not held:
        raise ValueError('Empty training or held-out partition')
    for field in ('capture_sha256', 'example_id'):
        if {r[field] for r in training} & {r[field] for r in held}:
            raise ValueError(f'{field} leaked across validation boundary')
    if {(r['group'], r['session']) for r in training} & {(r['group'], r['session']) for r in held}:
        raise ValueError('Physical pad session leaked across validation boundary')


def validation_splits(rows, session_folds=3):
    # Calibration-support windows (few-shot protocol) train in every fold and are never held out.
    support = [r for r in rows if r.get('training_only_support')]
    rows = [r for r in rows if not r.get('training_only_support')]
    for group in sorted({r['group'] for r in rows}):
        training, held = partition(rows, group)
        training += support
        check_split(training, held)
        yield 'pad', group, training, held
    sessions = {group: sorted({r['session'] for r in rows if r['group'] == group})
                for group in sorted({r['group'] for r in rows})}
    if any(len(values) < session_folds for values in sessions.values()):
        raise ValueError(f'Session evaluation requires at least {session_folds} query sessions per pad')
    # Lexical IDs define a deterministic fold; no window-level random split.
    for index in range(session_folds):
        selected = {(group, values[index]) for group, values in sessions.items()}
        training = [r for r in rows if (r['group'], r['session']) not in selected] + support
        held = [r for r in rows if (r['group'], r['session']) in selected]
        check_split(training, held)
        yield 'session', str(index + 1), training, held


def classification_metrics(model, rows):
    result = evaluate(model, rows)
    values = examples(rows)
    probabilities = model(np.asarray([v for v, _ in values], dtype=np.float32), training=False).numpy()
    classes = probabilities.shape[1]
    confusion = np.zeros((classes, classes), dtype=int)
    for row, prediction in zip(rows, probabilities):
        label = int(prediction.argmax())
        emitted = label if label > 0 and prediction[label] >= .65 else 0
        confusion[row['label'], emitted] += 1
    recalls = [float(confusion[c, c] / confusion[c].sum()) if confusion[c].sum() else None for c in range(classes)]
    result['live_confidence_gate'] = {
        'threshold': .65, 'confusion_rows_guided_columns_output_zero_means_no_event': confusion.tolist(),
        'recall': recalls, 'present_class_macro_recall': statistics.mean(v for v in recalls if v is not None),
        'scope': 'Window-level output gate only; not continuous event accuracy or false-trigger rate.'}
    return result


def augmented(training, augmentation):
    if augmentation == 'none':
        return training
    from waveform_augmentation import gain_augmented_rows
    return training + gain_augmented_rows(training)


def run(manifest, output, minimum_ratio=3., cnn_epochs=80, linear_epochs=400, calibration_adapter='none', augmentation='none', few_shot=False, seed=1729):
    if output.exists():
        raise ValueError('Choose a new output directory')
    output.mkdir(parents=True)
    config = {'format': 'open-mimic-model-comparison-v1', 'minimum_positive_training_threshold_ratio': minimum_ratio,
              'epochs': {'linear': linear_epochs, 'cnn': cnn_epochs}, 'seed': seed,
              'few_shot_support_in_training': few_shot,
              'balance': 'equal class mass, then equal mass per pad/session/class cell',
              'window_frames': 32, 'manifest_sha256': hashlib.sha256(manifest.read_bytes()).hexdigest(),
              'calibration_adapter': calibration_adapter, 'training_augmentation': augmentation,
              'runtime_input_compatible': calibration_adapter not in ('sensor-demix', 'sensor-gain'),
              'promotion_gate': 'Each pad: gated head/rim/rimshot recall >= 0.85 where present, background >= 0.98; session macro recall >= 0.90. Physical validation still required.',
              'validation_scope': 'Development folds; previously inspected captures are not a pristine independent release test.'}
    (output / 'protocol.json').write_text(json.dumps(config, indent=2) + '\n')
    print('Verifying recordings and extracting corrected quiet windows...', flush=True)
    rows, excluded = collect_manifest(manifest)
    if max(r['label'] for r in rows) != 3:
        raise ValueError('Comparison requires all four classes')
    eligible = {r['example_id']: training_evidence(r, minimum_ratio) for r in rows}
    if few_shot and calibration_adapter == 'none':
        raise ValueError('Few-shot support training requires a support/query calibration protocol')
    if calibration_adapter != 'none':
        from sensor_adapter import support_normalize
        original_waveforms = {r['example_id']: r['waveform'] for r in rows}
        rows, adapters = support_normalize(rows, eligible, 'gain' if calibration_adapter == 'sensor-gain' else 'demix', keep_support=few_shot)
        if calibration_adapter == 'support-control':
            rows = [{**r, 'waveform': original_waveforms[r['example_id']]} for r in rows]
        for adapter in adapters.values():
            adapter['applied'] = calibration_adapter != 'support-control'
        (output / 'sensor-adapters.json').write_text(json.dumps(adapters, indent=2) + '\n')
    splits = list(validation_splits(rows, 3 if calibration_adapter == 'none' else 2))
    support = [r for r in rows if r.get('training_only_support')]
    rows = [r for r in rows if not r.get('training_only_support')]
    (output / 'provenance.json').write_text(json.dumps([
        {**{k: v for k, v in r.items() if k not in ('features', 'waveform')},
         'positive_training_evidence': eligible[r['example_id']]} for r in rows], indent=2) + '\n')
    if support:
        (output / 'support-training-provenance.json').write_text(json.dumps([
            {**{k: v for k, v in r.items() if k not in ('features', 'waveform')},
             'positive_training_evidence': eligible[r['example_id']]} for r in support], indent=2) + '\n')
    summary = {'real_windows': len(rows), 'classes': dict(Counter(LABELS[r['label']] for r in rows)),
               'training_eligible': sum(eligible[r['example_id']] for r in rows), 'excluded_by_extractor': excluded,
               'training_only_support_windows': len(support)}
    (output / 'dataset.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary), flush=True)
    reports = []
    for architecture, epochs in config['epochs'].items():
        folds = []
        for kind, name, training, held in splits:
            selected = augmented([r for r in training if eligible[r['example_id']]], augmentation)
            print(f'Training {architecture}: {kind} holdout {name}, {len(selected)} training / {len(held)} held-out windows', flush=True)
            tf.keras.backend.clear_session()
            model = fit_model(selected, epochs, 4, architecture, balance_sessions=True, seed=seed)
            strong = [r for r in held if eligible[r['example_id']]]
            weak = [r for r in held if not eligible[r['example_id']]]
            fold = {'kind': kind, 'name': name, 'training_windows': len(selected), 'held_out_windows': len(held),
                    'all': classification_metrics(model, held),
                    'training_eligible_subset': classification_metrics(model, strong),
                    'ambiguous_subset': classification_metrics(model, weak) if weak else None,
                    'by_pad': {pad: classification_metrics(model, [r for r in held if r['group'] == pad])
                               for pad in sorted({r['group'] for r in held})}}
            folds.append(fold)
            (output / f'{architecture}-{kind}-{name}.json').write_text(json.dumps(fold, indent=2) + '\n')
            print(json.dumps({'architecture': architecture, 'kind': kind, 'name': name,
                              'gated_recall': fold['all']['live_confidence_gate']['recall']}), flush=True)
        tf.keras.backend.clear_session()
        model = fit_model(augmented([r for r in rows + support if eligible[r['example_id']]], augmentation), epochs, 4, architecture, balance_sessions=True, seed=seed)
        conversion = export(model, output / architecture, rows)
        pad_folds = [f for f in folds if f['kind'] == 'pad']
        session_folds = [f for f in folds if f['kind'] == 'session']
        pad_pass = all(all(value >= (.98 if c == 0 else .85) for c, value in enumerate(
            f['all']['live_confidence_gate']['recall']) if value is not None) for f in pad_folds)
        session_pass = all(f['all']['live_confidence_gate']['present_class_macro_recall'] >= .90 for f in session_folds)
        report = {'architecture': architecture, 'parameters': model.count_params(), 'folds': folds,
                  'conversion': conversion, 'numerical_promotion_gate_passed': pad_pass and session_pass,
                  'physical_validation_required': True, 'promoted': False,
                  'runtime_input_compatible': config['runtime_input_compatible'],
                  'mean_pad_gated_macro_recall': statistics.mean(f['all']['live_confidence_gate']['present_class_macro_recall'] for f in pad_folds),
                  'mean_session_gated_macro_recall': statistics.mean(f['all']['live_confidence_gate']['present_class_macro_recall'] for f in session_folds),
                  'final_training_metrics_not_validation': classification_metrics(model, rows)}
        reports.append(report)
        (output / f'{architecture}-evaluation.json').write_text(json.dumps(report, indent=2) + '\n')
    (output / 'evaluation.json').write_text(json.dumps({'protocol': config, 'dataset': summary, 'models': reports}, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--minimum-training-ratio', type=float, default=3.)
    parser.add_argument('--cnn-epochs', type=int, default=80)
    parser.add_argument('--linear-epochs', type=int, default=400)
    parser.add_argument('--calibration-adapter', choices=('none', 'sensor-demix', 'sensor-gain', 'support-control'), default='none')
    parser.add_argument('--augmentation', choices=('none', 'sensor-gain'), default='none')
    parser.add_argument('--few-shot-support', action='store_true',
                        help='Also train on each pad\'s reserved support session (never evaluated)')
    parser.add_argument('--seed', type=int, default=1729)
    args = parser.parse_args()
    if not np.isfinite(args.minimum_training_ratio) or args.minimum_training_ratio < 1:
        parser.error('minimum-training-ratio must be finite and >= 1')
    if not all(1 <= value <= 10000 for value in (args.cnn_epochs, args.linear_epochs)):
        parser.error('epochs must be in 1..10000')
    run(args.manifest, args.output_dir, args.minimum_training_ratio, args.cnn_epochs, args.linear_epochs, args.calibration_adapter, args.augmentation, args.few_shot_support, args.seed)


if __name__ == '__main__':
    main()
