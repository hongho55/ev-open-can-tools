"""Focused tests for private incident fixture extraction."""
from __future__ import annotations

import hashlib
import importlib.util
import json
import stat
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/extract_private_fixture.py"
spec = importlib.util.spec_from_file_location("extract_private_fixture", SCRIPT)
assert spec and spec.loader
fixture_tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture_tool)


class PrivateFixtureTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        records = [
            {"type": "header", "boardId": "private-board-id"},
            {"type": "raw", "ms": 100, "direction": "rx", "busMask": 1,
             "physicalBus": 0, "id": 0x247, "dlc": 2, "data": "A1B2"},
            {"type": "raw", "ms": 120, "direction": "rx", "busMask": 2,
             "physicalBus": 1, "id": 0x3E9, "dlc": 1, "data": "CC"},
            {"type": "raw", "ms": 140, "direction": "rx", "busMask": 1,
             "physicalBus": 0, "id": 0x247, "dlc": 2, "data": "D3E4"},
        ]
        self.source_bytes = b"".join(
            (json.dumps(record) + "\n").encode("utf-8") for record in records
        )
        self.source = self.root / "incident.jsonl"
        self.source.write_bytes(self.source_bytes)
        self.source.chmod(0o644)

    def tearDown(self) -> None:
        self.temp.cleanup()

    def test_default_fixture_is_minimized_and_payload_free(self) -> None:
        fixture = fixture_tool.extract_fixture(
            self.source, {0x247}, "decoder transition", "two observations"
        )
        self.assertEqual([frame["offsetMs"] for frame in fixture["frames"]], [0, 40])
        self.assertTrue(all("data" not in frame for frame in fixture["frames"]))
        self.assertEqual(fixture["transformation"]["payload"], "omitted")
        self.assertEqual(fixture["txPolicy"], {"send": False, "installationState": "disabled"})

    def test_provenance_uses_digest_without_source_path_or_header_identity(self) -> None:
        fixture = fixture_tool.extract_fixture(self.source, {0x247}, "p", "e")
        self.assertEqual(fixture["source"]["sha256"], hashlib.sha256(self.source_bytes).hexdigest())
        encoded = json.dumps(fixture)
        self.assertNotIn(str(self.source), encoded)
        self.assertNotIn("private-board-id", encoded)

    def test_payload_requires_explicit_opt_in(self) -> None:
        fixture = fixture_tool.extract_fixture(
            self.source, {0x3E9}, "parser", "one frame", include_payload=True
        )
        self.assertEqual(fixture["frames"][0]["data"], "CC")

    def test_source_and_atomic_output_are_private(self) -> None:
        fixture = fixture_tool.extract_fixture(self.source, {0x247}, "p", "e")
        output = self.root / "private" / "fixture.json"
        fixture_tool.write_private_atomic(fixture, output)
        self.assertEqual(stat.S_IMODE(self.source.stat().st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(output.parent.stat().st_mode), 0o700)
        self.assertEqual(stat.S_IMODE(output.stat().st_mode), 0o600)


if __name__ == "__main__":
    unittest.main()
