from __future__ import annotations

import base64
import hashlib
import json
import os
import stat
import sys
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from tempfile import TemporaryDirectory
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "collect_vehicle_incidents.py"

import importlib.util

spec = importlib.util.spec_from_file_location("collect_vehicle_incidents", SCRIPT)
if spec is None or spec.loader is None:
    raise RuntimeError("could not load incident sync module")
collector = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = collector
spec.loader.exec_module(collector)


class _IncidentHandler(BaseHTTPRequestHandler):
    username = "reader"
    password = "test-only-password"
    body = b'{"schema":"t2can-flight-recorder-v1"}\n'
    tampered_body: bytes | None = None
    acknowledged: set[str] = set()
    ack_requests: list[str] = []

    @classmethod
    def reset(cls):
        cls.tampered_body = None
        cls.acknowledged = set()
        cls.ack_requests = []

    def _authorized(self) -> bool:
        expected = "Basic " + base64.b64encode(
            f"{self.username}:{self.password}".encode("utf-8")
        ).decode("ascii")
        if self.headers.get("Authorization") == expected:
            return True
        self.send_response(401)
        self.send_header("Content-Length", "0")
        self.end_headers()
        return False

    def _send_json(self, payload: dict, status: int = 200):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):  # noqa: N802 - BaseHTTPRequestHandler API
        if not self._authorized():
            return
        parsed = urlparse(self.path)
        if parsed.path == "/event_list":
            after = int(parse_qs(parsed.query).get("after", ["0"])[0])
            body_size = len(self.body)
            body_sha = hashlib.sha256(self.body).hexdigest()
            incidents = [
                {
                    "id": "1-0",
                    "sequence": 1,
                    "slot": 0,
                    "size": body_size,
                    "sha256": body_sha,
                    "acknowledged": "1-0" in self.acknowledged,
                },
                {
                    "id": "2-0",
                    "sequence": 2,
                    "slot": 0,
                    "size": body_size,
                    "sha256": body_sha,
                    "acknowledged": True,
                },
            ]
            incidents = [item for item in incidents if item["sequence"] > after]
            self._send_json(
                {
                    "schema": "t2can-incident-list-v1",
                    "boardId": "t2can-test-board",
                    "pending": sum(not item["acknowledged"] for item in incidents),
                    "acknowledged": sum(item["acknowledged"] for item in incidents),
                    "truncated": False,
                    "incidents": incidents,
                }
            )
            return
        if parsed.path == "/event_download":
            event_id = parse_qs(parsed.query).get("id", [""])[0]
            if event_id != "1-0":
                self._send_json({"error": "not found"}, status=404)
                return
            body = self.tampered_body if self.tampered_body is not None else self.body
            digest = hashlib.sha256(self.body).hexdigest()
            self.send_response(200)
            self.send_header("Content-Type", "application/x-ndjson")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("X-T2CAN-Incident-Id", event_id)
            self.send_header("X-T2CAN-Size", str(len(self.body)))
            self.send_header("X-T2CAN-SHA256", digest)
            self.send_header("ETag", f'"{digest}"')
            self.end_headers()
            self.wfile.write(body)
            return
        self._send_json({"error": "not found"}, status=404)

    def do_POST(self):  # noqa: N802 - BaseHTTPRequestHandler API
        if not self._authorized():
            return
        if urlparse(self.path).path != "/event_ack":
            self._send_json({"error": "not found"}, status=404)
            return
        length = int(self.headers.get("Content-Length", "0"))
        values = parse_qs(self.rfile.read(length).decode("ascii"))
        event_id = values.get("id", [""])[0]
        self.ack_requests.append(event_id)
        self.acknowledged.add(event_id)
        self._send_json({"ok": True, "alreadyAcknowledged": False})

    def log_message(self, format, *args):
        return


class IncidentSyncTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), _IncidentHandler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.base_url = f"http://127.0.0.1:{cls.server.server_port}"
        cls.auth = collector.RecorderAuth("reader", "test-only-password")

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=2)

    def setUp(self):
        _IncidentHandler.reset()

    def test_new_event_is_verified_archived_and_acked(self):
        with TemporaryDirectory() as directory:
            result = collector.sync_incidents(self.base_url, self.auth, Path(directory))
            self.assertEqual(
                {
                    "listed": 2,
                    "skippedAcknowledged": 1,
                    "archived": 1,
                    "duplicates": 0,
                    "acknowledged": 1,
                    "errors": [],
                    "ok": True,
                },
                result.to_dict(),
            )
            final = Path(directory) / "board-t2can-test-board" / "incident-1-0.jsonl"
            metadata = final.with_suffix(".jsonl.meta.json")
            self.assertEqual(_IncidentHandler.body, final.read_bytes())
            self.assertEqual(0o600, stat.S_IMODE(final.stat().st_mode))
            self.assertEqual(0o600, stat.S_IMODE(metadata.stat().st_mode))
            self.assertEqual(["1-0"], _IncidentHandler.ack_requests)

            duplicate = collector.sync_incidents(self.base_url, self.auth, Path(directory))
            self.assertEqual(2, duplicate.skipped_acknowledged)
            self.assertEqual(0, duplicate.acknowledged)

    def test_tampered_download_is_not_archived_or_acked(self):
        _IncidentHandler.tampered_body = b"X" * len(_IncidentHandler.body)
        with TemporaryDirectory() as directory:
            result = collector.sync_incidents(self.base_url, self.auth, Path(directory))
            self.assertFalse(result.to_dict()["ok"])
            self.assertIn("1-0: download content mismatch", result.errors[0])
            final = Path(directory) / "board-t2can-test-board" / "incident-1-0.jsonl"
            self.assertFalse(final.exists())
            self.assertEqual([], _IncidentHandler.ack_requests)

    def test_existing_different_bytes_are_a_conflict_and_not_overwritten(self):
        with TemporaryDirectory() as directory:
            final = Path(directory) / "board-t2can-test-board" / "incident-1-0.jsonl"
            final.parent.mkdir(parents=True)
            final.write_bytes(b"do-not-overwrite")
            before = final.read_bytes()
            result = collector.sync_incidents(self.base_url, self.auth, Path(directory))
            self.assertFalse(result.to_dict()["ok"])
            self.assertIn("1-0: archive conflict", result.errors[0])
            self.assertEqual(before, final.read_bytes())
            self.assertEqual([], _IncidentHandler.ack_requests)

    def test_public_host_is_rejected(self):
        with self.assertRaises(collector.IncidentSyncError):
            collector.validate_base_url("https://example.com")


if __name__ == "__main__":
    unittest.main()
