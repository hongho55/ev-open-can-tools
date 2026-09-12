# Automatic vehicle incident sync

This document defines the first automatic-transfer slice for the vehicle ESP32. It is read-only evidence collection; it does not enable CAN transmission, OTA, plugin changes, or automatic firmware changes.

## Real-world topology

```text
Vehicle CAN
    ↓
ESP32 recorder
    ↓ Wi-Fi (local vehicle link)
S26 gateway
    ↓ encrypted app-private queue
mobile data or trusted Wi-Fi
    ↓ Tailscale private path
Mac mini archive
    ↓ local analysis
Hermes / optional redacted Oracle review
```

The Mac mini cannot normally reach the ESP32 while it is installed in the vehicle. The S26 is therefore the practical field gateway. The S26 should not be a live relay: it downloads a completed incident, stores it durably, and uploads it later when normal Internet connectivity is available.

## What is implemented now

`scripts/collect_vehicle_incidents.py` is a one-shot reference client for the local ESP32 evidence protocol. It exercises the same critical path that the future S26 worker must implement:

```text
GET /event_list
  → GET /event_download?id=<sequence>-<slot>
  → download to .part
  → verify ID, size, SHA-256, ETag, and Content-Length
  → atomic private archive commit
  → read-back size and SHA-256 verification
  → POST /event_ack
```

The client:

- accepts only private/local ESP32 addresses;
- uses credentials from an environment variable or stdin, never a password command-line argument;
- rejects unknown list schemas, malformed IDs, missing hashes, and oversized incidents;
- never overwrites an existing incident with different bytes;
- treats the same incident and hash as an idempotent duplicate;
- leaves the ESP32 incident unacknowledged when download or archive verification fails;
- writes raw and metadata files with mode `0600`;
- never sends the raw incident to Telegram or Oracle.

Development-only example (keep credentials out of shell history and chat):

```bash
export T2CAN_RECORDER_USER='read-only-user'
export T2CAN_RECORDER_PASSWORD='use-a-private-secret-store'
python3 scripts/collect_vehicle_incidents.py \
  http://192.168.4.1 \
  --output-dir ~/t2can-incidents
```

The current firmware export endpoints still reuse the configured dashboard/OTA Basic authentication. That credential coupling must be separated into a dedicated read-only recorder principal before unattended S26 operation. Do not put an OTA/admin password in an Android app.

## Mac receiver and S26 upload contract

`scripts/receive_vehicle_incidents.py` is the private receiver slice. It binds
only to `127.0.0.1`, requires the upload token from an environment variable, and
accepts one binary incident body at `POST /v1/incidents`. Tailscale Serve can
later provide the tailnet-only HTTPS edge in front of this loopback listener;
the receiver must not be bound to `0.0.0.0` and Tailscale Funnel is not part of
this design.

The future S26 upload request sends these headers and the incident bytes as the
request body:

```text
Authorization: Bearer <S26 upload token>
X-T2CAN-Incident-Id: <sequence>-<slot>
X-T2CAN-Board-Id: <board id from the local event list>
X-T2CAN-Size: <byte count>
X-T2CAN-SHA256: <64 lowercase hex characters>
Content-Length: <same byte count>
```

The receiver responds with `stored` only after temporary-file write, hash
verification, atomic rename, and read-back verification. The same ID and hash
returns `already_stored`; the same ID with different bytes returns a conflict.
The S26 must persist that result before it sends `/event_ack` to the ESP32.

Development-only local receiver example (keep the token out of chat and shell
history):

```bash
export T2CAN_UPLOAD_TOKEN='use-a-private-secret-store'
python3 scripts/receive_vehicle_incidents.py \
  --archive-dir ~/t2can-incidents \
  --port 8787
```

This is not a claim that Tailscale Serve or an Android app is configured yet;
those are separate deployment and physical-device milestones.

## S26 automatic worker

The future S26 app should use this durable state machine:

```text
IDLE
  ↓ ESP32 discovered on S26 hotspot
CONNECT_LOCAL
  ↓ event list
DOWNLOAD_TO_PART
  ↓ size/hash verified
QUEUED_LOCAL
  ↓ local queue commit
RELEASE_ESP32_NETWORK
  ↓ Internet/Tailscale available
UPLOAD_TO_MAC
  ↓ Mac durable commit + read-back response
ACK_PENDING
  ↓ next ESP32-local session, or current session if still connected
ACK_ESP32
  ↓
DONE
```

The app must survive process death, reboot, no Internet, ESP32 AP loss, Mac unavailability, and a lost upload response. A queue entry is complete only after the Mac's `event_id`, size, and SHA-256 commit result has been durably recorded on the S26. A failed or uncertain upload must be retried; it must never cause an early ESP32 ACK.

The first Android version should use Wi-Fi/HTTP for event files. BLE is optional for a small health snapshot and should not be required for the event-transfer path. The app should not depend on simultaneous ESP32 Wi-Fi and normal Internet connectivity; finish the local download, release that network, then upload over the normal Internet path.

## Mac mini receiver requirements

The eventual receiver should:

1. listen on localhost only;
2. be exposed to the S26 through private Tailscale Serve/access policy, not a public port or Funnel;
3. write to a temporary file with a bounded maximum size;
4. verify the declared event ID, size, and SHA-256;
5. atomically commit an append-only raw archive;
6. read the committed file back and verify it;
7. return an idempotent `stored` or `already_stored` result;
8. return `conflict` for the same event identity with different bytes;
9. keep analysis output separate from raw evidence.

A successful HTTP response before durable commit is not success. The S26 must not ACK based on a transport status alone.

## Automation boundaries

Safe to automate:

- ESP32 recorder operation and completed incident creation;
- S26 discovery, download, local verification, and queueing;
- delayed upload to the private Mac receiver;
- duplicate detection and retry;
- Mac local analysis and bounded Telegram status notifications.

Not automated:

- CAN TX or injection;
- changing DAS layout from observed frames;
- generating CAN TX rules;
- OTA or firmware flashing;
- sending raw vehicle evidence to external AI without a human-reviewed redacted derivative.

## Hard gates before unattended operation

- [ ] ESP32 read-only recorder credential is separate from OTA/admin/write credentials.
- [ ] Completed and in-progress event files survive the documented power-loss cases, or the volatile pre-persistence limitation is explicitly accepted.
- [ ] Local download verifies the real ESP32 headers and body hash.
- [ ] Mac receiver has durable commit and read-back tests.
- [ ] S26 queue survives reboot/process death and never ACKs early.
- [ ] Recorder/persistence has priority over HTTP serving and upload work.
- [ ] Tailscale path is private; no public listener or Funnel exposure exists.
- [ ] Physical S26 tests verify counts and hashes without logging private raw payloads.
- [ ] No real-vehicle firmware change is attempted from an analysis result.
