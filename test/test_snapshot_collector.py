from __future__ import annotations

import importlib.util
import json
import os
import stat
import sys
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from tempfile import TemporaryDirectory
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "collect_vehicle_snapshot.py"


spec = importlib.util.spec_from_file_location("collect_vehicle_snapshot", SCRIPT)
if spec is None or spec.loader is None:
    raise RuntimeError("could not load collector module")
collector = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = collector
spec_loader = spec.loader
spec_loader.exec_module(collector)


class _SnapshotHandler(BaseHTTPRequestHandler):
    payloads: dict[str, tuple[int, Any]] = {
        "/status": (200, {"can": True, "ready": False, "runtime": {"canFrames": 12}, "telemetry": {"speed": {"seen": True}}}),
        "/diagnostics_detail": (200, {"event": {"enabled": True, "rawCount": 4}}),
        "/gvret/status": (200, {"enabled": False, "connected": False}),
        "/support": (200, "SSID: ZZUBI_Tesla\nIP address: 192.168.4.1\nMAC address: AA:BB:CC:DD:EE:FF\n[CAN]\n"),
    }
    fail_path: str | None = None

    def do_GET(self):  # noqa: N802 - BaseHTTPRequestHandler API
        if self.path == self.fail_path:
            self.send_response(503)
            self.end_headers()
            return
        status, payload = self.payloads.get(self.path, (404, {"error": "not found"}))
        if isinstance(payload, dict):
            body = json.dumps(payload).encode("utf-8")
            content_type = "application/json"
        else:
            body = payload.encode("utf-8")
            content_type = "text/plain"
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        return


class SnapshotCollectorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), _SnapshotHandler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.base_url = f"http://127.0.0.1:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=2)

    def test_snapshot_is_versioned_bounded_and_read_only(self):
        snapshot = collector.collect_snapshot(self.base_url)
        self.assertEqual("t2can-maintenance-snapshot-v1", snapshot["schema"])
        self.assertTrue(snapshot["readOnly"])
        self.assertEqual({"status", "diagnostics", "gvret"}, set(snapshot["endpoints"]))
        self.assertEqual(12, snapshot["summary"]["runtime"]["canFrames"])
        paths = {item[1] for item in collector.READ_ONLY_ENDPOINTS}
        self.assertEqual({"/status", "/diagnostics_detail", "/gvret/status"}, paths)
        self.assertNotIn("/update", paths)
        self.assertNotIn("/config", paths)

    def test_support_is_redacted_and_output_is_private_and_atomic(self):
        snapshot = collector.collect_snapshot(self.base_url, include_support=True)
        support = snapshot["endpoints"]["support"]["text"]
        self.assertIn("SSID: <redacted>", support)
        self.assertIn("IP address: <redacted>", support)
        self.assertIn("MAC address: <redacted>", support)
        self.assertIn("[CAN]", support)

        with TemporaryDirectory() as directory:
            output = Path(directory) / "nested" / "snapshot.json"
            collector.write_atomic(snapshot, output)
            self.assertTrue(output.exists())
            self.assertEqual(0o600, stat.S_IMODE(output.stat().st_mode))
            loaded = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(snapshot["schema"], loaded["schema"])
            self.assertFalse(output.with_name("snapshot.json.tmp").exists())

    def test_public_host_is_rejected_by_default(self):
        with self.assertRaises(collector.SnapshotError):
            collector.validate_base_url("http://example.com")
        self.assertEqual(
            "https://example.com/",
            collector.validate_base_url("https://example.com", allow_public_host=True),
        )

    def test_required_endpoint_failure_stops_collection(self):
        _SnapshotHandler.fail_path = "/diagnostics_detail"
        try:
            with self.assertRaises(collector.SnapshotError):
                collector.collect_snapshot(self.base_url)
        finally:
            _SnapshotHandler.fail_path = None


if __name__ == "__main__":
    unittest.main()
