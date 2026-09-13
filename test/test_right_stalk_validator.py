"""Focused tests for the read-only 0x229 validator."""
from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
_SPEC = importlib.util.spec_from_file_location(
    "validate_right_stalk", ROOT / "scripts/validate_right_stalk.py"
)
assert _SPEC is not None and _SPEC.loader is not None
validator = importlib.util.module_from_spec(_SPEC)
sys.modules[_SPEC.name] = validator
_SPEC.loader.exec_module(validator)

SAMPLE = """(1.000) can0 229#460000
(1.010) can0 229#440100
(1.020) can0 229#523200
(1.030) can0 229#AABB
(1.040) can0 123#00
"""


class RightStalkValidatorTests(unittest.TestCase):
    def test_decode_exact_dlc_fields(self) -> None:
        frame = validator.FrameRecord(1, "1.0", "can0", 0x229, bytes.fromhex("AA3901"), "")
        self.assertEqual(
            {"crc": 0xAA, "counter": 9, "stalk": 3, "park": 1, "reserved": 0},
            validator.decoded(frame),
        )
        with self.assertRaises(ValueError):
            validator.decoded(validator.FrameRecord(1, "1", "can0", 0x229, b"\x00\x00", ""))

    def test_report_validates_shape_counter_and_state(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "private.log"
            source.write_text(SAMPLE, encoding="utf-8")
            report = validator.build_report([source])
        summary = report["summary"]
        self.assertEqual(4, summary["target_frames"])
        self.assertEqual(3, summary["exact_dlc3"])
        self.assertEqual(1, summary["rejected_dlc"])
        self.assertEqual(2, summary["counter_steps"]["sequential"])
        self.assertEqual(1, sum(summary["state_transitions"].values()))
        self.assertFalse(report["tx_capability"])
        self.assertEqual("unknown", report["command_validity"])

    def test_report_does_not_expose_payload_or_absolute_path(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "private.log"
            source.write_text(SAMPLE, encoding="utf-8")
            rendered = json.dumps(validator.build_report([source]))
        self.assertNotIn(directory, rendered)
        self.assertNotIn("460000", rendered)
        self.assertNotIn("523200", rendered)

    def test_upstream_zero_builder_is_observation_only(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "sample.log"
            source.write_text("(1.0) can0 229#000001\n", encoding="utf-8")
            report = validator.build_report([source])
        self.assertEqual(1, report["summary"]["upstream_zero_park_builder_observed"])
        self.assertFalse(report["tx_capability"])


if __name__ == "__main__":
    unittest.main()
