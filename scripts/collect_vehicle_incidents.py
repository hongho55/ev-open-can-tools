#!/usr/bin/env python3
"""Synchronize completed, read-only ESP32 incidents into a private archive.

The command is deliberately one-shot. A future S26 worker can implement the
same state machine: list -> download to a temporary file -> verify -> durable
archive commit -> ACK. No event is ACKed before its verified archive copy exists.
"""
from __future__ import annotations

import argparse
import base64
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
from urllib.parse import urlencode, urljoin, urlparse
from urllib.request import Request, urlopen


EVENT_LIST_SCHEMA = "t2can-incident-list-v1"
ARCHIVE_SCHEMA = "t2can-incident-archive-v1"
MAX_LIST_RESPONSE_BYTES = 128 * 1024
DEFAULT_MAX_EVENT_BYTES = 8 * 1024 * 1024
DEFAULT_OUTPUT_DIR = Path.home() / "t2can-incidents"
EVENT_ID_RE = re.compile(r"^[1-9][0-9]*-[0-9]{1,3}$")
SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")


class IncidentSyncError(RuntimeError):
    """Raised when a transfer cannot be verified or safely committed."""


@dataclass(frozen=True)
class RecorderAuth:
    username: str
    password: str

    def header(self) -> str:
        value = f"{self.username}:{self.password}".encode("utf-8")
        return "Basic " + base64.b64encode(value).decode("ascii")


@dataclass(frozen=True)
class Incident:
    event_id: str
    sequence: int
    slot: int
    size: int
    sha256: str
    acknowledged: bool


@dataclass(frozen=True)
class SyncResult:
    listed: int
    skipped_acknowledged: int
    archived: int
    duplicates: int
    acknowledged: int
    errors: tuple[str, ...]

    def to_dict(self) -> dict[str, Any]:
        return {
            "listed": self.listed,
            "skippedAcknowledged": self.skipped_acknowledged,
            "archived": self.archived,
            "duplicates": self.duplicates,
            "acknowledged": self.acknowledged,
            "errors": list(self.errors),
            "ok": not self.errors,
        }


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
        raise IncidentSyncError("base URL must use http:// or https://")
    if parsed.username or parsed.password or parsed.query or parsed.fragment:
        raise IncidentSyncError("base URL must not contain credentials, query, or fragment")
    if not parsed.hostname:
        raise IncidentSyncError("base URL must contain a host")
    try:
        parsed.port
    except ValueError as exc:
        raise IncidentSyncError("base URL has an invalid port") from exc
    if not allow_public_host and not _local_host(parsed.hostname):
        raise IncidentSyncError("refusing a non-local host; use the ESP32 private/local address")
    return f"{parsed.scheme}://{parsed.netloc}/"


def _request(
    url: str,
    auth: RecorderAuth,
    timeout: float,
    method: str = "GET",
    body: bytes | None = None,
    content_type: str | None = None,
):
    headers = {
        "Accept": "application/json, application/x-ndjson, text/plain",
        "Cache-Control": "no-store",
        "User-Agent": "t2can-incident-sync/1",
        "Authorization": auth.header(),
    }
    if content_type:
        headers["Content-Type"] = content_type
    request = Request(url, data=body, headers=headers, method=method)
    try:
        return urlopen(request, timeout=timeout)
    except HTTPError as exc:
        raise IncidentSyncError(f"HTTP {exc.code} for {urlparse(url).path}") from exc
    except URLError as exc:
        reason = str(exc.reason) if exc.reason else "connection failed"
        raise IncidentSyncError(f"{urlparse(url).path}: {reason}") from exc
    except TimeoutError as exc:
        raise IncidentSyncError(f"{urlparse(url).path}: request timed out") from exc
    except OSError as exc:
        raise IncidentSyncError(f"{urlparse(url).path}: request failed") from exc


def _read_bounded(response, limit: int) -> bytes:
    declared = response.headers.get("Content-Length")
    if declared:
        try:
            if int(declared) > limit:
                raise IncidentSyncError(f"response exceeds {limit} bytes")
        except ValueError as exc:
            raise IncidentSyncError("response has an invalid Content-Length") from exc
    body = response.read(limit + 1)
    if len(body) > limit:
        raise IncidentSyncError(f"response exceeds {limit} bytes")
    return body


