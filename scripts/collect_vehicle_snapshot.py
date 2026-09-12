#!/usr/bin/env python3
"""Collect a bounded, read-only ESP32 maintenance snapshot.

The collector pulls diagnostics from a locally reachable ESP32 dashboard and
writes one private, atomic JSON artifact for later comparison. It never calls
configuration, event-control, OTA, plugin, or CAN-transmit endpoints.
"""
from __future__ import annotations

import argparse
import hashlib
import ipaddress
import json
import os
import re
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.parse import urljoin, urlparse
from urllib.request import Request, urlopen


SNAPSHOT_SCHEMA = "t2can-maintenance-snapshot-v1"
COLLECTOR_VERSION = "1"
MAX_RESPONSE_BYTES = 128 * 1024
DEFAULT_OUTPUT_DIR = Path.home() / "t2can-snapshots"
READ_ONLY_ENDPOINTS = (
    ("status", "/status", True),
    ("diagnostics", "/diagnostics_detail", True),
    ("gvret", "/gvret/status", False),
)

# These lines may contain local network identity. Credentials are not expected
# in /support, but redacting identity makes an artifact safer to share.
_REDACT_LINE = re.compile(
    r"^(\s*(?:SSID|IP address|MAC address)\s*:\s*).*$", re.IGNORECASE
)


class SnapshotError(RuntimeError):
    """Raised when the target or a required read-only endpoint is invalid."""


@dataclass(frozen=True)
class EndpointResult:
    path: str
    ok: bool
    status: int | None
    bytes: int
    sha256: str | None
    json_body: dict[str, Any] | None = None
    text_body: str | None = None
    error: str | None = None

    def to_dict(self) -> dict[str, Any]:
        result: dict[str, Any] = {
            "path": self.path,
            "ok": self.ok,
            "status": self.status,
            "bytes": self.bytes,
            "sha256": self.sha256,
        }
        if self.json_body is not None:
            result["json"] = self.json_body
        if self.text_body is not None:
            result["text"] = self.text_body
        if self.error is not None:
            result["error"] = self.error
        return result


def _local_host(host: str) -> bool:
    normalized = host.rstrip(".").lower()
    if normalized in {"localhost", "127.0.0.1", "::1"} or normalized.endswith(".local"):
        return True
    try:
        address = ipaddress.ip_address(normalized)
    except ValueError:
        return False
    return address.is_private or address.is_loopback or address.is_link_local


def validate_base_url(raw: str, allow_public_host: bool = False) -> str:
    parsed = urlparse(raw)
    if parsed.scheme not in {"http", "https"}:
        raise SnapshotError("base URL must use http:// or https://")
    if parsed.username or parsed.password or parsed.query or parsed.fragment:
        raise SnapshotError("base URL must not contain credentials, query, or fragment")
    if not parsed.hostname:
        raise SnapshotError("base URL must contain a host")
    try:
        parsed.port
    except ValueError as exc:
        raise SnapshotError("base URL has an invalid port") from exc
    if not allow_public_host and not _local_host(parsed.hostname):
        raise SnapshotError(
            "refusing a non-local host; use the ESP32 private/local address"
        )
    # Strip a path so endpoint joining cannot accidentally target a different
    # subtree. A trailing slash is retained for urljoin.
    return f"{parsed.scheme}://{parsed.netloc}/"


def _read_response(url: str, timeout: float) -> tuple[int, bytes]:
    request = Request(
        url,
        headers={
            "Accept": "application/json, text/plain",
            "Cache-Control": "no-store",
            "User-Agent": "t2can-maintenance-collector/1",
        },
        method="GET",
    )
    try:
        with urlopen(request, timeout=timeout) as response:
            status = int(response.status)
            content_length = response.headers.get("Content-Length")
            if content_length:
                try:
                    declared_length = int(content_length)
                except ValueError as exc:
                    raise SnapshotError("response has an invalid Content-Length") from exc
                if declared_length > MAX_RESPONSE_BYTES:
                    raise SnapshotError(f"response exceeds {MAX_RESPONSE_BYTES} bytes")
            body = response.read(MAX_RESPONSE_BYTES + 1)
            if len(body) > MAX_RESPONSE_BYTES:
                raise SnapshotError(f"response exceeds {MAX_RESPONSE_BYTES} bytes")
            return status, body
    except HTTPError as exc:
        raise SnapshotError(f"HTTP {exc.code}") from exc
    except URLError as exc:
        reason = str(exc.reason) if exc.reason else "connection failed"
        raise SnapshotError(reason) from exc
    except TimeoutError as exc:
        raise SnapshotError("request timed out") from exc
    except ValueError as exc:
        raise SnapshotError(f"invalid target URL: {exc}") from exc
    except OSError as exc:
        raise SnapshotError("request failed") from exc


