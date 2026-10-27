#!/usr/bin/env python3
"""Per-pad classifier adaptation from the pad's own calibration recordings.

Three steps, all on the host that archives recordings:

1. ``manifest`` scans archived sessions and writes a training manifest.
2. ``base`` trains the shared CNN on every archived pad (the kit's factory model).
3. ``adapt`` fine-tunes only the last two layers of the base on one pad's
   windows, exports a LiteRT model plus a sidecar description that the module
   validates, and verifies the export against Keras.

The module keeps the shared model as fallback; nothing here installs anything.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import sys

os.environ.setdefault('TF_CPP_MIN_LOG_LEVEL', '2')

DEFAULT_GATE = (0.65, 0.65, 0.65)
IDENTITY_BASIS = 'Owner-confirmed pad slot; trigger model and serial not recorded.'


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build_manifest(measurements: Path) -> dict:
    """Describe every archived (device-released) session as a manifest source."""
    sources = []
    for download in sorted(measurements.glob('*/*/download.json')):
        session = download.parent
        pad = session.parent.name
        report = json.loads(download.read_text())
        if not report.get('verified_files') or not report.get('device_captures_removed'):
            continue
        mapping = None
        for name in sorted(report.get('verified_files', {})):
            if not name.endswith('.json') or name == 'ready.json':
                continue
            document = json.loads((session / name).read_text())
            if document.get('format') in ('open-mimic-pad-sequence-v1', 'open-mimic-pad-trial-v1') and document.get('pad') == pad:
                candidate = {'head': document['head'], 'rim': document.get('rim')}
                if mapping is not None and candidate != mapping:
                    raise ValueError(f'{pad}/{session.name}: sensor mapping changes inside one session')
                mapping = candidate
        if mapping is None:
            continue
        sources.append({'directory': str(session.resolve()),
                        'pads': {pad: {'physical_pad_group': pad, 'identity_basis': IDENTITY_BASIS, 'mapping': mapping}}})
    if not sources:
        raise ValueError('No archived sessions found')
    return {'format': 'open-mimic-shared-training-v1', 'sources': sources}


def sidecar(pad: str, base_sha256: str, source: str, model_sha256: str, gate) -> str:
    gate = [float(v) for v in gate]
    if len(gate) != 3 or any(not 0 <= v <= 1 for v in gate):
        raise ValueError('gate needs three thresholds within 0..1')
    for value in (base_sha256, source, model_sha256):
        if len(value) != 64 or any(c not in '0123456789abcdef' for c in value):
            raise ValueError('identities must be 64 lowercase hex characters')
    return (f'OPEN_MIMIC_PAD_MODEL 1\npad {pad}\nbase {base_sha256}\nsource {source}\n'
            f'sha256 {model_sha256}\ngate {gate[0]:.9g} {gate[1]:.9g} {gate[2]:.9g}\n')


def source_identity(rows, base_sha256: str) -> str:
    captures = sorted({r['capture_sha256'] for r in rows})
    return hashlib.sha256(('\n'.join([base_sha256, *captures])).encode()).hexdigest()


def finetune_two_layers(model, rows, epochs=60, rate=.002, seed=1729):
    """Keras fine-tune of only the Dense(24) and classifier layers (everything else frozen)."""
    import numpy as np
    from train_shared_drum_model import examples
    from train_tensorflow_model import tf
    clone = tf.keras.models.clone_model(model)
    clone.set_weights(model.get_weights())
    tf.keras.utils.set_random_seed(seed)
    for layer in clone.layers:
        layer.trainable = layer.name in ('dense', 'classifier')
    data = examples(rows)
    x = np.asarray([v for v, _ in data], dtype=np.float32)
    y = np.asarray([label for _, label in data], dtype=np.int32)
    counts = np.bincount(y, minlength=4).astype(np.float32)
    weights = (len(y) / ((counts > 0).sum() * np.maximum(counts[y], 1))).astype(np.float32)
    clone.compile(optimizer=tf.keras.optimizers.Adam(rate), loss=tf.keras.losses.SparseCategoricalCrossentropy())
    clone.fit(x, y, sample_weight=weights, batch_size=64, epochs=epochs, verbose=0, shuffle=True)
    return clone


def load_rows(manifest: Path, measurements: Path | None = None):
    """Sensor-evidenced windows, minus any the owner excluded through review_rimshots."""
    from compare_drum_models import training_evidence
    from review_rimshots import apply_reviews
    from train_shared_drum_model import collect_manifest
    rows, excluded = collect_manifest(manifest)
    rows = [r for r in rows if training_evidence(r, 3.)]
    if measurements is not None:
        rows, reviewed = apply_reviews(rows, measurements)
        if reviewed:
            excluded = [*excluded, {'reason': f'{reviewed} windows excluded by owner label review'}]
    return rows, excluded


def train_base(manifest: Path, output: Path, epochs=80, seed=1729, measurements: Path | None = None) -> dict:
    from train_tensorflow_model import export, fit_model, tf
    if output.exists():
        raise ValueError('Choose a new base output directory')
    rows, excluded = load_rows(manifest, measurements)
    if max(r['label'] for r in rows) != 3 or len({r['group'] for r in rows}) < 2:
        raise ValueError('Base training needs rimshot examples and at least two pads')
    tf.keras.backend.clear_session()
    model = fit_model(rows, epochs, 4, 'cnn', balance_sessions=True, seed=seed)
    conversion = export(model, output, rows)
    report = {'format': 'open-mimic-base-model-v1', 'architecture': 'cnn', 'epochs': epochs, 'seed': seed,
              'manifest_sha256': sha256_file(manifest), 'training_windows': len(rows),
              'pads': sorted({r['group'] for r in rows}), 'sessions': len({(r['group'], r['session']) for r in rows}),
              'excluded': excluded, 'conversion': conversion, 'model_sha256': conversion['model_sha256'],
              'scope': 'Shared factory model for this kit; trained on all archived sessions, so not an independent validation.'}
    (output / 'base.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def adapt_pad(manifest: Path, base: Path, pad: str, output: Path, gate=DEFAULT_GATE, epochs=60, seed=1729, measurements: Path | None = None) -> dict:
    import numpy as np
    from compare_drum_models import classification_metrics
    from train_tensorflow_model import export, tf
    if output.exists():
        raise ValueError('Choose a new adaptation output directory')
    base_report = json.loads((base / 'base.json').read_text())
    base_sha256 = sha256_file(base / 'model.tflite')
    if base_sha256 != base_report['model_sha256']:
        raise ValueError('Base model file differs from its report')
    rows, excluded = load_rows(manifest, measurements)
    own = [r for r in rows if r['group'] == pad]
    if len(own) < 40:
        raise ValueError(f'{pad}: only {len(own)} sensor-evidenced windows; record a complete calibration first')
    zones = {r.get('zone_count', 2) for r in own}
    if len(zones) != 1:
        raise ValueError(f'{pad}: sensor configuration changed between sessions')
    present = sorted({r['label'] for r in own})
    needed = [0, 1] if zones == {1} else [0, 1, 2, 3]
    if any(label not in present for label in needed):
        raise ValueError(f'{pad}: calibration lacks examples of every articulation')
    tf.keras.backend.clear_session()
    model = tf.keras.models.load_model(base / 'model.keras')
    tuned = finetune_two_layers(model, own, epochs=epochs, seed=seed)
    conversion = export(tuned, output, own)
    source = source_identity(own, base_sha256)
    description = sidecar(pad, base_sha256, source, conversion['model_sha256'], gate)
    (output / 'model.sidecar').write_text(description)
    training = classification_metrics(tuned, own)
    report = {'format': 'open-mimic-pad-model-v1', 'pad': pad, 'base_sha256': base_sha256, 'source': source,
              'model_sha256': conversion['model_sha256'], 'gate': list(gate), 'epochs': epochs, 'seed': seed,
              'windows': len(own), 'sessions': sorted({r['session'] for r in own}),
              'captures': sorted({r['capture_sha256'] for r in own}), 'conversion': conversion, 'excluded': excluded,
              'training_gated_recall_not_validation': training['live_confidence_gate']['recall'],
              'tflite': str(output / 'model.tflite'), 'sidecar': str(output / 'model.sidecar'),
              'scope': 'Last two layers refit on this pad; recall above is on its own training windows.'}
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    manifest = commands.add_parser('manifest')
    manifest.add_argument('--measurements', type=Path, required=True)
    manifest.add_argument('--output', type=Path, required=True)
    base = commands.add_parser('base')
    base.add_argument('--manifest', type=Path, required=True)
    base.add_argument('--output-dir', type=Path, required=True)
    base.add_argument('--epochs', type=int, default=80)
    base.add_argument('--measurements', type=Path, help='Honour owner label reviews stored under this tree')
    adapt = commands.add_parser('adapt')
    adapt.add_argument('--manifest', type=Path, required=True)
    adapt.add_argument('--base-dir', type=Path, required=True)
    adapt.add_argument('--pad', required=True)
    adapt.add_argument('--output-dir', type=Path, required=True)
    adapt.add_argument('--gate', type=float, nargs=3, default=list(DEFAULT_GATE), metavar=('HEAD', 'RIM', 'RIMSHOT'))
    adapt.add_argument('--epochs', type=int, default=60)
    adapt.add_argument('--measurements', type=Path, help='Honour owner label reviews stored under this tree')
    args = parser.parse_args()
    try:
        if args.command == 'manifest':
            document = build_manifest(args.measurements)
            args.output.write_text(json.dumps(document, indent=2) + '\n')
            result = {'sources': len(document['sources']), 'manifest': str(args.output)}
        elif args.command == 'base':
            result = train_base(args.manifest, args.output_dir, args.epochs, measurements=args.measurements)
        else:
            result = adapt_pad(args.manifest, args.base_dir, args.pad, args.output_dir, tuple(args.gate), args.epochs, measurements=args.measurements)
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(json.dumps({'error': str(error)}))
        return 1
    print(json.dumps(result))
    return 0


if __name__ == '__main__':
    sys.exit(main())
