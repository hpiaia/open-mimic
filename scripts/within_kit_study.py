#!/usr/bin/env python3
"""Within-kit study: how close can the owner's own kit get to 100%?

Protocol: three whole-session folds (every pad contributes two training
sessions and one held-out session). Metrics are gated recall on sensor-evidenced
held-out windows per pad and class. Variants compare one shared CNN with
pad-conditioned alternatives that the module could run because it knows which
pad slot produced the window. Nothing is installed by this command.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
import json
from pathlib import Path

import numpy as np

from compare_drum_models import training_evidence, validation_splits
from drum_model import shared_features
from train_shared_drum_model import collect_manifest, examples
from train_tensorflow_model import fit_model, make_model, tf

LABELS = ['background', 'head', 'rim', 'rimshot']


def feature_matrix(rows):
    data = examples(rows)
    return np.asarray([v for v, _ in data], dtype=np.float32), np.asarray([l for _, l in data], dtype=np.int32)


def gated(probabilities, threshold=.65):
    label = probabilities.argmax(axis=1)
    confident = probabilities[np.arange(len(label)), label] >= threshold
    return np.where((label > 0) & ~confident, 0, label)


def recalls(y, predicted):
    return [float((predicted[y == c] == c).mean()) if (y == c).any() else None for c in range(4)]


def pad_onehot_model(x, pads, classes=4):
    """CNN identical to make_model's, plus a pad one-hot concatenated before the dense layer."""
    base = make_model(x[:, :69], classes, 'cnn')
    inputs = tf.keras.Input(shape=(69 + pads,), name='sensor_features_pad')
    waveform = inputs[:, :69]
    onehot = inputs[:, 69:]
    mean = x[:, :69].mean(axis=0).astype(np.float32)
    scale = (1.0 / np.maximum(x[:, :69].std(axis=0), 0.05)).astype(np.float32)
    normalized = (waveform - mean) * scale
    w = tf.keras.layers.Reshape((2, 32))(waveform[:, :64])
    w = tf.keras.layers.Permute((2, 1))(w)
    w = tf.keras.layers.Conv1D(12, 5, padding='same', activation='relu')(w)
    w = tf.keras.layers.MaxPooling1D(2)(w)
    w = tf.keras.layers.Conv1D(16, 3, padding='same', activation='relu')(w)
    shape = tf.keras.layers.Concatenate()([tf.keras.layers.GlobalAveragePooling1D()(w), tf.keras.layers.GlobalMaxPooling1D()(w), normalized[:, 64:], onehot])
    hidden = tf.keras.layers.Dense(24, activation='relu', kernel_regularizer=tf.keras.regularizers.L2(.001))(shape)
    hidden = tf.keras.layers.Dropout(.1)(hidden)
    logits = tf.keras.layers.Dense(classes, name='classifier')(hidden)
    probabilities = tf.keras.layers.Softmax()(logits)
    gate = tf.keras.ops.concatenate([tf.keras.ops.ones_like(waveform[:, :2]), *[waveform[:, 68:69] for _ in range(classes - 2)]], axis=1)
    masked = probabilities * gate
    outputs = masked / tf.keras.ops.sum(masked, axis=1, keepdims=True)
    del base
    return tf.keras.Model(inputs, outputs)


def fit_padid(rows, pads, epochs, seed):
    tf.keras.utils.set_random_seed(seed)
    index = {pad: i for i, pad in enumerate(pads)}
    xs, ys, ws = [], [], []
    from collections import Counter
    cells = Counter((r['group'], r['session'], r['label']) for r in rows)
    class_cells = Counter(k[2] for k in cells)
    for r in rows:
        onehot = np.zeros(len(pads), np.float32)
        onehot[index[r['group']]] = 1
        weight = 1 / (4 * class_cells[r['label']] * cells[(r['group'], r['session'], r['label'])])
        for secondary in ((True, False) if r.get('zone_count', 2) == 2 and r['label'] < 2 else ((r.get('zone_count', 2) == 2),)):
            xs.append(np.concatenate([np.asarray(shared_features(r['waveform'], secondary), np.float32), onehot]))
            ys.append(r['label'])
            ws.append(weight)
    x, y, w = np.asarray(xs), np.asarray(ys, np.int32), np.asarray(ws, np.float32) * len(ys)
    model = pad_onehot_model(x, len(pads))
    model.compile(optimizer=tf.keras.optimizers.Adam(.003, epsilon=1e-8), loss=tf.keras.losses.SparseCategoricalCrossentropy())
    dataset = tf.data.Dataset.from_tensor_slices((x, y, w)).shuffle(len(y), seed=seed).batch(256)
    model.fit(dataset, epochs=epochs, verbose=0)
    return model, index


