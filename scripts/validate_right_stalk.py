#!/usr/bin/env python3
"""Validate observed SCCM_rightStalk (0x229) structure without enabling TX."""
from __future__ import annotations

import argparse
import hashlib
import json
from collections import Counter, defaultdict
from pathlib import Path
from statistics import mean, median
from typing import Any, Iterable

from ev_can_analyzer import FrameRecord, parse_log_line

TARGET_ID = 0x229
SCHEMA = "t2can-right-stalk-validator-v1"


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Read-only 0x229 candump validator.")
    parser.add_argument("inputs", nargs="+", help="Candump file or directory.")
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


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def timestamp_ms(value: str | None) -> float | None:
    if value is None:
        return None
    try:
        return float(value) * 1000.0
    except ValueError:
        return None


def read_file(path: Path) -> tuple[list[FrameRecord], dict[str, int]]:
    frames: list[FrameRecord] = []
    counters = {"lines": 0, "parsed": 0, "ignored": 0, "parse_errors": 0}
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
            if frame.can_id == TARGET_ID:
                frames.append(frame)
    return frames, counters


def decoded(frame: FrameRecord) -> dict[str, int]:
    if len(frame.data) != 3:
        raise ValueError("0x229 must have exact DLC 3")
    return {
        "crc": frame.data[0],
        "counter": frame.data[1] & 0x0F,
        "stalk": (frame.data[1] >> 4) & 0x07,
        "park": frame.data[2] & 0x03,
        "reserved": ((frame.data[1] >> 7) & 0x01) | ((frame.data[2] >> 2) << 1),
    }


def transition_key(before: tuple[int, int], after: tuple[int, int]) -> str:
    return f"stalk:{before[0]}->stalk:{after[0]},park:{before[1]}->park:{after[1]}"


def summarize_file(path: Path, frames: list[FrameRecord]) -> dict[str, Any]:
    exact = [frame for frame in frames if len(frame.data) == 3]
    rejected = len(frames) - len(exact)
    observations = [decoded(frame) for frame in exact]
    counter_steps = Counter()
    state_transitions = Counter()
    cadence: list[float] = []

    previous_counter: int | None = None
    previous_state: tuple[int, int] | None = None
    previous_time: float | None = None
    for frame, observation in zip(exact, observations):
        counter = observation["counter"]
        state = (observation["stalk"], observation["park"])
        when = timestamp_ms(frame.timestamp)
        if previous_counter is not None:
            delta = (counter - previous_counter) & 0x0F
            counter_steps["sequential" if delta == 1 else "repeat" if delta == 0 else "jump"] += 1
        if previous_state is not None and state != previous_state:
            state_transitions[transition_key(previous_state, state)] += 1
        if previous_time is not None and when is not None and when >= previous_time:
            cadence.append(when - previous_time)
        previous_counter = counter
        previous_state = state
        previous_time = when

    return {
        "source_name": path.name,
        "source_sha256": sha256_file(path),
        "frames": len(frames),
        "exact_dlc3": len(exact),
        "rejected_dlc": rejected,
        "counter_steps": dict(sorted(counter_steps.items())),
        "state_transitions": dict(sorted(state_transitions.items())),
        "cadence_ms": {
            "samples": len(cadence),
            "minimum": round(min(cadence), 6) if cadence else None,
            "median": round(median(cadence), 6) if cadence else None,
            "mean": round(mean(cadence), 6) if cadence else None,
            "maximum": round(max(cadence), 6) if cadence else None,
        },
    }


def build_report(files: list[Path]) -> dict[str, Any]:
    parse_totals = Counter()
    field_counts = {"counter": Counter(), "stalk": Counter(), "park": Counter(), "reserved": Counter()}
    crc_by_body: dict[bytes, set[int]] = defaultdict(set)
    file_reports: list[dict[str, Any]] = []
    total_target = exact_dlc3 = zero_builder_matches = 0
    aggregate_counter_steps = Counter()
    aggregate_transitions = Counter()

    for path in files:
        frames, parse_counts = read_file(path)
        parse_totals.update(parse_counts)
        summary = summarize_file(path, frames)
        file_reports.append(summary)
        total_target += len(frames)
        exact_dlc3 += int(summary["exact_dlc3"])
        aggregate_counter_steps.update(summary["counter_steps"])
        aggregate_transitions.update(summary["state_transitions"])
        for frame in frames:
            if len(frame.data) != 3:
                continue
            observation = decoded(frame)
            for field in field_counts:
                field_counts[field][observation[field]] += 1
            crc_by_body[frame.data[1:]].add(frame.data[0])
            if frame.data == b"\x00\x00\x01":
                zero_builder_matches += 1

    crc_conflicts = sum(1 for values in crc_by_body.values() if len(values) != 1)
    return {
        "schema": SCHEMA,
        "mode": "read_only",
        "tx_capability": False,
        "command_validity": "unknown",
        "profile_confidence": "observed_unconfirmed_vehicle_profile",
        "source_definition": "opendbc tesla_model3_vehicle.dbc SCCM_rightStalk",
        "summary": {
            "files": len(files),
            "files_with_0x229": sum(1 for item in file_reports if item["frames"]),
            "target_frames": total_target,
            "exact_dlc3": exact_dlc3,
            "rejected_dlc": total_target - exact_dlc3,
            "parse": dict(sorted(parse_totals.items())),
            "counter_steps": dict(sorted(aggregate_counter_steps.items())),
            "state_transitions": dict(sorted(aggregate_transitions.items())),
            "field_values": {
                field: {str(key): value for key, value in sorted(counts.items())}
                for field, counts in field_counts.items()
            },
            "crc_body_variants": len(crc_by_body),
            "crc_conflicts_for_same_body": crc_conflicts,
            "upstream_zero_park_builder_observed": zero_builder_matches,
        },
        "files": file_reports,
        "limitations": [
            "The DBC identifies a CRC byte but does not establish its algorithm here.",
            "Observed counter continuity and CRC/body consistency do not prove command validity.",
            "No frame construction, mutation, policy arm, or physical transmission is available.",
        ],
    }


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    files = discover_files(args.inputs)
    if not files:
        raise SystemExit("no input files")
    report = build_report(files)
    rendered = json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    if args.json_out:
        destination = Path(args.json_out).expanduser()
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(rendered, encoding="utf-8")
    else:
        print(rendered, end="")
    return 1 if report["summary"]["parse"]["parse_errors"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
