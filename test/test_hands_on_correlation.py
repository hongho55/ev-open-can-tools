"""Focused tests for read-only 0x247/0x3E9 correlation."""
from __future__ import annotations

import hashlib
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

_SPEC = importlib.util.spec_from_file_location(
    "analyze_hands_on_correlation", ROOT / "scripts/analyze_hands_on_correlation.py"
)
assert _SPEC is not None and _SPEC.loader is not None
correlation = importlib.util.module_from_spec(_SPEC)
sys.modules[_SPEC.name] = correlation
_SPEC.loader.exec_module(correlation)


SAMPLE = """# synthetic observations
(1.000) can0 247#0000000000000000
(1.010) can0 3E9#0000000000000000
(2.000) can0 247#0100000000000000
(2.015) can0 3E9#0100000000000000
(3.000) can0 123#AABB
"""


class HandsOnCorrelationTests(unittest.TestCase):
    def test_timestamp_and_nearest_pairing(self) -> None:
        self.assertEqual(1250.0, correlation.timestamp_ms("1.25"))
        self.assertIsNone(correlation.timestamp_ms(None))
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "sample.log"
            source.write_text(SAMPLE, encoding="utf-8")
            left, right, counters = correlation.read_targets(source)
            pairs = correlation.nearest_pairs(left, right, 20.0)
        self.assertEqual(5, counters["parsed"])
        self.assertEqual(2, len(left))
        self.assertEqual(2, len(right))
        self.assertEqual([10.0, 15.0], [round(pair[2], 3) for pair in pairs])

    def test_report_is_derived_read_only_and_path_free(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "private-vehicle.log"
            source.write_text(SAMPLE, encoding="utf-8")
            report = correlation.build_report([source], 20.0)
        self.assertEqual("read_only", report["mode"])
        self.assertEqual("unknown", report["semantics"])
        self.assertFalse(report["tx_capability"])
        self.assertEqual(2, report["summary"]["nearest_pairs"])
        self.assertEqual(hashlib.sha256(SAMPLE.encode()).hexdigest(), report["files"][0]["source_sha256"])
        rendered = json.dumps(report)
        self.assertNotIn(directory, rendered)
        self.assertNotIn("0000000000000000", rendered)

    def test_bit_covariation_is_descriptive(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "sample.log"
            source.write_text(SAMPLE, encoding="utf-8")
            report = correlation.build_report([source], 20.0)
        strongest = report["strongest_bit_covariations"]
        self.assertTrue(strongest)
        self.assertEqual(0, strongest[0]["bit_0x247"])
        self.assertEqual(0, strongest[0]["bit_0x3E9"])
        self.assertEqual(1.0, strongest[0]["phi"])

    def test_invalid_lines_are_counted(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "bad.log"
            source.write_text("not-candump\n", encoding="utf-8")
            _left, _right, counters = correlation.read_targets(source)
        self.assertEqual(1, counters["parse_errors"])


if __name__ == "__main__":
    unittest.main()
