#!/usr/bin/env python3
"""Asset-free pilot waveform features, softmax training and model serialization.

Standard library only. This is a small supervised baseline for experiments,
not a claim of validated drum-trigger accuracy or calibrated confidence.
"""
from __future__ import annotations

import math
from pathlib import Path
import struct

FRAMES = 32
FEATURES = 68
LABELS = ("background", "head", "rim")


def f32(value: float) -> float:
    return struct.unpack("<f", struct.pack("<f", value))[0]


def features(waveform: list[list[float]]) -> list[float]:
    if len(waveform) != 2 or any(len(channel) != FRAMES for channel in waveform):
        raise ValueError("expected 32 head samples and 32 rim samples")
    if any(not math.isfinite(value) or abs(value) > 4095 for channel in waveform for value in channel):
        raise ValueError("expected finite centered 12-bit ADC samples")
    samples = [[f32(value) for value in channel] for channel in waveform]
    peaks = [max(map(abs, channel)) for channel in samples]
    peak = max(1.0, *peaks)
    result = [f32(value / peak) for channel in samples for value in channel]
    result += [f32(f32(math.log1p(value)) / f32(math.log(4096))) for value in peaks]
    for channel in samples:
        total = 0.0
        for value in channel:
            total = f32(total + abs(value))
        result.append(f32(total / f32(peak * FRAMES)))
    return result


def shared_features(waveform: list[list[float]], secondary_present: bool = True) -> list[float]:
    if type(secondary_present) is not bool:
        raise ValueError("secondary_present must be explicit boolean")
    active = waveform if secondary_present else [waveform[0], [0.0] * FRAMES]
    return features(active) + [float(secondary_present)]


def probabilities(scores: list[float]) -> list[float]:
    maximum = max(scores)
    values = [math.exp(score - maximum) for score in scores]
    total = sum(values)
    return [value / total for value in values]


def predict(model: dict, values: list[float]) -> list[float]:
    if len(values) != model["features"] or any(not math.isfinite(value) for value in values):
        raise ValueError("invalid model feature vector")
    normalized = [(value - mean) * scale for value, mean, scale in
                  zip(values, model["mean"], model["scale"])]
    scores = [bias + sum(weight * value for weight, value in zip(row, normalized))
              for bias, row in zip(model["bias"], model["weights"])]
    if model.get("format") == "OMDRUM02":
        if values[-1] not in (0.0, 1.0):
            raise ValueError("invalid secondary sensor mask")
        if values[-1] == 0.0:
            return probabilities(scores[:2]) + [0.0]
    return probabilities(scores)


