#!/usr/bin/env python3
"""Evaluate real session-split windows and an optional real MCU prediction export.

Only Python's standard library is used.  This tool refuses to score a dataset
with structural quality errors, missing axis metadata, incomplete held-out
class coverage, or an incomplete session split.  If sensor calibration
metadata is unavailable, the threshold baseline is explicitly raw-only and a
model score is not reported as reproducible.  It never fills missing samples
and never fabricates model predictions.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Set, Tuple


CSV_HEADER = [
    "session_id",
    "label",
    "window_seq",
    "sample_index",
    "tick_ms",
    "ax_raw",
    "ay_raw",
    "az_raw",
]
PREDICTION_FIELDS = {"session_id", "window_seq", "predicted_class"}
TOKEN_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,63}\Z")
UINT_RE = re.compile(r"[0-9]+\Z")
INT_RE = re.compile(r"-?[0-9]+\Z")
DEFAULT_CLASSES = ["STABLE", "VIBRATION", "IMPACT"]
PROTOCOL_VERSION = 1
ACCEL_RANGE_G = 4
ACCEL_LSB_PER_G = 8192
RAW_SCALE_G_PER_LSB = 1.0 / ACCEL_LSB_PER_G
EXPECTED_CALIBRATION_METADATA = {
    "protocol_version": PROTOCOL_VERSION,
    "sensor": "MPU6050",
    "accel_range_g": ACCEL_RANGE_G,
    "accel_lsb_per_g": ACCEL_LSB_PER_G,
    "sensor_config": "current_compiled_configuration",
}


@dataclass(frozen=True)
class Window:
    session_id: str
    label: str
    window_seq: int
    samples: Tuple[Tuple[int, int, int], ...]


@dataclass(frozen=True)
class WindowMetrics:
    peak: float
    rms: float


class Quality:
    def __init__(self) -> None:
        self.issues: List[str] = []
        self.counts: Dict[str, int] = {}
        self.warnings: List[str] = []
        self.warning_counts: Dict[str, int] = {}

    def add(self, kind: str, detail: str) -> None:
        self.counts[kind] = self.counts.get(kind, 0) + 1
        if len(self.issues) < 40:
            self.issues.append("%s: %s" % (kind, detail))

    def warn(self, kind: str, detail: str) -> None:
        self.warning_counts[kind] = self.warning_counts.get(kind, 0) + 1
        if len(self.warnings) < 40:
            self.warnings.append("%s: %s" % (kind, detail))

    @property
    def ok(self) -> bool:
        return not self.issues


def _parse_uint(value: str, field: str, maximum: int) -> int:
    if not UINT_RE.fullmatch(value):
        raise ValueError("%s is not a non-negative integer" % field)
    parsed = int(value, 10)
    if parsed > maximum:
        raise ValueError("%s is outside 0..%d" % (field, maximum))
    return parsed


def _parse_int16(value: str, field: str) -> int:
    if not INT_RE.fullmatch(value):
        raise ValueError("%s is not an integer" % field)
    parsed = int(value, 10)
    if parsed < -32768 or parsed > 32767:
        raise ValueError("%s is outside int16 range" % field)
    return parsed


def _safe_session(value: str) -> str:
    if not TOKEN_RE.fullmatch(value):
        raise ValueError("session_id is not a safe non-empty token")
    return value


def read_metadata(path: Path, quality: Quality) -> Tuple[Dict[str, object], Optional[float]]:
    if not path.is_file():
        quality.add("axis_metadata_missing", str(path))
        return {}, None
    try:
        with path.open("r", encoding="utf-8-sig") as handle:
            metadata = json.load(handle)
    except (OSError, ValueError) as exc:
        quality.add("axis_metadata_invalid", "%s (%s)" % (path, exc))
        return {}, None
    if not isinstance(metadata, dict):
        quality.add("axis_metadata_invalid", "metadata root is not an object")
        return {}, None
    metadata_shape_valid = True
    if metadata.get("schema_version") != 2:
        quality.add("axis_metadata_invalid", "schema_version must be 2")
        metadata_shape_valid = False
    if metadata.get("axis_order") != ["ax_raw", "ay_raw", "az_raw"]:
        quality.add("axis_metadata_invalid", "axis_order must be [ax_raw, ay_raw, az_raw]")
        metadata_shape_valid = False
    if metadata.get("window_size") != 128:
        quality.add("axis_metadata_invalid", "window_size must be 128")
        metadata_shape_valid = False
    if metadata.get("sample_rate_hz") != 100:
        quality.add("axis_metadata_invalid", "sample_rate_hz must be 100")
        metadata_shape_valid = False
    if metadata.get("raw_dtype") != "int16":
        quality.add("axis_metadata_invalid", "raw_dtype must be int16")
        metadata_shape_valid = False

    calibration_valid = True
    for key, expected in EXPECTED_CALIBRATION_METADATA.items():
        if metadata.get(key) != expected:
            quality.warn(
                "metadata_calibration_unavailable",
                "%s must be %r for the current compiled firmware configuration"
                % (key, expected),
            )
            calibration_valid = False

    scale = metadata.get("raw_scale_g_per_lsb")
    if scale is None:
        quality.warn(
            "metadata_calibration_unavailable",
            "raw_scale_g_per_lsb is missing; metrics remain in raw int16 units",
        )
        return metadata, None
    try:
        if isinstance(scale, bool):
            raise ValueError("boolean is not a numeric scale")
        scale_value = float(scale)
    except (TypeError, ValueError):
        quality.warn(
            "metadata_calibration_unavailable",
            "raw_scale_g_per_lsb is not numeric; metrics remain in raw int16 units",
        )
        return metadata, None
    if not math.isfinite(scale_value) or scale_value <= 0:
        quality.warn(
            "metadata_calibration_unavailable",
            "raw_scale_g_per_lsb must be finite and positive",
        )
        return metadata, None
    if not math.isclose(scale_value, RAW_SCALE_G_PER_LSB, rel_tol=0.0, abs_tol=1e-12):
        quality.warn(
            "metadata_calibration_unavailable",
            "raw_scale_g_per_lsb must equal 1/8192 for the current compiled firmware",
        )
        calibration_valid = False
    if not metadata_shape_valid or not calibration_valid:
        return metadata, None
    return metadata, scale_value


def load_windows(data_dir: Path, quality: Quality) -> Tuple[List[Window], Dict[str, Set[str]]]:
    if not data_dir.is_dir():
        quality.add("data_directory_missing", str(data_dir))
        return [], {}

    files = sorted(path for path in data_dir.glob("*.csv") if path.is_file())
    if not files:
        quality.add("data_missing", "no CSV files in %s" % data_dir)
        return [], {}

    rows_by_window: Dict[Tuple[str, str, int], Dict[int, Tuple[int, int, int]]] = {}
    session_labels: Dict[str, Set[str]] = {}

    for path in files:
        try:
            with path.open("r", newline="", encoding="utf-8-sig") as handle:
                reader = csv.reader(handle)
                header = next(reader, None)
                if header != CSV_HEADER:
                    quality.add("csv_header_invalid", str(path))
                    continue
                for row in reader:
                    if not row:
                        continue
                    if len(row) != len(CSV_HEADER):
                        quality.add("bad_row", "%s line %d has %d fields" % (path, reader.line_num, len(row)))
                        continue
                    try:
                        session_id = _safe_session(row[0])
                        label = row[1]
                        if not label or len(label) > 64 or "," in label:
                            raise ValueError("label is empty or unsafe")
                        window_seq = _parse_uint(row[2], "window_seq", 0xFFFFFFFF)
                        sample_index = _parse_uint(row[3], "sample_index", 127)
                        _parse_uint(row[4], "tick_ms", 0xFFFFFFFF)
                        ax_raw = _parse_int16(row[5], "ax_raw")
                        ay_raw = _parse_int16(row[6], "ay_raw")
                        az_raw = _parse_int16(row[7], "az_raw")
                    except ValueError as exc:
                        quality.add("bad_row", "%s line %d (%s)" % (path, reader.line_num, exc))
                        continue

                    session_labels.setdefault(session_id, set()).add(label)
                    key = (session_id, label, window_seq)
                    samples = rows_by_window.setdefault(key, {})
                    if sample_index in samples:
                        quality.add(
                            "duplicate_sample",
                            "%s session=%s label=%s window=%d sample=%d"
                            % (path.name, session_id, label, window_seq, sample_index),
                        )
                        continue
                    samples[sample_index] = (ax_raw, ay_raw, az_raw)
        except (OSError, UnicodeError) as exc:
            quality.add("csv_read_error", "%s (%s)" % (path, exc))

    for session_id, labels in session_labels.items():
        if len(labels) > 1:
            quality.add("session_label_mix", "%s has labels %s" % (session_id, sorted(labels)))

    sequence_groups: Dict[Tuple[str, str], List[int]] = {}
    for session_id, label, window_seq in rows_by_window:
        sequence_groups.setdefault((session_id, label), []).append(window_seq)
    for stream, sequences in sequence_groups.items():
        ordered = sorted(set(sequences))
        for previous, current in zip(ordered, ordered[1:]):
            if current > previous + 1:
                quality.add("missing_window_sequence", "%s/%s missing %d..%d" % (stream[0], stream[1], previous + 1, current - 1))

    windows: List[Window] = []
    signatures: Dict[Tuple[Tuple[int, int, int], ...], Tuple[str, str, int]] = {}
    for (session_id, label, window_seq), samples in sorted(rows_by_window.items()):
        if len(samples) != 128 or set(samples) != set(range(128)):
            missing = 128 - len(samples)
            quality.add(
                "missing_sample",
                "%s/%s window %d has %d missing or invalid samples"
                % (session_id, label, window_seq, missing),
            )
            continue
        ordered_samples = tuple(samples[index] for index in range(128))
        if all(value == 0 for sample in ordered_samples for value in sample):
            quality.add("all_zero_window", "%s/%s window %d" % (session_id, label, window_seq))
        signature = ordered_samples
        if signature in signatures:
            quality.add(
                "duplicate_window",
                "%s/%s window %d duplicates %s" % (session_id, label, window_seq, signatures[signature]),
            )
        else:
            signatures[signature] = (session_id, label, window_seq)
        windows.append(Window(session_id, label, window_seq, ordered_samples))

    return windows, session_labels


def percentile(values: Sequence[float], fraction: float) -> float:
    if not values:
        raise ValueError("percentile requires at least one value")
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower = int(math.floor(position))
    upper = int(math.ceil(position))
    if lower == upper:
        return ordered[lower]
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def metrics(window: Window, scale: Optional[float]) -> WindowMetrics:
    means = [
        sum(sample[axis] for sample in window.samples) / float(len(window.samples))
        for axis in range(3)
    ]
    residuals = [
        math.sqrt(sum((sample[axis] - means[axis]) ** 2 for axis in range(3)))
        for sample in window.samples
    ]
    multiplier = 1.0 if scale is None else scale
    peak = max(residuals) * multiplier
    rms = math.sqrt(sum(value * value for value in residuals) / len(residuals)) * multiplier
    return WindowMetrics(peak=peak, rms=rms)


def thresholds(
    train: Sequence[Window],
    scale: Optional[float],
    stable_peak_max: Optional[float],
    stable_rms_max: Optional[float],
    impact_peak_min: Optional[float],
) -> Tuple[Optional[Dict[str, float]], str]:
    supplied = [stable_peak_max, stable_rms_max, impact_peak_min]
    if any(value is not None for value in supplied) and not all(value is not None for value in supplied):
        return None, "provide all three baseline thresholds or none"

    stable = [metrics(window, scale) for window in train if window.label == "STABLE"]
    vibration = [window for window in train if window.label == "VIBRATION"]
    impact = [metrics(window, scale) for window in train if window.label == "IMPACT"]
    if not stable or not vibration or not impact:
        return None, "training sessions need all three classes for the baseline"

    if all(value is not None for value in supplied):
        result = {
            "stable_peak_max": float(stable_peak_max),
            "stable_rms_max": float(stable_rms_max),
            "impact_peak_min": float(impact_peak_min),
        }
        origin = "CLI thresholds"
    else:
        result = {
            "stable_peak_max": percentile([item.peak for item in stable], 0.95),
            "stable_rms_max": percentile([item.rms for item in stable], 0.95),
            "impact_peak_min": percentile([item.peak for item in impact], 0.05),
        }
        origin = "training data: STABLE 95th percentile and IMPACT 5th percentile"

    if any(not math.isfinite(value) or value < 0 for value in result.values()):
        return None, "baseline thresholds must be finite and non-negative"
    if result["impact_peak_min"] <= result["stable_peak_max"]:
        return None, "STABLE and IMPACT peak thresholds overlap; baseline is ambiguous"
    return result, origin


def classify_baseline(item: WindowMetrics, limits: Dict[str, float]) -> str:
    if item.peak >= limits["impact_peak_min"]:
        return "IMPACT"
    if item.peak <= limits["stable_peak_max"] and item.rms <= limits["stable_rms_max"]:
        return "STABLE"
    return "VIBRATION"


def confusion(
    pairs: Iterable[Tuple[str, str]], classes: Sequence[str]
) -> Tuple[Dict[str, Dict[str, int]], int, int]:
    matrix = {actual: {predicted: 0 for predicted in classes} for actual in classes}
    total = 0
    correct = 0
    for actual, predicted in pairs:
        if actual not in matrix or predicted not in matrix[actual]:
            continue
        matrix[actual][predicted] += 1
        total += 1
        if actual == predicted:
            correct += 1
    return matrix, total, correct


def print_confusion(title: str, pairs: Iterable[Tuple[str, str]], classes: Sequence[str]) -> None:
    matrix, total, correct = confusion(pairs, classes)
    print(title)
    print("actual\\predicted," + ",".join(classes))
    for actual in classes:
        print(actual + "," + ",".join(str(matrix[actual][predicted]) for predicted in classes))
    print("total=%d accuracy=%s" % (total, "%.4f" % (correct / total) if total else "N/A"))
    for label in classes:
        denominator = sum(matrix[label].values())
        recall = matrix[label][label] / denominator if denominator else None
        print("recall[%s]=%s" % (label, "%.4f" % recall if recall is not None else "N/A"))


def load_predictions(path: Path, quality: Quality) -> Dict[Tuple[str, int], str]:
    predictions: Dict[Tuple[str, int], str] = {}
    if not path.is_file():
        quality.add("prediction_file_missing", str(path))
        return predictions
    try:
        with path.open("r", newline="", encoding="utf-8-sig") as handle:
            reader = csv.DictReader(handle)
            if not reader.fieldnames or not PREDICTION_FIELDS.issubset(set(reader.fieldnames)):
                quality.add("prediction_header_invalid", str(path))
                return predictions
            for row in reader:
                try:
                    session_id = _safe_session(row.get("session_id", ""))
                    window_seq = _parse_uint(row.get("window_seq", ""), "window_seq", 0xFFFFFFFF)
                    predicted = row.get("predicted_class", "")
                    if not predicted or len(predicted) > 64:
                        raise ValueError("predicted_class is empty")
                except (TypeError, ValueError) as exc:
                    quality.add("prediction_bad_row", "%s line %d (%s)" % (path, reader.line_num, exc))
                    continue
                key = (session_id, window_seq)
                if key in predictions:
                    quality.add("prediction_duplicate", "%s/%d" % key)
                    continue
                predictions[key] = predicted
    except (OSError, UnicodeError) as exc:
        quality.add("prediction_read_error", "%s (%s)" % (path, exc))
    return predictions


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-dir", type=Path, required=True)
    parser.add_argument("--metadata", type=Path)
    parser.add_argument("--test-session", action="append", default=[])
    parser.add_argument("--predictions", type=Path)
    parser.add_argument("--stable-peak-max", type=float)
    parser.add_argument("--stable-rms-max", type=float)
    parser.add_argument("--impact-peak-min", type=float)
    args = parser.parse_args(argv)

    classes = list(DEFAULT_CLASSES)
    metadata_path = args.metadata or (args.data_dir / "dataset_metadata.json")
    quality = Quality()
    _metadata, scale = read_metadata(metadata_path, quality)
    windows, session_labels = load_windows(args.data_dir, quality)
    observed_labels = {label for labels in session_labels.values() for label in labels}
    for label in sorted(observed_labels - set(DEFAULT_CLASSES)):
        quality.add("unexpected_label", label)

    print("Dataset quality")
    print("  data_dir=%s" % args.data_dir)
    print("  files=%d windows=%d" % (len(list(args.data_dir.glob("*.csv"))), len(windows)))
    print("  sessions=%d labels=%s" % (len(session_labels), sorted({w.label for w in windows})))
    print("  axis_order=%s" % ("ax_raw,ay_raw,az_raw" if "axis_metadata_invalid" not in quality.counts and "axis_metadata_missing" not in quality.counts else "INVALID/MISSING"))
    if scale is None:
        print("  metric_units=raw int16 residual units (raw-only baseline; no g-unit claim)")
        print("  calibration_reproducibility=UNAVAILABLE (metadata is missing or inconsistent with current firmware)")
    else:
        print("  metric_units=g (raw_scale_g_per_lsb=%g; MPU6050 +/-4g, 8192 LSB/g)" % scale)
    if quality.counts:
        print("  issue_counts=" + json.dumps(quality.counts, ensure_ascii=False, sort_keys=True))
    if quality.warning_counts:
        print("  warning_counts=" + json.dumps(quality.warning_counts, ensure_ascii=False, sort_keys=True))
    print("  status=%s" % ("PASS" if quality.ok and not quality.warnings else "LIMITED" if quality.ok else "FAIL"))
    for issue in quality.issues:
        print("  issue: %s" % issue)
    if len(quality.issues) == 40:
        print("  issue: further issues omitted from display")
    for warning in quality.warnings:
        print("  warning: %s" % warning)
    if len(quality.warnings) == 40:
        print("  warning: further warnings omitted from display")

    test_ids = set(args.test_session)
    if not test_ids:
        print("Independent session evaluation: unavailable; pass --test-session once per held-out session.")
        print("Model evaluation: unavailable; no real model prediction export was supplied.")
        return 2 if not quality.ok else 0

    known_sessions = set(session_labels)
    unknown_test = sorted(test_ids - known_sessions)
    if unknown_test:
        print("Independent session evaluation: unavailable; unknown test sessions=%s" % unknown_test)
        return 2

    test_windows = [window for window in windows if window.session_id in test_ids and window.label in classes]
    train_windows = [window for window in windows if window.session_id not in test_ids and window.label in classes]
    print("Session split")
    print("  train_sessions=%s windows=%d" % (sorted(known_sessions - test_ids), len(train_windows)))
    print("  test_sessions=%s windows=%d" % (sorted(test_ids), len(test_windows)))

    test_sessions_by_class = {
        label: sorted(
            {window.session_id for window in test_windows if window.label == label}
        )
        for label in classes
    }
    missing_test_classes = [
        label for label in classes if not test_sessions_by_class[label]
    ]
    if missing_test_classes:
        print(
            "Scores unavailable: held-out test data must have at least one "
            "complete independent session for each class; missing=%s"
            % missing_test_classes
        )
        print(
            "  test_sessions_by_class=%s"
            % json.dumps(test_sessions_by_class, ensure_ascii=False, sort_keys=True)
        )
        print("No partial independent score was reported.")
        return 2

    if not quality.ok:
        print("Scores unavailable: fix the dataset quality issues above; no missing/duplicate rows are imputed.")
        print("Model evaluation: unavailable until a clean, complete prediction export is supplied.")
        return 2
    if not test_windows or not train_windows:
        print("Scores unavailable: both independent test windows and training windows are required.")
        print("Model evaluation: unavailable; no score was fabricated.")
        return 2

    limits, origin = thresholds(
        train_windows,
        scale,
        args.stable_peak_max,
        args.stable_rms_max,
        args.impact_peak_min,
    )
    if limits is None:
        print("Threshold baseline: unavailable; %s" % origin)
    else:
        print("Threshold baseline: peak + RMS residual magnitude (%s)" % origin)
        print("  thresholds=" + json.dumps(limits, sort_keys=True))
        pairs = [
            (window.label, classify_baseline(metrics(window, scale), limits)) for window in test_windows
        ]
        print_confusion("  baseline confusion matrix", pairs, classes)

    if args.predictions is None:
        print("Model evaluation: unavailable; no real model prediction export was supplied.")
        print("Expected prediction CSV columns: session_id,window_seq,predicted_class")
        return 0 if limits is not None else 2

    if scale is None:
        print(
            "Model evaluation: unavailable; calibration metadata is missing or "
            "inconsistent, so g-unit input scaling and model reproducibility "
            "cannot be claimed."
        )
        return 2

    prediction_quality = Quality()
    predictions = load_predictions(args.predictions, prediction_quality)
    test_by_key = {(window.session_id, window.window_seq): window for window in test_windows}
    model_pairs: List[Tuple[str, str]] = []
    missing = []
    for key, window in sorted(test_by_key.items()):
        predicted = predictions.get(key)
        if predicted is None:
            missing.append(key)
        else:
            model_pairs.append((window.label, predicted))
    if prediction_quality.issues:
        print("Model prediction quality: FAIL")
        for issue in prediction_quality.issues:
            print("  prediction_issue: %s" % issue)
    if missing:
        print("Model evaluation: unavailable; missing predictions for %d/%d test windows." % (len(missing), len(test_by_key)))
        print("No partial model score was reported.")
        return 2
    if prediction_quality.issues:
        print("Model evaluation: unavailable; prediction rows were malformed or duplicated.")
        return 2
    unknown_predictions = sorted({predicted for _actual, predicted in model_pairs if predicted not in classes})
    if unknown_predictions:
        print("Model evaluation: unavailable; predictions contain unknown classes=%s" % unknown_predictions)
        return 2
    print(
        "Model evaluation: real prediction export=%s "
        "(current MPU6050 calibration metadata is consistent)" % args.predictions
    )
    print_confusion("  model confusion matrix", model_pairs, classes)
    return 0 if limits is not None else 2


if __name__ == "__main__":
    raise SystemExit(main())
