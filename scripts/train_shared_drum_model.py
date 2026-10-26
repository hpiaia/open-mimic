#!/usr/bin/env python3
"""Train one shared waveform classifier with whole-pad holdouts and provenance.

The manifest identifies physical pad groups, verified source archives, accepted
maps and explicit corrections. Original captures are never changed. Synthetic
single-zone views are training augmentation, not validation on physical mono pads.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import re
from pathlib import Path
import statistics

from drum_model import evaluate, fit, predict, save_model, shared_features
from train_drum_model import load_trial
from sequence_training import load_sequence_trials


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def collect_manifest(manifest_path: Path) -> tuple[list[dict], list[dict]]:
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("format") != "open-mimic-shared-training-v1":
        raise ValueError("unsupported training manifest")
    trials, excluded, hashes = [], [], {}
    for source in manifest["sources"]:
        directory = Path(source["directory"])
        if not directory.is_absolute():
            directory = manifest_path.parent / directory
        directory = directory.resolve()
        verified = json.loads((directory / "download.json").read_text())["verified_files"]
        for relative, expected_hash in sorted(verified.items()):
            if not relative.endswith(".json"):
                continue
            path = directory / relative
            if path.resolve().parent != path.parent.resolve() or not path.resolve().is_relative_to(directory):
                raise ValueError("metadata path escaped its verified archive")
            if sha256(path) != expected_hash:
                raise ValueError(f"verified metadata changed: {path}")
            metadata = json.loads(path.read_text())
            if metadata.get("format") not in ("open-mimic-pad-trial-v1", "open-mimic-pad-sequence-v1"):
                continue
            pad = metadata["pad"]
            spec = source["pads"].get(pad)
            if spec is None:
                continue
            original = {"head": metadata["head"], "rim": metadata["rim"]}
            correction = spec.get("correction")
            desired = spec["mapping"]
            if original != desired and (not correction or original != correction["original_mapping"]):
                excluded.append({"metadata": str(path), "reason": "not the manifest's accepted channel profile"})
                continue
            capture = path.parent / metadata["capture"]
            try:
                capture_relative = str(capture.relative_to(directory))
                if capture.resolve().parent != path.parent.resolve() or not capture.resolve().is_relative_to(directory):
                    raise ValueError("capture path escaped its verified archive")
                if sha256(capture) != verified.get(capture_relative):
                    raise ValueError("verified capture changed or is not in archive manifest")
            except (OSError, ValueError) as error:
                raise ValueError(f"archive integrity failure: {capture}: {error}") from error
            try:
                if metadata["format"] == "open-mimic-pad-sequence-v1":
                    candidates = load_sequence_trials(path, desired if original != desired else None)
                else:
                    candidates = [load_trial(path, desired if original != desired else None)]
            except (OSError, ValueError, KeyError, TypeError) as error:
                excluded.append({"metadata": str(path), "reason": str(error)})
                continue
            for trial in candidates:
                if not isinstance(spec["physical_pad_group"], str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,63}", spec["physical_pad_group"]):
                    raise ValueError("physical pad group must be a safe, stable identifier")
                trial["group"] = spec["physical_pad_group"]
                trial["group_identity_basis"] = spec["identity_basis"]
                trial["mapping_correction"] = correction if original != desired else None
                key = trial.get("example_id", trial["capture_sha256"])
                previous = hashes.get(key)
                if previous:
                    if (previous["group"], previous["label"], previous["mapping"]) != (trial["group"], trial["label"], trial["mapping"]):
                        raise ValueError("identical raw capture has conflicting pad/label/map provenance")
                    excluded.append({"metadata": str(path), "reason": "duplicate raw capture"})
                    continue
                hashes[key] = trial
                trials.append(trial)

    if not trials:
        raise ValueError("no usable verified trials")
    return trials, excluded


def partition(trials: list[dict], held_out_group: str) -> tuple[list[dict], list[dict]]:
    training = [trial for trial in trials if trial["group"] != held_out_group]
    validation = [trial for trial in trials if trial["group"] == held_out_group]
    if not training or not validation:
        raise ValueError("whole-pad holdout needs nonempty training and validation")
    if {trial["group"] for trial in training} & {trial["group"] for trial in validation}:
        raise ValueError("physical pad leaked across split")
    if {trial["capture_sha256"] for trial in training} & {trial["capture_sha256"] for trial in validation}:
        raise ValueError("raw waveform leaked across split")
    return training, validation


def examples(trials: list[dict], augment: bool = False) -> list[tuple[list[float], int]]:
    result = []
    for trial in trials:
        secondary = trial.get("zone_count", 2) == 2
        result.append((shared_features(trial["waveform"], secondary), trial["label"]))
        if augment and secondary and trial["label"] < 2:
            result.append((shared_features(trial["waveform"], False), trial["label"]))
    return result


def metrics(model: dict, trials: list[dict]) -> dict:
    return {"overall": evaluate(model, examples(trials)),
            "by_phase": {phase: evaluate(model, examples([t for t in trials if t["phase"] == phase]))
                         for phase in sorted({t["phase"] for t in trials})}}


def train_shared(trials: list[dict], epochs: int, output: Path) -> dict:
    if any(trial["label"] == 3 for trial in trials):
        raise ValueError("Rimshot data requires train_tensorflow_model.py")
    groups = sorted({trial["group"] for trial in trials})
    if len(groups) < 3:
        raise ValueError("shared pilot requires at least three separately identified pad groups")
    rates = [trial["estimated_frames_per_second"] for trial in trials]
    cadence = statistics.median(rates)
    if not 1000 <= cadence <= 20000 or max(abs(rate / cadence - 1) for rate in rates) > 0.10:
        raise ValueError("capture cadence profiles are incompatible")
    # Fail before creating artifacts if any fold cannot train all classes.
    for group in groups:
        training, _ = partition(trials, group)
        counts = Counter(trial["label"] for trial in training)
        if any(counts[label] < 8 for label in range(3)):
            raise ValueError(f"fold {group} needs at least eight training trials per class")
    output.mkdir(parents=True, exist_ok=False)
    folds = []
    for group in groups:
        training, validation = partition(trials, group)
        model = fit(examples(training, augment=True), epochs=epochs)
        fold = {"held_out_group": group, "training_groups": sorted({t["group"] for t in training}),
                "training_real_trials": len(training), "held_out_real_trials": len(validation),
                "training": metrics(model, training), "held_out": metrics(model, validation),
                "complete_class_coverage": set(t["label"] for t in validation) == {0, 1, 2},
                "held_out_predictions": [{"capture_sha256": t["capture_sha256"], "phase": t["phase"],
                                          "actual": t["label"], "probabilities": predict(model, shared_features(t["waveform"]))}
                                         for t in validation]}
        fold_dir = output / "folds" / group
        fold_dir.mkdir(parents=True)
        save_model(model, fold_dir / "model.omdrum")
        (fold_dir / "evaluation.json").write_text(json.dumps(fold, indent=2) + "\n")
        folds.append(fold)
        print(f"Held out {group}: {fold['held_out']['overall']}", flush=True)
    model = fit(examples(trials, augment=True), epochs=epochs)
    save_model(model, output / "model.omdrum")
    (output / "model.json").write_text(json.dumps(model, indent=2) + "\n")
    complete = [fold["held_out"]["overall"]["balanced_accuracy"] for fold in folds if fold["complete_class_coverage"]]
    report = {"format": "open-mimic-shared-evaluation-v1", "experimental": True, "epochs_fixed_before_evaluation": epochs,
              "real_trials": len(trials), "training_examples_including_mask_augmentation": len(examples(trials, True)),
              "estimated_frames_per_second": cadence, "window_milliseconds_estimate": 32 / cadence * 1000,
              "groups": {group: dict(Counter(t["phase"] for t in trials if t["group"] == group)) for group in groups},
              "complete_group_macro_balanced_accuracy": statistics.mean(complete) if complete else None,
              "grouping_limitation": "Owner-configured pad slots identify groups; physical device serials/models were not recorded. This is not proof of unseen trigger-model generalization.",
              "split": "Whole pad group held out; all channel-mask augmentations remain on the training side. No random stroke split.",
              "single_zone_scope": "Synthetic masked views of real dual-zone head/background recordings only. No physical single-zone pad validation.",
              "gain_scope": "Per-window joint peak normalization and quiet baseline removal; hardware gain unmeasured. Quick calibration adapter not validated.",
              "deployment": "Not an event engine. No automatic MIDI, velocity estimation, rimshot or continuous-roll accuracy claim.",
              "final_model_training_metrics_not_validation": metrics(model, trials), "folds": folds}
    (output / "evaluation.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--epochs", type=int, default=400)
    args = parser.parse_args()
    try:
        trials, excluded = collect_manifest(args.manifest)
        report = train_shared(trials, args.epochs, args.output_dir)
        provenance = {"manifest": str(args.manifest.resolve()), "manifest_sha256": sha256(args.manifest),
                      "trials": [{k: v for k, v in trial.items() if k not in ("features", "waveform")} for trial in trials],
                      "excluded": excluded}
        (args.output_dir / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
        (args.output_dir / "waveforms.json").write_text(json.dumps([
            {"capture_sha256": t["capture_sha256"], "group": t["group"], "label": t["label"],
             "phase": t["phase"], "waveform": t["waveform"]} for t in trials]) + "\n")
        artifacts = {str(path.relative_to(args.output_dir)): sha256(path) for path in args.output_dir.rglob("*") if path.is_file()}
        (args.output_dir / "checksums.json").write_text(json.dumps(artifacts, indent=2) + "\n")
        print("Shared model trained. Complete-group macro balanced accuracy:", report["complete_group_macro_balanced_accuracy"])
        print("Experimental artifact only; no MIDI event engine enabled.")
        return 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f"error: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