def padid_features(rows, index):
    x, y = feature_matrix(rows)
    onehot = np.zeros((len(rows), len(index)), np.float32)
    for i, r in enumerate(rows):
        onehot[i, index[r['group']]] = 1
    return np.concatenate([x, onehot], axis=1), y


def finetune_two_layers(model, rows, epochs=60, rate=.002, seed=1729):
    clone = tf.keras.models.clone_model(model)
    clone.set_weights(model.get_weights())
    tf.keras.utils.set_random_seed(seed)
    for layer in clone.layers:
        layer.trainable = layer.name in ('dense', 'classifier')
    x, y = feature_matrix(rows)
    counts = np.bincount(y, minlength=4).astype(np.float32)
    weights = (len(y) / ((counts > 0).sum() * np.maximum(counts[y], 1))).astype(np.float32)
    clone.compile(optimizer=tf.keras.optimizers.Adam(rate), loss=tf.keras.losses.SparseCategoricalCrossentropy())
    clone.fit(x, y, sample_weight=weights, batch_size=64, epochs=epochs, verbose=0, shuffle=True)
    return clone


def rimshot_consistency(rows, eligible):
    """Flag rimshot-take candidates whose rim/head peak ratio sits inside the same pad's
    pure head (<= q90) or pure rim (>= q10) range. Such a window cannot be told apart
    from a single-zone hit by any model and is treated as an ambiguous label: it is
    removed from training and reported separately, never relabeled."""
    ratio = lambda r: max(abs(v) for v in r['waveform'][1]) / max(1., max(abs(v) for v in r['waveform'][0]))
    flagged, report = set(), {}
    for pad in sorted({r['group'] for r in rows}):
        own = [r for r in rows if r['group'] == pad and eligible[r['example_id']]]
        heads = [ratio(r) for r in own if r['label'] == 1]
        rims = [ratio(r) for r in own if r['label'] == 2]
        shots = [r for r in own if r['label'] == 3]
        if not rims or not shots:
            continue
        head_limit, rim_limit = float(np.quantile(heads, .9)), float(np.quantile(rims, .1))
        bad = [r for r in shots if ratio(r) <= head_limit or ratio(r) >= rim_limit]
        flagged.update(r['example_id'] for r in bad)
        report[pad] = {'rimshot_evidenced': len(shots), 'flagged': len(bad), 'head_like': sum(ratio(r) <= head_limit for r in bad),
                       'rim_like': sum(ratio(r) >= rim_limit for r in bad), 'head_q90': head_limit, 'rim_q10': rim_limit}
    return flagged, report


