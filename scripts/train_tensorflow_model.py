#!/usr/bin/env python3
"""Train shared sensor classification with TensorFlow/Keras and export LiteRT.

Verified archives, whole-pad splits and sensor-mask augmentation are reused
from the dataset tooling. No custom gradients or inference operators are used.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path

os.environ.setdefault('TF_CPP_MIN_LOG_LEVEL', '2')
os.environ.setdefault('TF_NUM_INTRAOP_THREADS', '1')
os.environ.setdefault('TF_NUM_INTEROP_THREADS', '1')
import numpy as np
import tensorflow as tf

from train_shared_drum_model import collect_manifest, examples, partition

LABELS = ['background', 'head', 'rim', 'rimshot']


def make_model(x: np.ndarray, classes: int = 3) -> tf.keras.Model:
    mean = x.mean(axis=0).astype(np.float32)
    scale = (1.0 / np.maximum(x.std(axis=0), 0.05)).astype(np.float32)
    inputs = tf.keras.Input(shape=(69,), name='sensor_features', dtype='float32')
    normalized = (inputs - mean) * scale
    # Keep the existing linear baseline architecture during the framework migration.
    logits = tf.keras.layers.Dense(classes, kernel_initializer='zeros', bias_initializer='zeros',
                                   kernel_regularizer=tf.keras.regularizers.L2(0.005), name='classifier')(normalized)
    probabilities = tf.keras.layers.Softmax()(logits)
    gate = tf.keras.ops.concatenate([tf.keras.ops.ones_like(inputs[:, :2]), *[inputs[:, 68:69] for _ in range(classes - 2)]], axis=1)
    masked = probabilities * gate
    outputs = masked / tf.keras.ops.sum(masked, axis=1, keepdims=True)
    return tf.keras.Model(inputs, outputs, name='shared_drum')


def fit_model(rows: list[dict], epochs: int, classes: int | None = None) -> tf.keras.Model:
    tf.keras.utils.set_random_seed(1729)
    data = examples(rows, augment=True)
    x = np.asarray([values for values, _ in data], dtype=np.float32)
    y = np.asarray([label for _, label in data], dtype=np.int32)
    classes = classes or max(label for _, label in data) + 1
    counts = np.bincount(y, minlength=classes)
    if np.any(counts < 8):
        raise ValueError('training needs eight examples of each class')
    weights = (len(data) / (classes * counts[y])).astype(np.float32)
    model = make_model(x, classes)
    model.compile(optimizer=tf.keras.optimizers.Adam(learning_rate=0.03, epsilon=1e-8),
                  loss=tf.keras.losses.SparseCategoricalCrossentropy())
    options = tf.data.Options()
    options.threading.private_threadpool_size = 1
    options.threading.max_intra_op_parallelism = 1
    options.autotune.enabled = False
    options.experimental_deterministic = True
    dataset = tf.data.Dataset.from_tensor_slices((x, y, weights)).batch(len(data)).with_options(options)
    history = model.fit(dataset, epochs=epochs, verbose=0)
    if not np.all(np.isfinite(history.history['loss'])):
        raise ValueError('non-finite Keras training loss')

    return model


def evaluate(model, rows):
    data = examples(rows)
    x = np.asarray([v for v, _ in data], dtype=np.float32)
    probabilities = model(x, training=False).numpy()
    classes = model.output_shape[-1]
    confusion = np.zeros((classes, classes), dtype=int)
    for (_, label), output in zip(data, probabilities):
        confusion[label, int(output.argmax())] += 1
    recalls = [float(confusion[i, i] / confusion[i].sum()) if confusion[i].sum() else None for i in range(classes)]
    return {'trials': len(rows), 'confusion_rows_actual_columns_predicted': confusion.tolist(),
            'recall': recalls, 'accuracy': float(np.trace(confusion) / len(rows)),
            'balanced_accuracy': float(np.mean(recalls)) if all(v is not None for v in recalls) else None}


def export(model, folder: Path, trials: list[dict]):
    folder.mkdir(parents=True, exist_ok=False)
    model.save(folder / 'model.keras')
    # Export a fixed batch of one: all target buffers can be allocated before live processing.
    module = tf.Module()
    module.model = model
    module.serve = tf.function(lambda x: model(x, training=False), input_signature=[tf.TensorSpec([1, 69], tf.float32, name='sensor_features')])
    tf.saved_model.save(module, str(folder / 'saved-model'), signatures={'serving_default': module.serve})
    fixed_input = tf.keras.Input(batch_shape=(1, 69), dtype="float32", name="sensor_features")
    export_model = tf.keras.Model(fixed_input, model(fixed_input, training=False))
    converter = tf.lite.TFLiteConverter.from_keras_model(export_model)
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS]
    payload = converter.convert()
    (folder / 'model.tflite').write_bytes(payload)
    runtime = tf.lite.Interpreter(model_content=payload, num_threads=1)
    runtime.allocate_tensors()
    input_info, output_info = runtime.get_input_details()[0], runtime.get_output_details()[0]
    maximum = 0.0
    cases = []
    for trial in trials:
        for secondary in ((True, False) if trial.get("zone_count", 2) == 2 else (False,)):
            if not secondary and trial['label'] >= 2:
                continue
            from drum_model import shared_features
            x = np.asarray([shared_features(trial['waveform'], secondary)], dtype=np.float32)
            expected = model(x, training=False).numpy()[0]
            runtime.set_tensor(input_info['index'], x)
            runtime.invoke()
            actual = runtime.get_tensor(output_info['index'])[0]
            difference = float(np.max(np.abs(actual - expected)))
            maximum = max(maximum, difference)
            if difference > 0.00002 or actual.argmax() != expected.argmax():
                raise ValueError('LiteRT conversion changed a prediction')
            cases.append({'capture_sha256': trial['capture_sha256'], 'secondary_present': secondary,
                          'features': x[0].tolist(), 'probabilities': actual.tolist()})
    (folder / 'conversion-cases.json').write_text(json.dumps(cases) + '\n')
    return {'cases': len(cases), 'maximum_probability_difference': maximum,
            'model_sha256': hashlib.sha256(payload).hexdigest(), 'model_bytes': len(payload)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--epochs', type=int, default=400)
    args = parser.parse_args()
    if not 1 <= args.epochs <= 10000:
        parser.error('epochs must be in 1..10000')
    if args.output_dir.exists():
        parser.error('output directory already exists')
    trials, excluded = collect_manifest(args.manifest)
    groups = sorted({t['group'] for t in trials})
    if len(groups) < 3:
        parser.error('need three identified pad groups')
    classes = max(t["label"] for t in trials) + 1
    folds = []
    for group in groups:
        training, held = partition(trials, group)
        model = fit_model(training, args.epochs, classes)
        result = {'pad': group, 'training_groups': sorted({t['group'] for t in training}),
                  'held_out': evaluate(model, held),
                  'by_phase': {p: evaluate(model, [t for t in held if t['phase'] == p]) for p in sorted({t['phase'] for t in held})}}
        folds.append(result)
        print(json.dumps(result), flush=True)
    model = fit_model(trials, args.epochs, classes)
    conversion = export(model, args.output_dir, trials)
    report = {'format': 'open-mimic-tensorflow-training-v1', 'tensorflow': tf.__version__, 'keras': tf.keras.__version__,
              'labels': LABELS[:model.output_shape[-1]], 'epochs': args.epochs, 'seed': 1729, 'real_trials': len(trials), 'folds': folds, 'conversion': conversion,
              'excluded': excluded, 'final_training_metrics_not_validation': evaluate(model, trials),
              'scope': 'Presegmented waveform classification; physical single-zone and continuous triggering not validated.'}
    (args.output_dir / 'evaluation.json').write_text(json.dumps(report, indent=2) + '\n')
    (args.output_dir / 'provenance.json').write_text(json.dumps([{k:v for k,v in t.items() if k not in ('features','waveform')} for t in trials],indent=2)+'\n')
    print(json.dumps(conversion),flush=True)


if __name__ == '__main__':
    main()
