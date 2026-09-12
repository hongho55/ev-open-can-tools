#!/usr/bin/env python3
"""Receive verified vehicle incidents on a localhost-only private endpoint.

Tailscale Serve should be the only ingress to this process. The server itself
binds to loopback, requires a separately provisioned upload token, and accepts
only bounded binary incident bodies. It never executes vehicle commands.
"""
from __future__ import annotations

import argparse
import hashlib
import hmac
import json
import os
import re
import sys
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any


ARCHIVE_SCHEMA = "t2can-incident-archive-v1"
DEFAULT_MAX_BYTES = 8 * 1024 * 1024
EVENT_ID_RE = re.compile(r"^[1-9][0-9]*-[0-9]{1,3}$")
SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")


class ReceiverError(RuntimeError):
    """Raised when an upload cannot be safely committed."""


def _safe_component(value: str) -> str:
    result = re.sub(r"[^A-Za-z0-9_.-]+", "_", value).strip("._")
    return result[:80] or "unknown"


def _hash_file(path: Path) -> tuple[int, str]:
    size = 0
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while True:
            chunk = source.read(64 * 1024)
            if not chunk:
                break
            size += len(chunk)
            digest.update(chunk)
    return size, digest.hexdigest()


def _fsync_directory(directory: Path) -> None:
    try:
        fd = os.open(directory, os.O_RDONLY)
    except OSError:
        return
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def _write_json_atomic(payload: dict[str, Any], path: Path) -> None:
    temporary = path.with_name(path.name + ".tmp")
    encoded = (json.dumps(payload, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    try:
        with temporary.open("wb") as target:
            target.write(encoded)
            target.flush()
            os.fsync(target.fileno())
        os.chmod(temporary, 0o600)
        os.replace(temporary, path)
        os.chmod(path, 0o600)
        _fsync_directory(path.parent)
    finally:
        if temporary.exists():
            temporary.unlink()


def _validate_headers(headers, max_bytes: int) -> tuple[str, str, int, str]:
    event_id = headers.get("X-T2CAN-Incident-Id", "")
    board_id = headers.get("X-T2CAN-Board-Id", "")
    sha256 = headers.get("X-T2CAN-SHA256", "")
    content_length = headers.get("Content-Length")
    if not EVENT_ID_RE.fullmatch(event_id) or len(event_id) > 32:
        raise ReceiverError("invalid incident id")
    if not board_id or len(board_id) > 128 or "\r" in board_id or "\n" in board_id:
        raise ReceiverError("invalid board id")
    if not isinstance(sha256, str) or not SHA256_RE.fullmatch(sha256):
        raise ReceiverError("invalid SHA-256")
    try:
        size = int(content_length or "-1")
        declared_size = int(headers.get("X-T2CAN-Size", "-1"))
    except ValueError as exc:
        raise ReceiverError("invalid content size") from exc
    if size < 0 or declared_size != size or size > max_bytes:
        raise ReceiverError("content size is invalid or exceeds the limit")
    return event_id, board_id, size, sha256.lower()


def commit_upload(
    body_reader,
    headers,
    archive_dir: Path,
    token: str,
    max_bytes: int = DEFAULT_MAX_BYTES,
) -> tuple[int, dict[str, Any]]:
    """Validate, durably commit, and classify one upload.

    `body_reader` must provide `read(size)` and is consumed exactly once. The
    token is passed separately so callers cannot accidentally accept a missing
    Authorization header as a valid upload.
    """
    supplied = headers.get("Authorization", "")
    expected = "Bearer " + token
    if not hmac.compare_digest(supplied, expected):
        return 401, {"ok": False, "error": "unauthorized"}
    try:
        event_id, board_id, size, expected_sha = _validate_headers(headers, max_bytes)
    except ReceiverError as exc:
        return 400, {"ok": False, "error": str(exc)}

    final = archive_dir.expanduser() / f"board-{_safe_component(board_id)}" / f"incident-{event_id}.jsonl"
    final.parent.mkdir(parents=True, exist_ok=True)
    if final.exists():
        existing_size, existing_sha = _hash_file(final)
        if existing_size == size and existing_sha == expected_sha:
            return 200, {"ok": True, "status": "already_stored", "id": event_id, "sha256": expected_sha}
        return 409, {"ok": False, "error": "archive_conflict", "id": event_id}

    temporary = final.with_name(final.name + ".uploading")
    if temporary.exists():
        temporary.unlink()
    digest = hashlib.sha256()
    total = 0
    try:
        with temporary.open("wb") as target:
            while total < size:
                chunk = body_reader.read(min(64 * 1024, size - total))
                if not chunk:
                    break
                total += len(chunk)
                target.write(chunk)
                digest.update(chunk)
            target.flush()
            os.fsync(target.fileno())
        actual_sha = digest.hexdigest()
        if total != size or actual_sha != expected_sha:
            raise ReceiverError("upload content does not match declared size or SHA-256")
        os.chmod(temporary, 0o600)
        os.replace(temporary, final)
        os.chmod(final, 0o600)
        _fsync_directory(final.parent)
        readback_size, readback_sha = _hash_file(final)
        if readback_size != size or readback_sha != expected_sha:
            raise ReceiverError("archive read-back verification failed")
        metadata = {
            "schema": ARCHIVE_SCHEMA,
            "receivedAt": datetime.now(timezone.utc).isoformat(),
            "source": {"boardId": board_id},
            "incident": {
                "id": event_id,
                "size": size,
                "sha256": expected_sha,
                "path": final.name,
            },
        }
        _write_json_atomic(metadata, final.with_suffix(final.suffix + ".meta.json"))
        return 201, {"ok": True, "status": "stored", "id": event_id, "sha256": expected_sha}
    except Exception:
        if temporary.exists():
            temporary.unlink()
        raise


class _ReceiverHandler(BaseHTTPRequestHandler):
    archive_dir: Path
    token: str
    max_bytes: int

    def _send(self, status: int, payload: dict[str, Any]) -> None:
        body = (json.dumps(payload, ensure_ascii=False) + "\n").encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):  # noqa: N802 - BaseHTTPRequestHandler API
        if self.path != "/v1/incidents":
            self._send(404, {"ok": False, "error": "not_found"})
            return
        try:
            status, payload = commit_upload(
                self.rfile,
                self.headers,
                self.archive_dir,
                self.token,
                self.max_bytes,
            )
        except (ReceiverError, OSError) as exc:
            status, payload = 400, {"ok": False, "error": str(exc)}
        self._send(status, payload)

    def log_message(self, format, *args):
        return


def create_server(bind: str, port: int, archive_dir: Path, token: str, max_bytes: int):
    if bind != "127.0.0.1":
        raise ReceiverError("receiver must bind to localhost")
    if not token or "\r" in token or "\n" in token:
        raise ReceiverError("upload token is required")
    handler = type(
        "ConfiguredReceiverHandler",
        (_ReceiverHandler,),
        {"archive_dir": archive_dir.expanduser(), "token": token, "max_bytes": max_bytes},
    )
    return ThreadingHTTPServer((bind, port), handler)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Receive verified vehicle incidents on localhost.")
    parser.add_argument("--bind", default="127.0.0.1", choices=("127.0.0.1",))
    parser.add_argument("--port", type=int, default=8787)
    parser.add_argument("--archive-dir", type=Path, default=Path.home() / "t2can-incidents")
    parser.add_argument("--token-env", default="T2CAN_UPLOAD_TOKEN")
    parser.add_argument("--max-bytes", type=int, default=DEFAULT_MAX_BYTES)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    token = os.environ.get(args.token_env, "")
    try:
        if args.port < 1 or args.port > 65535:
            raise ReceiverError("port must be between 1 and 65535")
        if args.max_bytes <= 0:
            raise ReceiverError("max-bytes must be positive")
        server = create_server(args.bind, args.port, args.archive_dir, token, args.max_bytes)
    except (ReceiverError, OSError) as exc:
        print(f"incident receiver failed: {exc}", file=sys.stderr)
        return 1
    print(f"listening on http://{args.bind}:{args.port}/v1/incidents")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