def run(manifest, output, epochs, seeds, variants, clean_rimshots=False):
    if output.exists():
        raise ValueError('Choose a new output directory')
    output.mkdir(parents=True)
    rows, _ = collect_manifest(manifest)
    eligible = {r['example_id']: training_evidence(r, 3.) for r in rows}
    consistency = {}
    if clean_rimshots:
        flagged, consistency = rimshot_consistency(rows, eligible)
        for identity in flagged:
            eligible[identity] = False
        print('rimshot consistency:', json.dumps(consistency), flush=True)
    pads = sorted({r['group'] for r in rows})
    folds = [(name, training, held) for kind, name, training, held in validation_splits(rows, 3) if kind == 'session']
    report = {'format': 'open-mimic-within-kit-study-v1', 'epochs': epochs, 'seeds': seeds, 'selected_variants': variants,
              'rimshot_label_consistency': consistency or None, 'variants': defaultdict(dict), 'rimshot_errors': {}}
    for name, training, held in folds:
        train = [r for r in training if eligible[r['example_id']]]
        strong = [r for r in held if eligible[r['example_id']]]
        print(f'fold {name}: {len(train)} training, {len(strong)} evidenced held-out', flush=True)
        probabilities = {}
        tf.keras.backend.clear_session()
        shared = fit_model(train, epochs, 4, 'cnn', balance_sessions=True, seed=seeds[0])
        x, y = feature_matrix(strong)
        probabilities['shared'] = shared(x, training=False).numpy()
        ensemble = [probabilities['shared']]
        for seed in (seeds[1:] if 'ensemble' in variants else []):
            tf.keras.backend.clear_session()
            ensemble.append(fit_model(train, epochs, 4, 'cnn', balance_sessions=True, seed=seed)(x, training=False).numpy())
        if 'ensemble' in variants:
            probabilities['ensemble'] = np.mean(ensemble, axis=0)
        if 'pad_onehot' in variants:
            tf.keras.backend.clear_session()
            padid, index = fit_padid(train, pads, epochs, seeds[0])
            xp, _ = padid_features(strong, index)
            probabilities['pad_onehot'] = padid(xp, training=False).numpy()
        tf.keras.backend.clear_session()
        shared = fit_model(train, epochs, 4, 'cnn', balance_sessions=True, seed=seeds[0])
        if 'per_pad_finetune' in variants:
            probabilities['per_pad_finetune'] = np.zeros_like(probabilities['shared'])
        if 'per_pad_model' in variants:
            probabilities['per_pad_model'] = np.zeros_like(probabilities['shared'])
        for pad in pads:
            mask = np.asarray([r['group'] == pad for r in strong])
            if not mask.any():
                continue
            own = [r for r in train if r['group'] == pad]
            if 'per_pad_finetune' in variants:
                tuned = finetune_two_layers(shared, own, seed=seeds[0])
                probabilities['per_pad_finetune'][mask] = tuned(x[mask], training=False).numpy()
            if 'per_pad_model' not in variants:
                continue
            try:
                tf.keras.backend.clear_session()
                solo = fit_model(own, epochs, 4, 'cnn', balance_sessions=True, seed=seeds[0])
                probabilities['per_pad_model'][mask] = solo(x[mask], training=False).numpy()
            except ValueError as error:  # mono pads lack rim/rimshot classes
                print(f'  per-pad model skipped for {pad}: {error}', flush=True)
                probabilities['per_pad_model'][mask] = probabilities['shared'][mask]
        groups = np.asarray([r['group'] for r in strong])
        for variant, p in probabilities.items():
            g, a = gated(p), p.argmax(axis=1)
            report['variants'][variant][name] = {
                pad: {'gated_recall': recalls(y[groups == pad], g[groups == pad]), 'argmax_recall': recalls(y[groups == pad], a[groups == pad]),
                      'gated_confusion': np.array([[int(((y[groups == pad] == i) & (g[groups == pad] == j)).sum()) for j in range(4)] for i in range(4)]).tolist()}
                for pad in pads}
        # Rimshot error anatomy on the shared model.
        g = gated(probabilities['shared'])
        anatomy = {}
        for pad in pads:
            sel = [(i, r) for i, r in enumerate(strong) if r['group'] == pad and r['label'] == 3]
            if not sel:
                continue
            ratio = lambda r: max(abs(v) for v in r['waveform'][1]) / max(1., max(abs(v) for v in r['waveform'][0]))
            correct = [ratio(r) for i, r in sel if g[i] == 3]
            wrong = {LABELS[int(g[i])]: [] for i, r in sel if g[i] != 3}
            for i, r in sel:
                if g[i] != 3:
                    wrong[LABELS[int(g[i])]].append(ratio(r))
            anatomy[pad] = {'correct_median_rim_over_head': float(np.median(correct)) if correct else None,
                            'wrong': {k: {'count': len(v), 'median_rim_over_head': float(np.median(v))} for k, v in wrong.items()}}
        report['rimshot_errors'][name] = anatomy
        (output / 'evaluation.json').write_text(json.dumps(report, indent=2) + '\n')
    print_summary(report)


def print_summary(report):
    fmt = lambda v: ' / '.join('—' if x is None else f'{x:.0%}' for x in v[1:])
    for variant, folds in report['variants'].items():
        print(f'\n== {variant} (evidenced gated recall head / rim / rimshot; min over folds)')
        pads = sorted(next(iter(folds.values())).keys())
        for pad in pads:
            per = np.asarray([[np.nan if v is None else v for v in folds[f][pad]['gated_recall']] for f in folds])
            mins = np.nanmin(per, axis=0)
            means = np.nanmean(per, axis=0)
            print(f'  {pad:<6} min {fmt([None if np.isnan(v) else v for v in mins]):<22} mean {fmt([None if np.isnan(v) else v for v in means])}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--cnn-epochs', type=int, default=80)
    parser.add_argument('--seeds', type=int, nargs='+', default=[1729, 2, 3])
    parser.add_argument('--variants', nargs='+', default=['shared', 'ensemble', 'pad_onehot', 'per_pad_finetune', 'per_pad_model'])
    parser.add_argument('--clean-rimshots', action='store_true', help='Exclude rimshot labels indistinguishable from single-zone hits')
    args = parser.parse_args()
    run(args.manifest, args.output_dir, args.cnn_epochs, args.seeds, args.variants, args.clean_rimshots)


if __name__ == '__main__':
    main()