def fit(examples: list[tuple[list[float], int]], epochs: int = 400,
        learning_rate: float = 0.03, regularization: float = 0.01, zone_count: int = 2) -> dict:
    if not examples or not 1 <= epochs <= 10000:
        raise ValueError("training requires examples and 1..10000 epochs")
    if zone_count not in (1, 2):
        raise ValueError("only single-zone and dual-zone pads are supported")
    class_count = zone_count + 1
    counts = [sum(label == index for _, label in examples) for index in range(class_count)]
    if any(count == 0 for count in counts):
        raise ValueError("training needs examples for every configured pad class")
    feature_count = len(examples[0][0])
    if feature_count not in (FEATURES, FEATURES + 1) or (feature_count == FEATURES + 1 and zone_count != 2):
        raise ValueError("unsupported waveform feature shape")
    shared = feature_count == FEATURES + 1
    if any(len(values) != feature_count or label not in range(class_count)
           or any(not math.isfinite(value) for value in values) for values, label in examples):
        raise ValueError("invalid training examples")
    if shared and any(values[-1] not in (0.0, 1.0) or (values[-1] == 0.0 and label == 2)
                      for values, label in examples):
        raise ValueError("invalid masked training label")
    means = [sum(values[index] for values, _ in examples) / len(examples) for index in range(feature_count)]
    # A fixed variance floor avoids amplifying tiny incidental waveform differences.
    scales = [1 / max(0.05, math.sqrt(sum((values[index] - means[index]) ** 2
                                         for values, _ in examples) / len(examples)))
              for index in range(feature_count)]
    normalized = [([(value - mean) * scale for value, mean, scale in zip(values, means, scales)], label)
                  for values, label in examples]
    weights = [[0.0] * feature_count for _ in range(class_count)]
    biases = [0.0] * class_count
    first = [[0.0] * (feature_count + 1) for _ in range(class_count)]
    second = [[0.0] * (feature_count + 1) for _ in range(class_count)]
    masks = [not shared or values[-1] == 1.0 for values, _ in examples]
    for epoch in range(1, epochs + 1):
        gradient = [[0.0] * (feature_count + 1) for _ in range(class_count)]
        for (values, label), secondary_present in zip(normalized, masks):
            predicted = probabilities([bias + sum(weight * value for weight, value in zip(row, values))
                                       for bias, row in zip(biases, weights)])
            if not secondary_present:
                predicted = probabilities([bias + sum(weight * value for weight, value in zip(row, values))
                                           for bias, row in zip(biases[:2], weights[:2])]) + [0.0]
            importance = 1 / (class_count * counts[label])
            for index in range(class_count):
                error = (predicted[index] - int(index == label)) * importance
                for feature, value in enumerate(values):
                    gradient[index][feature] += error * value
                gradient[index][-1] += error
        for index in range(class_count):
            parameters = weights[index] + [biases[index]]
            for feature in range(feature_count + 1):
                derivative = gradient[index][feature]
                if feature < feature_count:
                    derivative += regularization * parameters[feature]
                first[index][feature] = 0.9 * first[index][feature] + 0.1 * derivative
                second[index][feature] = 0.999 * second[index][feature] + 0.001 * derivative ** 2
                corrected_first = first[index][feature] / (1 - 0.9 ** epoch)
                corrected_second = second[index][feature] / (1 - 0.999 ** epoch)
                parameters[feature] -= learning_rate * corrected_first / (math.sqrt(corrected_second) + 1e-8)
            weights[index], biases[index] = parameters[:-1], parameters[-1]
    return {"format": "OMDRUM02" if shared else "OMDRUM01", "frames": FRAMES, "features": feature_count,
            "zone_count": zone_count, "labels": list(LABELS[:class_count]),
            "mean": list(map(f32, means)), "scale": list(map(f32, scales)),
            "weights": [list(map(f32, row)) for row in weights], "bias": list(map(f32, biases))}


def save_model(model: dict, path: Path) -> None:
    with path.open("x") as output:
        output.write(f"{model['format']} 32 {model['features']} {len(model['labels'])} {model['zone_count']}\n")
        for tag in ("mean", "scale", "weights", "bias"):
            values = ([value for row in model[tag] for value in row] if tag == "weights" else model[tag])
            output.write(tag + " " + " ".join(format(f32(value), ".9g") for value in values) + "\n")


def evaluate(model: dict, examples: list[tuple[list[float], int]]) -> dict:
    class_count = len(model["labels"])
    confusion = [[0] * class_count for _ in range(class_count)]
    for values, label in examples:
        predicted = predict(model, values)
        confusion[label][max(range(class_count), key=predicted.__getitem__)] += 1
    recalls = [confusion[index][index] / sum(confusion[index]) if sum(confusion[index]) else None
               for index in range(class_count)]
    return {"labels": model["labels"], "confusion_rows_actual_columns_predicted": confusion,
            "accuracy": sum(confusion[index][index] for index in range(class_count)) / len(examples) if examples else None,
            "recall_per_class": recalls,
            "balanced_accuracy": sum(recalls) / class_count if all(value is not None for value in recalls) else None,
            "trials": len(examples), "probabilities_are_calibrated": False}
