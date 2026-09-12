#!/usr/bin/env python3
"""Durable reference queue for the S26 store-and-forward worker.

This module is intentionally Android-independent so its crash/replay contract
can be tested before the same state machine is ported to Room/WorkManager.
It stores metadata and a path to an already committed local file; it never
stores raw vehicle payloads in the database.
"""

from __future__ import annotations

import hashlib
import re
import sqlite3
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


EVENT_ID_RE = re.compile(r"^[1-9][0-9]{0,18}-[0-9]{1,3}$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
STATES = ("QUEUED_LOCAL", "UPLOADING", "ACK_PENDING", "ACKED")
MAX_LOCAL_FILE_BYTES = 64 * 1024 * 1024
MAX_ERROR_CODE_LENGTH = 64


class QueueError(Exception):
    """Base class for queue contract failures."""


class QueueConflict(QueueError):
    """The same event identity was presented with different content."""


class InvalidQueueEntry(QueueError):
    """An input or requested state transition is invalid."""


@dataclass(frozen=True)
class QueueItem:
    board_id: str
    event_id: str
    size: int
    sha256: str
    local_path: str
    state: str
    attempts: int
    lease_owner: Optional[str]
    lease_until: Optional[float]
    next_attempt_at: float
    last_error_code: Optional[str]
    remote_status: Optional[str]


class GatewayQueue:
    """SQLite-backed queue with explicit upload leases and ACK gating."""

    def __init__(self, database_path: Path | str, max_file_bytes: int = MAX_LOCAL_FILE_BYTES):
        if max_file_bytes <= 0:
            raise ValueError("max_file_bytes must be positive")
        self.database_path = Path(database_path)
        self.database_path.parent.mkdir(parents=True, exist_ok=True)
        self.max_file_bytes = max_file_bytes
        self._db = sqlite3.connect(self.database_path, timeout=5.0)
        self._db.row_factory = sqlite3.Row
        self._db.execute("PRAGMA foreign_keys = ON")
        self._db.execute("PRAGMA busy_timeout = 5000")
        self._db.execute("PRAGMA journal_mode = WAL")
        self._db.execute("PRAGMA synchronous = FULL")
        self._db.executescript(
            """
            CREATE TABLE IF NOT EXISTS gateway_queue (
                board_id TEXT NOT NULL,
                event_id TEXT NOT NULL,
                size INTEGER NOT NULL CHECK (size >= 0),
                sha256 TEXT NOT NULL CHECK (length(sha256) = 64),
                local_path TEXT NOT NULL,
                state TEXT NOT NULL CHECK (state IN ('QUEUED_LOCAL', 'UPLOADING', 'ACK_PENDING', 'ACKED')),
                attempts INTEGER NOT NULL DEFAULT 0 CHECK (attempts >= 0),
                lease_owner TEXT,
                lease_until REAL,
                next_attempt_at REAL NOT NULL,
                last_error_code TEXT,
                remote_status TEXT,
                created_at REAL NOT NULL,
                updated_at REAL NOT NULL,
                PRIMARY KEY (board_id, event_id)
            );
            CREATE INDEX IF NOT EXISTS gateway_queue_upload_idx
                ON gateway_queue(state, next_attempt_at, created_at);
            """
        )
        self._db.commit()

    def close(self) -> None:
        self._db.close()

    def __enter__(self) -> "GatewayQueue":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def enqueue(
        self,
        board_id: str,
        event_id: str,
        local_path: Path | str,
        expected_size: int,
        expected_sha256: str,
        now: Optional[float] = None,
    ) -> QueueItem:
        """Verify a committed local file and add it idempotently to the queue."""
        board_id = _validate_board_id(board_id)
        event_id = _validate_event_id(event_id)
        expected_sha256 = _validate_sha256(expected_sha256)
        if not isinstance(expected_size, int) or isinstance(expected_size, bool) or expected_size < 0:
            raise InvalidQueueEntry("invalid expected size")
        if expected_size > self.max_file_bytes:
            raise InvalidQueueEntry("event exceeds queue size limit")
        path = Path(local_path)
        if not path.is_file():
            raise InvalidQueueEntry("local incident file is missing")
        actual_size, actual_sha256 = _hash_file(path, self.max_file_bytes)
        if actual_size != expected_size or actual_sha256 != expected_sha256:
            raise InvalidQueueEntry("local incident verification failed")

        now = time.time() if now is None else _validate_time(now)
        existing = self.get(board_id, event_id)
        if existing is not None:
            if existing.size != expected_size or existing.sha256 != expected_sha256:
                raise QueueConflict("same event identity has different content")
            return existing

        self._db.execute(
            """
            INSERT INTO gateway_queue
              (board_id, event_id, size, sha256, local_path, state, attempts,
               lease_owner, lease_until, next_attempt_at, last_error_code,
               remote_status, created_at, updated_at)
            VALUES (?, ?, ?, ?, ?, 'QUEUED_LOCAL', 0, NULL, NULL, ?, NULL, NULL, ?, ?)
            """,
            (board_id, event_id, expected_size, expected_sha256, str(path), now, now, now),
        )
        self._db.commit()
        return self.get(board_id, event_id)  # type: ignore[return-value]

    def claim_upload(self, owner: str, lease_seconds: float, now: Optional[float] = None) -> Optional[QueueItem]:
        """Claim one upload, recovering leases that expired during a crash."""
        owner = _validate_owner(owner)
        if lease_seconds <= 0 or lease_seconds > 24 * 60 * 60:
            raise InvalidQueueEntry("lease_seconds must be in (0, 86400]")
        now = time.time() if now is None else _validate_time(now)
        lease_until = now + lease_seconds
        self._db.execute("BEGIN IMMEDIATE")
        try:
            self._db.execute(
                """
                UPDATE gateway_queue
                   SET state = 'QUEUED_LOCAL', lease_owner = NULL, lease_until = NULL,
                       updated_at = ?
                 WHERE state = 'UPLOADING' AND lease_until IS NOT NULL AND lease_until <= ?
                """,
                (now, now),
            )
            row = self._db.execute(
                """
                SELECT * FROM gateway_queue
                 WHERE state = 'QUEUED_LOCAL' AND next_attempt_at <= ?
                 ORDER BY created_at ASC
                 LIMIT 1
                """,
                (now,),
            ).fetchone()
            if row is None:
                self._db.commit()
                return None
            self._db.execute(
                """
                UPDATE gateway_queue
                   SET state = 'UPLOADING', attempts = attempts + 1,
                       lease_owner = ?, lease_until = ?, updated_at = ?
                 WHERE board_id = ? AND event_id = ? AND state = 'QUEUED_LOCAL'
                """,
                (owner, lease_until, now, row["board_id"], row["event_id"]),
            )
            self._db.commit()
        except Exception:
            self._db.rollback()
            raise
        return self.get(row["board_id"], row["event_id"])

    def mark_upload_failure(
        self,
        board_id: str,
        event_id: str,
        owner: str,
        error_code: str,
        now: Optional[float] = None,
    ) -> QueueItem:
        """Return a leased item to the queue with bounded exponential backoff."""
        owner = _validate_owner(owner)
        error_code = _validate_error_code(error_code)
        now = time.time() if now is None else _validate_time(now)
        item = self._require(board_id, event_id)
        self._require_lease(item, owner)
        delay = min(15 * 60, 2 ** min(item.attempts, 8))
        self._db.execute(
            """
            UPDATE gateway_queue
               SET state = 'QUEUED_LOCAL', lease_owner = NULL, lease_until = NULL,
                   next_attempt_at = ?, last_error_code = ?, updated_at = ?
             WHERE board_id = ? AND event_id = ? AND state = 'UPLOADING' AND lease_owner = ?
            """,
            (now + delay, error_code, now, item.board_id, item.event_id, owner),
        )
        self._db.commit()
        return self._require(board_id, event_id)

    def mark_mac_committed(
        self,
        board_id: str,
        event_id: str,
        owner: str,
        remote_status: str,
        now: Optional[float] = None,
    ) -> QueueItem:
        """Persist the Mac commit result before allowing an ESP32 ACK."""
        owner = _validate_owner(owner)
        if remote_status not in ("stored", "already_stored"):
            raise InvalidQueueEntry("remote status is not a durable commit result")
        now = time.time() if now is None else _validate_time(now)
        item = self._require(board_id, event_id)
        self._require_lease(item, owner)
        self._db.execute(
            """
            UPDATE gateway_queue
               SET state = 'ACK_PENDING', lease_owner = NULL, lease_until = NULL,
                   next_attempt_at = ?, last_error_code = NULL, remote_status = ?, updated_at = ?
             WHERE board_id = ? AND event_id = ? AND state = 'UPLOADING' AND lease_owner = ?
            """,
            (now, remote_status, now, item.board_id, item.event_id, owner),
        )
        self._db.commit()
        return self._require(board_id, event_id)

    def mark_acked(self, board_id: str, event_id: str, now: Optional[float] = None) -> QueueItem:
        """Complete the queue only after the ESP32 ACK itself succeeds."""
        now = time.time() if now is None else _validate_time(now)
        item = self._require(board_id, event_id)
        if item.state != "ACK_PENDING":
            raise InvalidQueueEntry("ESP32 ACK is not allowed in current state")
        self._db.execute(
            """
            UPDATE gateway_queue
               SET state = 'ACKED', updated_at = ?
             WHERE board_id = ? AND event_id = ? AND state = 'ACK_PENDING'
            """,
            (now, item.board_id, item.event_id),
        )
        self._db.commit()
        return self._require(board_id, event_id)

    def ready_for_ack(self) -> list[QueueItem]:
        rows = self._db.execute(
            "SELECT * FROM gateway_queue WHERE state = 'ACK_PENDING' ORDER BY created_at ASC"
        ).fetchall()
        return [_row_to_item(row) for row in rows]

    def get(self, board_id: str, event_id: str) -> Optional[QueueItem]:
        board_id = _validate_board_id(board_id)
        event_id = _validate_event_id(event_id)
        row = self._db.execute(
            "SELECT * FROM gateway_queue WHERE board_id = ? AND event_id = ?",
            (board_id, event_id),
        ).fetchone()
        return None if row is None else _row_to_item(row)

    def _require(self, board_id: str, event_id: str) -> QueueItem:
        item = self.get(board_id, event_id)
        if item is None:
            raise InvalidQueueEntry("queue item does not exist")
        return item

    @staticmethod
    def _require_lease(item: QueueItem, owner: str) -> None:
        if item.state != "UPLOADING" or item.lease_owner != owner:
            raise InvalidQueueEntry("upload lease is not owned by caller")


def _validate_board_id(value: str) -> str:
    if not isinstance(value, str) or not value or len(value) > 64 or any(ord(ch) < 0x20 for ch in value):
        raise InvalidQueueEntry("invalid board id")
    return value


def _validate_event_id(value: str) -> str:
    if not isinstance(value, str) or not EVENT_ID_RE.fullmatch(value):
        raise InvalidQueueEntry("invalid event id")
    return value


def _validate_sha256(value: str) -> str:
    if not isinstance(value, str) or not SHA256_RE.fullmatch(value.lower()):
        raise InvalidQueueEntry("invalid sha256")
    return value.lower()


def _validate_owner(value: str) -> str:
    if not isinstance(value, str) or not value or len(value) > 64 or any(ord(ch) < 0x20 for ch in value):
        raise InvalidQueueEntry("invalid lease owner")
    return value


def _validate_error_code(value: str) -> str:
    if not isinstance(value, str) or not value or len(value) > MAX_ERROR_CODE_LENGTH:
        raise InvalidQueueEntry("invalid error code")
    if not re.fullmatch(r"[a-z0-9_.-]+", value):
        raise InvalidQueueEntry("invalid error code")
    return value


def _validate_time(value: float) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or value != value:
        raise InvalidQueueEntry("invalid timestamp")
    return float(value)


def _hash_file(path: Path, max_bytes: int) -> tuple[int, str]:
    digest = hashlib.sha256()
    total = 0
    with path.open("rb") as source:
        while True:
            chunk = source.read(64 * 1024)
            if not chunk:
                break
            total += len(chunk)
            if total > max_bytes:
                raise InvalidQueueEntry("local incident exceeds queue size limit")
            digest.update(chunk)
    return total, digest.hexdigest()


def _row_to_item(row: sqlite3.Row) -> QueueItem:
    return QueueItem(
        board_id=row["board_id"],
        event_id=row["event_id"],
        size=row["size"],
        sha256=row["sha256"],
        local_path=row["local_path"],
        state=row["state"],
        attempts=row["attempts"],
        lease_owner=row["lease_owner"],
        lease_until=row["lease_until"],
        next_attempt_at=row["next_attempt_at"],
        last_error_code=row["last_error_code"],
        remote_status=row["remote_status"],
    )


if __name__ == "__main__":
    print("This module is a library; use it from the S26 queue worker.")
