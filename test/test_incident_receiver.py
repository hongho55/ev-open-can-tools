from __future__ import annotations

import hashlib
import json
import stat
import sys
import threading
import unittest
from http.client import HTTPResponse
from pathlib import Path
from tempfile import TemporaryDirectory
from urllib.error import HTTPError
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "receive_vehicle_incidents.py"

import importlib.util

spec = importlib.util.spec_from_file_location("receive_vehicle_incidents", SCRIPT)
if spec is None or spec.loader is None:
    raise RuntimeError("could not load incident receiver module")
receiver = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = receiver
spec.loader.exec_module(receiver)


class IncidentReceiverTests(unittest.TestCase):
    def _start_server(self, archive: Path):
        server = receiver.create_server("127.0.0.1", 0, archive, "test-upload-token", 1024)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        self.addCleanup(server.shutdown)
        self.addCleanup(server.server_close)
        self.addCleanup(thread.join, 2)
        return server, f"http://127.0.0.1:{server.server_port}/v1/incidents"

    @staticmethod
    def _request(url: str, body: bytes, event_id: str = "1-0", board_id: str = "board-test"):
        digest = hashlib.sha256(body).hexdigest()
        request = Request(
            url,
            data=body,
            method="POST",
            headers={
                "Authorization": "Bearer test-upload-token",
                "Content-Type": "application/x-ndjson",
                "Content-Length": str(len(body)),
                "X-T2CAN-Incident-Id": event_id,
                "X-T2CAN-Board-Id": board_id,
                "X-T2CAN-Size": str(len(body)),
                "X-T2CAN-SHA256": digest,
            },
        )
        return urlopen(request, timeout=2)

    def test_http_upload_commits_and_duplicate_is_idempotent(self):
        body = b'{"schema":"t2can-flight-recorder-v1"}\n'
        with TemporaryDirectory() as directory:
            server, url = self._start_server(Path(directory))
            with self._request(url, body) as response:
                self.assertEqual(201, response.status)
                self.assertEqual("stored", json.loads(response.read())["status"])
            final = Path(directory) / "board-board-test" / "incident-1-0.jsonl"
            metadata = final.with_suffix(".jsonl.meta.json")
            self.assertEqual(body, final.read_bytes())
            self.assertEqual(0o600, stat.S_IMODE(final.stat().st_mode))
            self.assertEqual(0o600, stat.S_IMODE(metadata.stat().st_mode))

            with self._request(url, body) as response:
                self.assertEqual(200, response.status)
                self.assertEqual("already_stored", json.loads(response.read())["status"])

    def test_bad_hash_is_not_committed(self):
        body = b"good\n"
        tampered = b"bad!\n"
        digest = hashlib.sha256(body).hexdigest()
        with TemporaryDirectory() as directory:
            server, url = self._start_server(Path(directory))
            request = Request(
                url,
                data=tampered,
                method="POST",
                headers={
                    "Authorization": "Bearer test-upload-token",
                    "Content-Length": str(len(tampered)),
                    "X-T2CAN-Incident-Id": "1-0",
                    "X-T2CAN-Board-Id": "board-test",
                    "X-T2CAN-Size": str(len(tampered)),
                    "X-T2CAN-SHA256": digest,
                },
            )
            with self.assertRaises(HTTPError) as raised:
                urlopen(request, timeout=2)
            self.assertEqual(400, raised.exception.code)
            final = Path(directory) / "board-board-test" / "incident-1-0.jsonl"
            self.assertFalse(final.exists())

    def test_same_identity_with_different_bytes_is_conflict(self):
        first = b"first\n"
        second = b"second\n"
        with TemporaryDirectory() as directory:
            server, url = self._start_server(Path(directory))
            with self._request(url, first) as response:
                self.assertEqual(201, response.status)
            with self.assertRaises(HTTPError) as raised:
                self._request(url, second)
            self.assertEqual(409, raised.exception.code)
            final = Path(directory) / "board-board-test" / "incident-1-0.jsonl"
            self.assertEqual(first, final.read_bytes())

    def test_receiver_requires_loopback_and_token(self):
        with TemporaryDirectory() as directory:
            with self.assertRaises(receiver.ReceiverError):
                receiver.create_server("0.0.0.0", 0, Path(directory), "token", 1024)
            with self.assertRaises(receiver.ReceiverError):
                receiver.create_server("127.0.0.1", 0, Path(directory), "", 1024)


if __name__ == "__main__":
    unittest.main()
