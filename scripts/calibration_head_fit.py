#!/usr/bin/env python3
"""Base model + on-device-style last-layer calibration experiment.

For each pad: train the CNN base on every other pad (the pad is never seen),
freeze it, then refit only the final softmax layer on the pad's reserved
calibration (support) session using plain gradient descent on the frozen
24-value embedding. Evaluate on the pad's two remaining sessions. This is the
product path (factory base model, calibration on the module) measured offline.
No model is installed by this command.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
import json
from pathlib import Path

import numpy as np

from compare_drum_models import augmented, classification_metrics, training_evidence
from train_shared_drum_model import collect_manifest, examples
from train_tensorflow_model import fit_model, tf


def split_support(rows):
    grouped = defaultdict(list)
    for row in rows:
        grouped[row['group']].append(row)
    support, query = [], []
    for members in grouped.values():
        first = sorted({r['session'] for r in members})[0]
        for row in members:
            (support if row['session'] == first else query).append(row)
    return support, query


def embedding_model(model):
    hidden = model.get_layer('dense')  # Dense(24) feeding dropout -> classifier
    return tf.keras.Model(model.input, hidden.output)


def embed(encoder, rows):
    data = examples(rows, augment=True)
    x = np.asarray([v for v, _ in data], dtype=np.float32)
    y = np.asarray([label for _, label in data], dtype=np.int32)
    mask = x[:, 68] > .5
    return encoder(x, training=False).numpy().astype(np.float64), y, mask


def gated_softmax(logits, mask):
    logits = logits - logits.max(axis=1, keepdims=True)
    p = np.exp(logits)
    p[~mask, 2:] = 0
    return p / p.sum(axis=1, keepdims=True)


def fit_head(embeddings, labels, mask, weight0, bias0, steps=400, rate=.05, anchor=.01):
    """Balanced-class gradient descent on the last layer, anchored to the base head."""
    weight, bias = weight0.copy(), bias0.copy()
    counts = np.bincount(labels, minlength=4).astype(np.float64)
    present = counts > 0
    sample_weight = np.where(present[labels], 1. / np.maximum(counts[labels], 1), 0.) / present.sum()
    for _ in range(steps):
        p = gated_softmax(embeddings @ weight + bias, mask)
        error = p.copy()
        error[np.arange(len(labels)), labels] -= 1
        error *= sample_weight[:, None]
        grad_w = embeddings.T @ error + anchor * (weight - weight0)
        grad_b = error.sum(axis=0)
        weight -= rate * grad_w
        bias -= rate * grad_b
    return weight, bias


def fit_two_layers(base, calibration, epochs=60, rate=.002, seed=1729):
    """Keras fine-tune of only the Dense(24) and classifier layers on calibration rows (base frozen)."""
    tf.keras.utils.set_random_seed(seed)
    for layer in base.layers:
        layer.trainable = layer.name in ('dense', 'classifier')
    data = examples(calibration, augment=True)
    x = np.asarray([v for v, _ in data], dtype=np.float32)
    y = np.asarray([label for _, label in data], dtype=np.int32)
    counts = np.bincount(y, minlength=4).astype(np.float32)
    weights = (len(y) / ((counts > 0).sum() * counts[y])).astype(np.float32)
    base.compile(optimizer=tf.keras.optimizers.Adam(rate), loss=tf.keras.losses.SparseCategoricalCrossentropy())
    base.fit(x, y, sample_weight=weights, batch_size=64, epochs=epochs, verbose=0, shuffle=True)
    return base


class HeadModel:
    """Minimal callable mirroring the Keras model interface for classification_metrics."""
    def __init__(self, encoder, weight, bias):
        self.encoder, self.weight, self.bias = encoder, weight, bias
        self.output_shape = (None, 4)

    def __call__(self, x, training=False):
        x = np.asarray(x, dtype=np.float32)
        logits = self.encoder(x, training=False).numpy().astype(np.float64) @ self.weight + self.bias
        return tf.constant(gated_softmax(logits, x[:, 68] > .5).astype(np.float32))


def run(manifest, output, epochs=80, seed=1729, augmentation='none', layers='head', anchor=.01, steps=400):
    if output.exists():
        raise ValueError('Choose a new output directory')
    output.mkdir(parents=True)
    rows, _ = collect_manifest(manifest)
    eligible = {r['example_id']: training_evidence(r, 3.) for r in rows}
    support, query = split_support(rows)
    report = {'format': 'open-mimic-calibration-head-study-v1', 'seed': seed, 'cnn_epochs': epochs,
              'base_augmentation': augmentation, 'calibrated_layers': layers, 'anchor': anchor, 'steps': steps,
              'query_windows': len(query), 'support_windows': len(support), 'pads': {}}
    for pad in sorted({r['group'] for r in rows}):
        base_rows = [r for r in rows if r['group'] != pad and eligible[r['example_id']]]
        held = [r for r in query if r['group'] == pad]
        calibration = [r for r in support if r['group'] == pad and eligible[r['example_id']]]
        assert not ({r['capture_sha256'] for r in base_rows} | {r['capture_sha256'] for r in calibration}) & {r['capture_sha256'] for r in held}
        print(f'{pad}: base on {len(base_rows)} windows from other pads, calibrate head on {len(calibration)}, evaluate {len(held)}', flush=True)
        tf.keras.backend.clear_session()
        base = fit_model(augmented(base_rows, augmentation), epochs, 4, 'cnn', balance_sessions=True, seed=seed)
        encoder = embedding_model(base)
        classifier = base.get_layer('classifier')
        weight0, bias0 = (v.numpy().astype(np.float64) for v in classifier.weights)
        e, y, m = embed(encoder, calibration)
        strong = [r for r in held if eligible[r['example_id']]]
        result = {}
        # Score the untouched base first: two-layer fine-tuning mutates the shared Keras layers in place.
        zero_shot = HeadModel(encoder, weight0, bias0)
        result['zero_shot_base'] = {'all': classification_metrics(zero_shot, held), 'training_eligible_subset': classification_metrics(zero_shot, strong)}
        if layers == 'head':
            weight, bias = fit_head(e, y, m, weight0, bias0, steps=steps, anchor=anchor)
            calibrated = HeadModel(encoder, weight, bias)
        else:
            calibrated = fit_two_layers(base, calibration, seed=seed)
        result['calibrated_head'] = {'all': classification_metrics(calibrated, held), 'training_eligible_subset': classification_metrics(calibrated, strong)}
        for name in ('zero_shot_base', 'calibrated_head'):
            print(json.dumps({'pad': pad, 'model': name, 'evidenced_gated_recall': result[name]['training_eligible_subset']['live_confidence_gate']['recall']}), flush=True)
        report['pads'][pad] = result
    (output / 'evaluation.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--cnn-epochs', type=int, default=80)
    parser.add_argument('--seed', type=int, default=1729)
    parser.add_argument('--augmentation', choices=('none', 'sensor-gain'), default='none')
    parser.add_argument('--layers', choices=('head', 'head+dense'), default='head')
    parser.add_argument('--anchor', type=float, default=.01)
    parser.add_argument('--steps', type=int, default=400)
    args = parser.parse_args()
    run(args.manifest, args.output_dir, args.cnn_epochs, args.seed, args.augmentation, args.layers, args.anchor, args.steps)


if __name__ == '__main__':
    main()