def fetch_endpoint(base_url: str, path: str, timeout: float) -> EndpointResult:
    url = urljoin(base_url, path.lstrip("/"))
    try:
        status, body = _read_response(url, timeout)
        digest = hashlib.sha256(body).hexdigest()
        if status < 200 or status >= 300:
            return EndpointResult(path, False, status, len(body), digest, error=f"HTTP {status}")
        if path == "/support":
            text = body.decode("utf-8", errors="replace")
            return EndpointResult(path, True, status, len(body), digest, text_body=redact_support(text))
        try:
            parsed = json.loads(body.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            return EndpointResult(path, False, status, len(body), digest, error=f"invalid JSON: {exc}")
        if not isinstance(parsed, dict):
            return EndpointResult(path, False, status, len(body), digest, error="JSON body is not an object")
        return EndpointResult(path, True, status, len(body), digest, json_body=parsed)
    except SnapshotError as exc:
        return EndpointResult(path, False, None, 0, None, error=str(exc))


def redact_support(text: str) -> str:
    return "\n".join(
        _REDACT_LINE.sub(r"\1<redacted>", line) for line in text.splitlines()
    ) + ("\n" if text.endswith("\n") else "")


def build_summary(status: dict[str, Any] | None, diagnostics: dict[str, Any] | None) -> dict[str, Any]:
    if not status:
        return {"available": False}
    keys = ("can", "ready", "hardwareReady", "trafficSeen", "vehicleOnline", "runtime", "telemetry")
    summary = {key: status[key] for key in keys if key in status}
    if diagnostics and "event" in diagnostics:
        summary["eventRecorder"] = diagnostics["event"]
    summary["available"] = True
    return summary


def collect_snapshot(
    base_url: str,
    timeout: float = 5.0,
    include_support: bool = False,
    allow_public_host: bool = False,
) -> dict[str, Any]:
    normalized = validate_base_url(base_url, allow_public_host=allow_public_host)
    results: dict[str, EndpointResult] = {}
    for name, path, required in READ_ONLY_ENDPOINTS:
        result = fetch_endpoint(normalized, path, timeout)
        results[name] = result
        if required and not result.ok:
            raise SnapshotError(f"required endpoint {path} failed: {result.error or 'unknown error'}")

    if include_support:
        results["support"] = fetch_endpoint(normalized, "/support", timeout)

    status = results["status"].json_body
    diagnostics = results["diagnostics"].json_body
    return {
        "schema": SNAPSHOT_SCHEMA,
        "collectorVersion": COLLECTOR_VERSION,
        "collectedAt": datetime.now(timezone.utc).isoformat(),
        "target": {"baseUrl": normalized},
        "readOnly": True,
        "endpoints": {name: result.to_dict() for name, result in results.items()},
        "summary": build_summary(status, diagnostics),
    }


def write_atomic(snapshot: dict[str, Any], output: Path) -> None:
    output = output.expanduser()
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + ".tmp")
    encoded = json.dumps(snapshot, ensure_ascii=False, indent=2) + "\n"
    temporary.write_text(encoded, encoding="utf-8")
    os.chmod(temporary, 0o600)
    os.replace(temporary, output)
    os.chmod(output, 0o600)


def default_output_path(output_dir: Path) -> Path:
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    return output_dir.expanduser() / f"t2can-snapshot-{stamp}.json"


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Pull a bounded read-only ESP32 maintenance snapshot to this Mac."
    )
    parser.add_argument("base_url", help="Private ESP32 dashboard URL, e.g. http://192.168.4.1")
    parser.add_argument("--output", type=Path, help="Exact JSON output path")
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument(
        "--include-support",
        action="store_true",
        help="Also pull the human-readable support report with network identity redacted",
    )
    parser.add_argument(
        "--allow-public-host",
        action="store_true",
        help="Override the local-host safety gate; not recommended for vehicle devices",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if args.timeout <= 0:
        print("snapshot collector: timeout must be positive", file=sys.stderr)
        return 2
    output = args.output.expanduser() if args.output else default_output_path(args.output_dir)
    try:
        snapshot = collect_snapshot(
            args.base_url,
            timeout=args.timeout,
            include_support=args.include_support,
            allow_public_host=args.allow_public_host,
        )
        write_atomic(snapshot, output)
    except (SnapshotError, OSError) as exc:
        print(f"snapshot collector failed: {exc}", file=sys.stderr)
        return 1
    print(f"wrote {output} ({len(snapshot['endpoints'])} read-only endpoints)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
