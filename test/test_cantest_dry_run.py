"""Focused contract tests for the P1 dry-run-only `.cantest` workflow."""
from __future__ import annotations

import copy
import importlib.util
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/cantest_dry_run.py"
FIXTURE = ROOT / "test/fixtures/cantest/right_stalk_readonly.cantest"
_SPEC = importlib.util.spec_from_file_location("cantest_dry_run", SCRIPT)
assert _SPEC is not None and _SPEC.loader is not None
cantest = importlib.util.module_from_spec(_SPEC)
sys.modules[_SPEC.name] = cantest
_SPEC.loader.exec_module(cantest)


class CanTestDryRunTests(unittest.TestCase):
    def setUp(self) -> None:
        self.profile = json.loads(FIXTURE.read_text(encoding="utf-8"))

    def test_right_stalk_profile_is_valid_but_semantically_denied(self) -> None:
        report = cantest.run_pipeline(self.profile, "0" * 64)
        self.assertTrue(report["validated"])
        self.assertFalse(report["dry_run"]["send"])
        self.assertFalse(report["dry_run"]["installed"])
        self.assertFalse(report["dry_run"]["armed"])
        self.assertFalse(report["dry_run"]["execution_supported"])
        self.assertEqual(0, report["dry_run"]["physical_attempts"])
        self.assertFalse(report["policy_simulation"]["eligible"])
        self.assertTrue(report["policy_simulation"]["semantic_deny"])
        self.assertIn("park_button_tx_prohibited", report["policy_simulation"]["blockers"])

    def test_send_enabled_or_installed_profiles_are_rejected(self) -> None:
        for key in ("send", "enabled", "installed"):
            with self.subTest(key=key):
                profile = copy.deepcopy(self.profile)
                profile[key] = True
                with self.assertRaises(cantest.ProfileError):
                    cantest.validate_profile(profile)

    def test_bus_profile_and_layout_are_never_inferred(self) -> None:
        for key, value in (("target_bus", "auto"), ("profile", "auto"), ("layout", "auto")):
            with self.subTest(key=key):
                profile = copy.deepcopy(self.profile)
                profile[key] = value
                with self.assertRaises(cantest.ProfileError):
                    cantest.validate_profile(profile)

    def test_schema_is_strict_and_no_execution_module_is_imported(self) -> None:
        profile = copy.deepcopy(self.profile)
        profile["surprise"] = "ignored-by-unsafe-parser"
        with self.assertRaises(cantest.ProfileError):
            cantest.validate_profile(profile)
        source = SCRIPT.read_text(encoding="utf-8")
        self.assertNotIn("import can", source)
        self.assertNotIn("import socket", source)
        self.assertNotIn("import serial", source)
        self.assertNotIn("subprocess", source)

    def test_output_is_deterministic_and_preserves_declared_mutation_diff(self) -> None:
        profile = copy.deepcopy(self.profile)
        profile["can_id"] = "0x247"
        profile["feature"] = "hands_on_research"
        profile["mutation_diff"] = [
            {"field": "candidate_bit", "from": "observed", "to": "proposed_only"}
        ]
        first = cantest.run_pipeline(profile, "1" * 64)
        second = cantest.run_pipeline(profile, "1" * 64)
        self.assertEqual(first, second)
        self.assertEqual(profile["mutation_diff"], first["dry_run"]["mutation_diff"])
        self.assertFalse(first["policy_simulation"]["eligible"])


if __name__ == "__main__":
    unittest.main()
