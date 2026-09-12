from __future__ import annotations

import hashlib
import tempfile
import unittest
from pathlib import Path

from scripts.s26_gateway_queue import GatewayQueue, InvalidQueueEntry, QueueConflict


class GatewayQueueTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = Path(self.temp_dir.name)
        self.database = self.root / "queue.sqlite3"
        self.incident = self.root / "incident.jsonl"
        self.payload = b"verified incident bytes\n"
        self.incident.write_bytes(self.payload)
        self.size = len(self.payload)
        self.sha256 = hashlib.sha256(self.payload).hexdigest()

    def tearDown(self):
        self.temp_dir.cleanup()

    def _enqueue(self, queue: GatewayQueue, event_id: str = "1-0"):
        return queue.enqueue("board-a", event_id, self.incident, self.size, self.sha256, now=100.0)

    def test_durable_state_machine_requires_mac_commit_before_ack(self):
        with GatewayQueue(self.database) as queue:
            item = self._enqueue(queue)
            self.assertEqual("QUEUED_LOCAL", item.state)

            claimed = queue.claim_upload("worker-a", lease_seconds=10, now=100.0)
            self.assertEqual("UPLOADING", claimed.state)
            self.assertEqual(1, claimed.attempts)
            with self.assertRaises(InvalidQueueEntry):
                queue.mark_acked("board-a", "1-0", now=101.0)

            pending = queue.mark_mac_committed(
                "board-a", "1-0", "worker-a", "stored", now=102.0
            )
            self.assertEqual("ACK_PENDING", pending.state)
            self.assertEqual([pending], queue.ready_for_ack())

            done = queue.mark_acked("board-a", "1-0", now=103.0)
            self.assertEqual("ACKED", done.state)
            self.assertEqual("stored", done.remote_status)

    def test_expired_upload_lease_is_recovered_after_reopen(self):
        with GatewayQueue(self.database) as queue:
            self._enqueue(queue)
            first = queue.claim_upload("worker-a", lease_seconds=10, now=100.0)
            self.assertEqual(1, first.attempts)

        with GatewayQueue(self.database) as queue:
            recovered = queue.claim_upload("worker-b", lease_seconds=10, now=111.0)
            self.assertEqual("worker-b", recovered.lease_owner)
            self.assertEqual(2, recovered.attempts)
            self.assertEqual("UPLOADING", recovered.state)

    def test_failure_is_retryable_with_bounded_backoff(self):
        with GatewayQueue(self.database) as queue:
            self._enqueue(queue)
            claimed = queue.claim_upload("worker-a", lease_seconds=10, now=100.0)
            failed = queue.mark_upload_failure(
                "board-a", "1-0", "worker-a", "network_timeout", now=100.0
            )
            self.assertEqual("QUEUED_LOCAL", failed.state)
            self.assertEqual("network_timeout", failed.last_error_code)
            self.assertEqual(102.0, failed.next_attempt_at)
            self.assertIsNone(queue.claim_upload("worker-b", lease_seconds=10, now=101.9))
            retry = queue.claim_upload("worker-b", lease_seconds=10, now=102.0)
            self.assertEqual(2, retry.attempts)

    def test_same_identity_same_hash_is_idempotent_but_different_hash_conflicts(self):
        with GatewayQueue(self.database) as queue:
            first = self._enqueue(queue)
            duplicate = self._enqueue(queue)
            self.assertEqual(first, duplicate)

            other = self.root / "other.jsonl"
            other.write_bytes(b"different incident bytes\n")
            other_sha = hashlib.sha256(other.read_bytes()).hexdigest()
            with self.assertRaises(QueueConflict):
                queue.enqueue("board-a", "1-0", other, other.stat().st_size, other_sha, now=101.0)

    def test_local_file_must_match_declared_metadata(self):
        with GatewayQueue(self.database) as queue:
            with self.assertRaises(InvalidQueueEntry):
                queue.enqueue("board-a", "1-0", self.incident, self.size + 1, self.sha256)


if __name__ == "__main__":
    unittest.main()
