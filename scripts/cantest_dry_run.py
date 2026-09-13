#!/usr/bin/env python3
"""Strict, non-executing `.cantest` validator and policy simulator.

This P1 implementation intentionally has no arm, install, scheduler, or CAN-driver
integration. A valid profile must be disabled and `send:false`; dry-run output is
therefore evidence for review, never permission to transmit.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any

SCHEMA = "t2can-cantest-v1"
REPORT_SCHEMA = "t2can-cantest-dry-run-v1"
SEMANTIC_DENY_IDS = {0x229: "park_button_tx_prohibited"}
BUSES = {"party", "chassis", "vehicle"}
CONFIDENCE = {"unknown", "observed", "inferred", "confirmed"}
POLICIES = {"none", "unverified", "observed_only", "verified"}
REQUIRED_KEYS = {
    "schema",
    "name",
    "feature",
    "enabled",
    "installed",
    "send",
    "target_bus",
    "can_id",
    "dlc",
    "mux",
    "cadence_ms",
    "counter_policy",
    "checksum_policy",
    "profile",
    "layout",
    "prerequisites",
    "maximum_duration_ms",
    "abort_conditions",
    "evidence",
    "mutation_diff",
}


class ProfileError(ValueError):
    pass


def read_profile(path: Path) -> tuple[dict[str, Any], str]:
    raw = path.read_bytes()
    try:
        document = json.loads(raw)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ProfileError(f"invalid_json:{exc}") from exc
    if not isinstance(document, dict):
        raise ProfileError("profile_must_be_an_object")
    return document, hashlib.sha256(raw).hexdigest()


def parse_can_id(value: Any) -> int:
    if isinstance(value, int) and not isinstance(value, bool):
        result = value
    elif isinstance(value, str) and re.fullmatch(r"0x[0-9A-Fa-f]{1,3}", value):
        result = int(value, 16)
    else:
        raise ProfileError("can_id_must_be_standard_hex_or_integer")
    if not 0 <= result <= 0x7FF:
        raise ProfileError("can_id_out_of_standard_range")
    return result


def require_string(profile: dict[str, Any], key: str) -> str:
    value = profile[key]
    if not isinstance(value, str) or not value.strip():
        raise ProfileError(f"{key}_must_be_nonempty_string")
    return value


def require_string_list(profile: dict[str, Any], key: str, *, nonempty: bool) -> list[str]:
    value = profile[key]
    if not isinstance(value, list) or (nonempty and not value):
        raise ProfileError(f"{key}_must_be_{'nonempty_' if nonempty else ''}list")
    if any(not isinstance(item, str) or not item.strip() for item in value):
        raise ProfileError(f"{key}_items_must_be_nonempty_strings")
    return value


def validate_evidence(value: Any) -> list[dict[str, str]]:
    if not isinstance(value, list):
        raise ProfileError("evidence_must_be_list")
    result: list[dict[str, str]] = []
    for item in value:
        if not isinstance(item, dict) or set(item) != {"ref", "revision", "confidence"}:
            raise ProfileError("evidence_item_shape_invalid")
        ref = item["ref"]
        revision = item["revision"]
        confidence = item["confidence"]
        if not all(isinstance(field, str) and field.strip() for field in (ref, revision)):
            raise ProfileError("evidence_ref_and_revision_required")
        if confidence not in CONFIDENCE:
            raise ProfileError("evidence_confidence_invalid")
        result.append({"ref": ref, "revision": revision, "confidence": confidence})
    return result


def validate_mutation_diff(value: Any) -> list[dict[str, str]]:
    if not isinstance(value, list):
        raise ProfileError("mutation_diff_must_be_list")
    result: list[dict[str, str]] = []
    for item in value:
        if not isinstance(item, dict) or set(item) != {"field", "from", "to"}:
            raise ProfileError("mutation_diff_item_shape_invalid")
        if not all(isinstance(item[key], str) and item[key].strip() for key in item):
            raise ProfileError("mutation_diff_values_must_be_nonempty_strings")
        result.append({key: item[key] for key in ("field", "from", "to")})
    return result


def validate_mux(value: Any) -> None:
    if value is None:
        return
    if not isinstance(value, dict) or set(value) != {"byte", "mask", "value"}:
        raise ProfileError("mux_must_be_null_or_exact_object")
    byte = value["byte"]
    mask = value["mask"]
    selected = value["value"]
    if not all(isinstance(field, int) and not isinstance(field, bool) for field in (byte, mask, selected)):
        raise ProfileError("mux_fields_must_be_integers")
    if not 0 <= byte <= 7 or not 1 <= mask <= 0xFF or selected & ~mask:
        raise ProfileError("mux_fields_out_of_range")


def validate_profile(profile: dict[str, Any]) -> dict[str, Any]:
    missing = sorted(REQUIRED_KEYS - set(profile))
    extra = sorted(set(profile) - REQUIRED_KEYS)
    if missing:
        raise ProfileError("missing_keys:" + ",".join(missing))
    if extra:
        raise ProfileError("unknown_keys:" + ",".join(extra))
    if profile["schema"] != SCHEMA:
        raise ProfileError("unsupported_schema")
    if profile["enabled"] is not False:
        raise ProfileError("enabled_must_be_false")
    if profile["installed"] is not False:
        raise ProfileError("installed_must_be_false")
    if profile["send"] is not False:
        raise ProfileError("send_must_be_false")

    name = require_string(profile, "name")
    feature = require_string(profile, "feature")
    bus = require_string(profile, "target_bus")
    if bus not in BUSES:
        raise ProfileError("target_bus_must_be_explicit")
    profile_name = require_string(profile, "profile")
    layout = require_string(profile, "layout")
    if profile_name == "auto" or layout == "auto":
        raise ProfileError("profile_and_layout_may_not_be_inferred")

    can_id = parse_can_id(profile["can_id"])
    dlc = profile["dlc"]
    cadence = profile["cadence_ms"]
    duration = profile["maximum_duration_ms"]
    if not isinstance(dlc, int) or isinstance(dlc, bool) or not 0 <= dlc <= 8:
        raise ProfileError("dlc_out_of_range")
    if not isinstance(cadence, int) or isinstance(cadence, bool) or not 1 <= cadence <= 60_000:
        raise ProfileError("cadence_ms_out_of_range")
    if not isinstance(duration, int) or isinstance(duration, bool) or not 1 <= duration <= 300_000:
        raise ProfileError("maximum_duration_ms_out_of_range")
    validate_mux(profile["mux"])

    counter_policy = require_string(profile, "counter_policy")
    checksum_policy = require_string(profile, "checksum_policy")
    if counter_policy not in POLICIES or checksum_policy not in POLICIES:
        raise ProfileError("counter_or_checksum_policy_invalid")

    prerequisites = require_string_list(profile, "prerequisites", nonempty=True)
    abort_conditions = require_string_list(profile, "abort_conditions", nonempty=True)
    evidence = validate_evidence(profile["evidence"])
    mutation_diff = validate_mutation_diff(profile["mutation_diff"])
    return {
        "name": name,
        "feature": feature,
        "target_bus": bus,
        "can_id": can_id,
        "dlc": dlc,
        "mux": profile["mux"],
        "cadence_ms": cadence,
        "counter_policy": counter_policy,
        "checksum_policy": checksum_policy,
        "profile": profile_name,
        "layout": layout,
        "prerequisites": prerequisites,
        "maximum_duration_ms": duration,
        "abort_conditions": abort_conditions,
        "evidence": evidence,
        "mutation_diff": mutation_diff,
    }


def run_pipeline(profile: dict[str, Any], source_sha256: str) -> dict[str, Any]:
    normalized = validate_profile(profile)
    can_id = normalized["can_id"]
    blockers = ["p1_dry_run_only", "profile_send_false", "profile_not_installed"]
    if can_id in SEMANTIC_DENY_IDS:
        blockers.append(SEMANTIC_DENY_IDS[can_id])
    if normalized["counter_policy"] != "verified":
        blockers.append("counter_policy_not_verified")
    if normalized["checksum_policy"] != "verified":
        blockers.append("checksum_policy_not_verified")
    if not normalized["evidence"]:
        blockers.append("no_evidence")

    return {
        "schema": REPORT_SCHEMA,
        "source_sha256": source_sha256,
        "stages": ["parse", "validate", "dry_run", "policy_simulation"],
        "validated": True,
        "normalized": {
            **normalized,
            "can_id": f"0x{can_id:03X}",
        },
        "dry_run": {
            "send": False,
            "installed": False,
            "armed": False,
            "execution_supported": False,
            "physical_attempts": 0,
            "mutation_diff": normalized["mutation_diff"],
        },
        "policy_simulation": {
            "eligible": False,
            "blockers": blockers,
            "semantic_deny": can_id in SEMANTIC_DENY_IDS,
        },
        "limitations": [
            "No CAN driver, scheduler, arm, install, or execute path exists in this tool.",
            "A valid dry-run is not evidence that a command is safe or effective.",
            "Target bus, vehicle profile, and layout are declarations, never inferred values.",
        ],
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", type=Path)
    parser.add_argument("--json-out", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        profile, digest = read_profile(args.profile)
        report = run_pipeline(profile, digest)
    except (OSError, ProfileError) as exc:
        print(f"cantest_invalid:{exc}", file=sys.stderr)
        return 2
    rendered = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.json_out:
        args.json_out.write_text(rendered, encoding="utf-8")
    else:
        print(rendered, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