def _normalize_sha256(value: Any, field: str) -> str:
    if not isinstance(value, str) or not SHA256_RE.fullmatch(value):
        raise IncidentSyncError(f"{field} must be a 64-character SHA-256 value")
    return value.lower()


def _parse_incident(item: Any) -> Incident:
    if not isinstance(item, dict):
        raise IncidentSyncError("incident list contains a non-object item")
    event_id = item.get("id")
    if not isinstance(event_id, str) or len(event_id) > 32 or not EVENT_ID_RE.fullmatch(event_id):
        raise IncidentSyncError("incident list contains an invalid id")
    try:
        sequence_text, slot_text = event_id.split("-", 1)
        sequence = int(sequence_text)
        slot = int(slot_text)
    except ValueError as exc:
        raise IncidentSyncError("incident list contains an invalid id") from exc
    if sequence <= 0 or slot > 255:
        raise IncidentSyncError("incident list contains an invalid id")
    size = item.get("size")
    if not isinstance(size, int) or isinstance(size, bool) or size < 0:
        raise IncidentSyncError(f"incident {event_id} has an invalid size")
    sha256 = _normalize_sha256(item.get("sha256"), f"incident {event_id} sha256")
    acknowledged = item.get("acknowledged")
    if not isinstance(acknowledged, bool):
        raise IncidentSyncError(f"incident {event_id} has an invalid acknowledged flag")
    return Incident(event_id, sequence, slot, size, sha256, acknowledged)


