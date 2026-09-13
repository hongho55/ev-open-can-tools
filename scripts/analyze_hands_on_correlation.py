#!/usr/bin/env python3
"""Read-only correlation report for CAN 0x247 and 0x3E9 observations.

No signal meaning is inferred. The report measures co-presence, DLC, timing and
bit co-variation only; it cannot generate or transmit a CAN frame.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

from ev_can_analyzer import FrameRecord, parse_log_line

TARGET_A = 0x247
TARGET_B = 0x3E9
SCHEMA = "t2can-hands-on-correlation-v1"


@dataclass(frozen=True)
class TimedFrame:
    timestamp_ms: float
    frame: FrameRecord


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Passively correlate 0x247 and 0x3E9 candump observations."
    )
    parser.add_argument("inputs", nargs="+", help="Candump file or directory.")
    parser.add_argument("--window-ms", type=float, default=100.0)
    parser.add_argument("--json-out", help="Write derived JSON report here.")
    return parser.parse_args(argv)


def discover_files(inputs: Iterable[str]) -> list[Path]:
    files: set[Path] = set()
    for value in inputs:
        path = Path(value).expanduser()
        if path.is_dir():
            files.update(item.resolve() for item in path.rglob("*.log") if item.is_file())
        elif path.is_file():
            files.add(path.resolve())
        else:
            raise FileNotFoundError(value)
    return sorted(files, key=lambda item: str(item))


def timestamp_ms(value: str | None) -> float | None:
    if value is None:
        return None
    try:
        return float(value) * 1000.0
    except ValueError:
        return None


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_targets(path: Path) -> tuple[list[TimedFrame], list[TimedFrame], dict[str, int]]:
    groups: dict[int, list[TimedFrame]] = {TARGET_A: [], TARGET_B: []}
    counters = {"lines": 0, "parsed": 0, "ignored": 0, "parse_errors": 0, "missing_timestamp": 0}
    with path.open("r", encoding="utf-8", errors="replace") as source:
        for line_number, line in enumerate(source, 1):
            counters["lines"] += 1
            try:
                frame = parse_log_line(line, line_number)
            except ValueError:
                counters["parse_errors"] += 1
                continue
            if frame is None:
                counters["ignored"] += 1
                continue
            counters["parsed"] += 1
            if frame.can_id not in groups:
                continue
            when = timestamp_ms(frame.timestamp)
            if when is None:
                counters["missing_timestamp"] += 1
                continue
            groups[frame.can_id].append(TimedFrame(when, frame))
    for values in groups.values():
        values.sort(key=lambda item: (item.timestamp_ms, item.frame.line_number))
    return groups[TARGET_A], groups[TARGET_B], counters


def nearest_pairs(
    left: list[TimedFrame], right: list[TimedFrame], window_ms: float
) -> list[tuple[TimedFrame, TimedFrame, float]]:
    if not left or not right:
        return []
    pairs: list[tuple[TimedFrame, TimedFrame, float]] = []
    cursor = 0
    for first in left:
        while cursor + 1 < len(right) and (
            abs(right[cursor + 1].timestamp_ms - first.timestamp_ms)
            <= abs(right[cursor].timestamp_ms - first.timestamp_ms)
        ):
            cursor += 1
        delta = right[cursor].timestamp_ms - first.timestamp_ms
        if abs(delta) <= window_ms:
            pairs.append((first, right[cursor], delta))
    return pairs


def bit_value(payload: bytes, bit: int) -> int:
    return (payload[bit // 8] >> (bit % 8)) & 1


def phi(table: tuple[int, int, int, int]) -> float | None:
    n00, n01, n10, n11 = table
    denominator = (n10 + n11) * (n00 + n01) * (n01 + n11) * (n00 + n10)
    if denominator == 0:
        return None
    return (n11 * n00 - n10 * n01) / math.sqrt(denominator)


def bit_correlations(
    pairs: list[tuple[TimedFrame, TimedFrame, float]], limit: int = 16
) -> list[dict[str, object]]:
    tables: dict[tuple[int, int], list[int]] = {}
    for left, right, _delta in pairs:
        for left_bit in range(len(left.frame.data) * 8):
            left_value = bit_value(left.frame.data, left_bit)
            for right_bit in range(len(right.frame.data) * 8):
                right_value = bit_value(right.frame.data, right_bit)
                table = tables.setdefault((left_bit, right_bit), [0, 0, 0, 0])
                table[left_value * 2 + right_value] += 1

    result: list[dict[str, Any]] = []
    for (left_bit, right_bit), mutable in tables.items():
        table = tuple(mutable)
        coefficient = phi(table)  # type: ignore[arg-type]
        if coefficient is None:
            continue
        result.append(
            {
                "bit_0x247": left_bit,
                "bit_0x3E9": right_bit,
                "phi": round(coefficient, 6),
                "observations": sum(table),
                "contingency_00_01_10_11": list(table),
            }
        )
    result.sort(
        key=lambda item: (
            -abs(float(item["phi"])),
            int(item["bit_0x247"]),
            int(item["bit_0x3E9"]),
        )
    )
    return result[:limit]


def dlc_counts(frames: list[TimedFrame]) -> dict[str, int]:
    return {str(key): value for key, value in sorted(Counter(len(x.frame.data) for x in frames).items())}


def build_report(files: list[Path], window_ms: float) -> dict[str, Any]:
    if window_ms < 0:
        raise ValueError("window-ms must be non-negative")
    all_pairs: list[tuple[TimedFrame, TimedFrame, float]] = []
    file_reports: list[dict[str, object]] = []
    totals = Counter()

    for path in files:
        left, right, counters = read_targets(path)
        pairs = nearest_pairs(left, right, window_ms)
        all_pairs.extend(pairs)
        totals.update(counters)
        totals["frames_0x247"] += len(left)
        totals["frames_0x3E9"] += len(right)
        totals["nearest_pairs"] += len(pairs)
        file_reports.append(
            {
                "source_sha256": sha256_file(path),
                "source_name": path.name,
                "parse": counters,
                "frames": {"0x247": len(left), "0x3E9": len(right)},
                "dlc": {"0x247": dlc_counts(left), "0x3E9": dlc_counts(right)},
                "nearest_pairs": len(pairs),
            }
        )

    deltas = [delta for _left, _right, delta in all_pairs]
    return {
        "schema": SCHEMA,
        "mode": "read_only",
        "semantics": "unknown",
        "tx_capability": False,
        "window_ms": window_ms,
        "summary": dict(sorted(totals.items())),
        "timing": {
            "paired": len(deltas),
            "minimum_delta_ms": round(min(deltas), 6) if deltas else None,
            "maximum_delta_ms": round(max(deltas), 6) if deltas else None,
            "mean_absolute_delta_ms": (
                round(sum(abs(value) for value in deltas) / len(deltas), 6) if deltas else None
            ),
        },
        "strongest_bit_covariations": bit_correlations(all_pairs),
        "files": file_reports,
        "limitations": [
            "Nearest-time pairing is descriptive and does not prove signal identity or causation.",
            "No payload mutation, candidate layout application, or CAN transmission is available.",
        ],
    }


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    files = discover_files(args.inputs)
    if not files:
        raise SystemExit("no input files")
    report = build_report(files, args.window_ms)
    rendered = json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    if args.json_out:
        destination = Path(args.json_out).expanduser()
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(rendered, encoding="utf-8")
    else:
        print(rendered, end="")
    return 1 if report["summary"].get("parse_errors", 0) else 0


if __name__ == "__main__":
    raise SystemExit(main())
