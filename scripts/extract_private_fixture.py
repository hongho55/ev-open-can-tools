#!/usr/bin/env python3
"""Extract a minimal, non-enabling fixture from a private T-2CAN incident."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable

SCHEMA = "t2can-minimized-fixture-v1"
MAX_SOURCE_BYTES = 16 * 1024 * 1024
MAX_FRAMES = 10_000


class FixtureError(RuntimeError):
    pass


def parse_can_id(value: str) -> int:
    try:
        parsed = int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("CAN ID must be decimal or 0x-prefixed hex") from exc
    if parsed < 0 or parsed > 0x1FFFFFFF:
        raise argparse.ArgumentTypeError("CAN ID is outside the 29-bit range")
    return parsed


def read_private_source(path: Path) -> tuple[bytes, str]:
    path = path.expanduser().resolve()
    if not path.is_file():
        raise FixtureError("source is not a regular file")
    size = path.stat().st_size
    if size > MAX_SOURCE_BYTES:
        raise FixtureError(f"source exceeds {MAX_SOURCE_BYTES} bytes")
    os.chmod(path, 0o600)
    raw = path.read_bytes()
    return raw, hashlib.sha256(raw).hexdigest()


def raw_records(source: bytes) -> Iterable[dict[str, Any]]:
    for line_number, line in enumerate(source.splitlines(), 1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as exc:
            raise FixtureError(f"invalid JSON on line {line_number}") from exc
        if not isinstance(record, dict):
            raise FixtureError(f"line {line_number} is not an object")
        if record.get("type") == "raw":
            yield record


def extract_fixture(
    source_path: Path,
    can_ids: set[int],
    purpose: str,
    expected_result: str,
    start_ms: int | None = None,
    end_ms: int | None = None,
    include_payload: bool = False,
    max_frames: int = MAX_FRAMES,
) -> dict[str, Any]:
    if not can_ids:
        raise FixtureError("at least one CAN ID is required")
    if not purpose.strip() or not expected_result.strip():
        raise FixtureError("purpose and expected result are required")
    if max_frames <= 0 or max_frames > MAX_FRAMES:
        raise FixtureError(f"max_frames must be 1..{MAX_FRAMES}")
    if start_ms is not None and end_ms is not None and start_ms > end_ms:
        raise FixtureError("start_ms must not exceed end_ms")

    source, digest = read_private_source(source_path)
    selected: list[dict[str, Any]] = []
    for record in raw_records(source):
        try:
            frame_id = int(record["id"])
            timestamp = int(record["ms"])
            dlc = int(record["dlc"])
        except (KeyError, TypeError, ValueError) as exc:
            raise FixtureError("raw record has invalid id/ms/dlc") from exc
        if frame_id not in can_ids:
            continue
        if start_ms is not None and timestamp < start_ms:
            continue
        if end_ms is not None and timestamp > end_ms:
            continue
        item = {
            "ms": timestamp,
            "direction": record.get("direction", "rx"),
            "busMask": int(record.get("busMask", 0)),
            "physicalBus": int(record.get("physicalBus", 0)),
            "id": frame_id,
            "dlc": dlc,
        }
        if include_payload:
            data = record.get("data")
            if not isinstance(data, str) or len(data) != dlc * 2:
                raise FixtureError("raw record payload does not match DLC")
            item["data"] = data.upper()
        selected.append(item)
        if len(selected) > max_frames:
            raise FixtureError("selection exceeds max_frames; narrow the window")

    if not selected:
        raise FixtureError("selection produced no frames")
    first_ms = selected[0]["ms"]
    for item in selected:
        item["offsetMs"] = item.pop("ms") - first_ms

    return {
        "schema": SCHEMA,
        "createdAt": datetime.now(timezone.utc).isoformat(),
        "source": {"sha256": digest, "bytes": len(source), "pathIncluded": False},
        "transformation": {
            "ids": sorted(can_ids),
            "startMs": start_ms,
            "endMs": end_ms,
            "payload": "included" if include_payload else "omitted",
            "frameCount": len(selected),
        },
        "purpose": purpose.strip(),
        "expectedResult": expected_result.strip(),
        "txPolicy": {"send": False, "installationState": "disabled"},
        "frames": selected,
    }


def write_private_atomic(value: dict[str, Any], output: Path) -> None:
    output = output.expanduser()
    output.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    os.chmod(output.parent, 0o700)
    temporary = output.with_name(output.name + ".tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    os.chmod(temporary, 0o600)
    os.replace(temporary, output)
    os.chmod(output, 0o600)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--id", dest="ids", type=parse_can_id, action="append", required=True)
    parser.add_argument("--purpose", required=True)
    parser.add_argument("--expected", required=True)
    parser.add_argument("--start-ms", type=int)
    parser.add_argument("--end-ms", type=int)
    parser.add_argument("--include-payload", action="store_true")
    parser.add_argument("--max-frames", type=int, default=MAX_FRAMES)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        fixture = extract_fixture(
            args.source,
            set(args.ids),
            args.purpose,
            args.expected,
            args.start_ms,
            args.end_ms,
            args.include_payload,
            args.max_frames,
        )
        write_private_atomic(fixture, args.output)
    except (FixtureError, OSError) as exc:
        parser.error(str(exc))
    print(args.output.expanduser())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