def fetch_incident_page(
    base_url: str, auth: RecorderAuth, timeout: float, after_sequence: int = 0
) -> tuple[str, list[Incident], bool]:
    query = "" if after_sequence == 0 else "?" + urlencode({"after": after_sequence})
    url = urljoin(base_url, "event_list" + query)
    with _request(url, auth, timeout) as response:
        status = int(getattr(response, "status", 200))
        if status < 200 or status >= 300:
            raise IncidentSyncError(f"HTTP {status} for /event_list")
        try:
            payload = json.loads(_read_bounded(response, MAX_LIST_RESPONSE_BYTES).decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise IncidentSyncError(f"/event_list returned invalid JSON: {exc}") from exc
    if not isinstance(payload, dict) or payload.get("schema") != EVENT_LIST_SCHEMA:
        raise IncidentSyncError("/event_list returned an unsupported schema")
    board_id = payload.get("boardId")
    if not isinstance(board_id, str) or not board_id or len(board_id) > 128:
        raise IncidentSyncError("/event_list returned an invalid boardId")
    raw_incidents = payload.get("incidents")
    if not isinstance(raw_incidents, list):
        raise IncidentSyncError("/event_list returned an invalid incidents array")
    incidents = [_parse_incident(item) for item in raw_incidents]
    truncated = payload.get("truncated")
    if not isinstance(truncated, bool):
        raise IncidentSyncError("/event_list returned an invalid truncated flag")
    return board_id, incidents, truncated


def list_incidents(
    base_url: str, auth: RecorderAuth, timeout: float, max_pages: int = 64
) -> tuple[str, list[Incident]]:
    if max_pages <= 0:
        raise IncidentSyncError("max_pages must be positive")
    after = 0
    board_id: str | None = None
    found: dict[str, Incident] = {}
    for _ in range(max_pages):
        page_board_id, page, truncated = fetch_incident_page(base_url, auth, timeout, after)
        if board_id is None:
            board_id = page_board_id
        elif page_board_id != board_id:
            raise IncidentSyncError("boardId changed during incident listing")
        for incident in page:
            previous = found.get(incident.event_id)
            if previous is not None and previous != incident:
                raise IncidentSyncError(f"incident {incident.event_id} changed during listing")
            found[incident.event_id] = incident
        if not truncated:
            return board_id or page_board_id, sorted(
                found.values(), key=lambda item: (item.sequence, item.slot)
            )
        if not page:
            raise IncidentSyncError("truncated incident list made no progress")
        next_after = max(item.sequence for item in page)
        if next_after <= after:
            raise IncidentSyncError("truncated incident list cursor made no progress")
        after = next_after
    raise IncidentSyncError("incident list exceeded the page safety limit")


def _safe_component(value: str) -> str:
    result = re.sub(r"[^A-Za-z0-9_.-]+", "_", value).strip("._")
    return result[:80] or "unknown"


def _archive_path(archive_dir: Path, board_id: str, event_id: str) -> Path:
    return archive_dir.expanduser() / f"board-{_safe_component(board_id)}" / f"incident-{event_id}.jsonl"


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


def _download_event(
    base_url: str,
    auth: RecorderAuth,
    incident: Incident,
    temporary: Path,
    timeout: float,
    max_event_bytes: int,
) -> None:
    query = urlencode({"id": incident.event_id})
    url = urljoin(base_url, "event_download?" + query)
    temporary.parent.mkdir(parents=True, exist_ok=True)
    if temporary.exists():
        temporary.unlink()
    digest = hashlib.sha256()
    total = 0
    try:
        with _request(url, auth, timeout) as response:
            status = int(getattr(response, "status", 200))
            if status < 200 or status >= 300:
                raise IncidentSyncError(f"HTTP {status} for /event_download")
            header_id = response.headers.get("X-T2CAN-Incident-Id")
            if header_id != incident.event_id:
                raise IncidentSyncError(f"download id mismatch for {incident.event_id}")
            header_size = response.headers.get("X-T2CAN-Size")
            try:
                if int(header_size or "-1") != incident.size:
                    raise IncidentSyncError(f"download size header mismatch for {incident.event_id}")
            except ValueError as exc:
                raise IncidentSyncError(f"download size header invalid for {incident.event_id}") from exc
            header_sha = _normalize_sha256(
                response.headers.get("X-T2CAN-SHA256"),
                f"download sha256 for {incident.event_id}",
            )
            if header_sha != incident.sha256:
                raise IncidentSyncError(f"download hash header mismatch for {incident.event_id}")
            etag = response.headers.get("ETag")
            if etag:
                if etag.strip('"').lower() != incident.sha256:
                    raise IncidentSyncError(f"download ETag mismatch for {incident.event_id}")
            content_length = response.headers.get("Content-Length")
            if content_length:
                try:
                    declared_length = int(content_length)
                    if declared_length > max_event_bytes:
                        raise IncidentSyncError(
                            f"incident {incident.event_id} exceeds {max_event_bytes} bytes"
                        )
                    if declared_length != incident.size:
                        raise IncidentSyncError(f"download Content-Length mismatch for {incident.event_id}")
                except ValueError as exc:
                    raise IncidentSyncError(f"download Content-Length invalid for {incident.event_id}") from exc
            with temporary.open("wb") as target:
                while True:
                    chunk = response.read(min(64 * 1024, max_event_bytes + 1))
                    if not chunk:
                        break
                    total += len(chunk)
                    if total > max_event_bytes:
                        raise IncidentSyncError(f"incident {incident.event_id} exceeds {max_event_bytes} bytes")
                    target.write(chunk)
                    digest.update(chunk)
                target.flush()
                os.fsync(target.fileno())
        actual_sha = digest.hexdigest()
        if total != incident.size or actual_sha != incident.sha256:
            raise IncidentSyncError(f"download content mismatch for {incident.event_id}")
        os.chmod(temporary, 0o600)
    except Exception:
        if temporary.exists():
            temporary.unlink()
        raise


def _commit_event(
    temporary: Path, final: Path, incident: Incident, board_id: str, source_url: str
) -> str:
    final.parent.mkdir(parents=True, exist_ok=True)
    if final.exists():
        existing_size, existing_sha = _hash_file(final)
        if existing_size == incident.size and existing_sha == incident.sha256:
            return "duplicate"
        raise IncidentSyncError(f"archive conflict for {incident.event_id}; refusing overwrite")
    os.replace(temporary, final)
    os.chmod(final, 0o600)
    _fsync_directory(final.parent)
    readback_size, readback_sha = _hash_file(final)
    if readback_size != incident.size or readback_sha != incident.sha256:
        raise IncidentSyncError(f"archive read-back mismatch for {incident.event_id}")
    metadata = {
        "schema": ARCHIVE_SCHEMA,
        "receivedAt": datetime.now(timezone.utc).isoformat(),
        "source": {"baseUrl": source_url, "boardId": board_id},
        "incident": {
            "id": incident.event_id,
            "size": incident.size,
            "sha256": incident.sha256,
            "path": final.name,
        },
    }
    _write_json_atomic(metadata, final.with_suffix(final.suffix + ".meta.json"))
    return "archived"


def _ack_event(base_url: str, auth: RecorderAuth, incident: Incident, timeout: float) -> None:
    body = urlencode(
        {"id": incident.event_id, "size": str(incident.size), "sha256": incident.sha256}
    ).encode("ascii")
    url = urljoin(base_url, "event_ack")
    with _request(
        url,
        auth,
        timeout,
        method="POST",
        body=body,
        content_type="application/x-www-form-urlencoded",
    ) as response:
        status = int(getattr(response, "status", 200))
        if status < 200 or status >= 300:
            raise IncidentSyncError(f"HTTP {status} for /event_ack")
        try:
            payload = json.loads(_read_bounded(response, 16 * 1024).decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise IncidentSyncError(f"/event_ack returned invalid JSON: {exc}") from exc
    if not isinstance(payload, dict) or payload.get("ok") is not True:
        raise IncidentSyncError(f"/event_ack did not confirm {incident.event_id}")


def sync_incidents(
    base_url: str,
    auth: RecorderAuth,
    archive_dir: Path = DEFAULT_OUTPUT_DIR,
    timeout: float = 5.0,
    max_event_bytes: int = DEFAULT_MAX_EVENT_BYTES,
    max_pages: int = 64,
) -> SyncResult:
    if timeout <= 0:
        raise IncidentSyncError("timeout must be positive")
    if max_event_bytes <= 0:
        raise IncidentSyncError("max_event_bytes must be positive")
    normalized = validate_base_url(base_url)
    board_id, incidents = list_incidents(normalized, auth, timeout, max_pages=max_pages)
    archived = duplicates = acknowledged = skipped = 0
    errors: list[str] = []
    for incident in incidents:
        if incident.acknowledged:
            skipped += 1
            continue
        if incident.size > max_event_bytes:
            errors.append(
                f"{incident.event_id}: incident exceeds {max_event_bytes} bytes"
            )
            continue
        final = _archive_path(archive_dir, board_id, incident.event_id)
        temporary = final.with_name(final.name + ".part")
        try:
            if final.exists():
                state = _commit_event(temporary, final, incident, board_id, normalized)
            else:
                _download_event(normalized, auth, incident, temporary, timeout, max_event_bytes)
                state = _commit_event(temporary, final, incident, board_id, normalized)
            if state == "archived":
                archived += 1
            else:
                duplicates += 1
            _ack_event(normalized, auth, incident, timeout)
            acknowledged += 1
        except (IncidentSyncError, OSError) as exc:
            if temporary.exists():
                temporary.unlink()
            errors.append(f"{incident.event_id}: {exc}")
    return SyncResult(
        listed=len(incidents),
        skipped_acknowledged=skipped,
        archived=archived,
        duplicates=duplicates,
        acknowledged=acknowledged,
        errors=tuple(errors),
    )


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Synchronize verified read-only ESP32 incidents into a private archive."
    )
    parser.add_argument("base_url", help="Private ESP32 dashboard URL, e.g. http://192.168.4.1")
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--username", default=os.environ.get("T2CAN_RECORDER_USER", ""))
    parser.add_argument("--password-env", default="T2CAN_RECORDER_PASSWORD")
    parser.add_argument("--password-stdin", action="store_true")
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--max-event-bytes", type=int, default=DEFAULT_MAX_EVENT_BYTES)
    parser.add_argument("--max-pages", type=int, default=64)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if not args.username:
        print("incident sync: username is required", file=sys.stderr)
        return 2
    if args.password_stdin:
        password = sys.stdin.readline().rstrip("\r\n")
    else:
        password = os.environ.get(args.password_env, "")
    if not password:
        print("incident sync: password is required via --password-stdin or the password environment", file=sys.stderr)
        return 2
    try:
        result = sync_incidents(
            args.base_url,
            RecorderAuth(args.username, password),
            archive_dir=args.output_dir,
            timeout=args.timeout,
            max_event_bytes=args.max_event_bytes,
            max_pages=args.max_pages,
        )
    except (IncidentSyncError, OSError) as exc:
        print(f"incident sync failed: {exc}", file=sys.stderr)
        return 1
    print(json.dumps(result.to_dict(), ensure_ascii=False, sort_keys=True))
    return 0 if result.errors == () else 1


if __name__ == "__main__":
    raise SystemExit(main())
