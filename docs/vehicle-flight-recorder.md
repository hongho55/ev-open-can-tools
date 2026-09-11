# Vehicle flight recorder (board-local slice)

The dashboard firmware has an observational, bounded recorder. It is armed by
`mcpDashboardSetup()` and can be disabled or manually marked with
`POST /event_control` (`action=enable|disable|clear|mark`). It does not upload
anything and never participates in a vehicle-control decision.

## Rings and measured coverage

- **Raw ring:** documented/core RX IDs only, plus TX outcomes. Dual-CAN emits
  one record per physical bus attempt and a denied request is marked
  `txAttempted=false`; single-bus drivers emit the same distinction with an
  explicit physical bus label for submitted requests. TX records preserve the
  original semantic request in `busMask` and the actual physical destination
  in `physicalBus`.
  Each raw record contains monotonic `ms`, `direction` (`rx`/`tx`), exact
  `busMask`, `physicalBus`, CAN `id`, `dlc`, payload bytes, `txOk`, and `txAttempted` for TX
  records. Raw storage is key-ID bounded; it is not a whole-bus multi-minute
  promise.
- **State/decision ring:** deduplicated AP-state transitions, observed Nag mode,
  torque observations, injection decisions, CAN health and receive-liveness
  transitions, and settings changes. State records contain `ms`, `kind`, stable numeric `reason`, `value`,
  and `value32`. Upstream gate outcomes include CAN-disabled, AP-gate-blocked,
  summon-policy-blocked, and Nag-disabled reasons.
- Effective settings are retained outside the rotating state ring and are emitted
  as `config` records in every incident, with an immutable snapshot taken when the
  exact 10,000 ms post-event deadline freezes. Configuration updates use a
  depth-balanced boundary so overlapping writers cannot end one another's update;
  settings changes are still observed without extending the event window. In
  automatic speed-profile mode, the snapshot records the handler's active profile,
  not only the manual preference. A rearmed capture then uses live settings.
- Diagnostics expose the actual raw/state capacity, counts, monotonic coverage,
  state-only coverage, and raw/state drop counters. Verified PSRAM selects the
  larger rings (8192 raw and 6144 state entries). A failed or absent PSRAM probe
  selects the bounded, still-useful internal fallback (2048 raw and 3072 state
  entries).
- Pre-trigger state history is protected for five minutes. State writes are
  bounded per one-second bucket (16 with PSRAM, 8 in the internal fallback),
  and the recorder refuses to evict a state record younger than 300,000 ms.
  Excess bursts increment `stateProtectedDrops`; this preserves the time window
  without allowing an abnormal producer to exhaust it silently. Diagnostics
  expose `stateCoverageMs`, `stateTargetMs`, and `stateTargetReady`.
- Each ring reserves a bounded post-trigger partition. Once an incident is
  marked, new records cannot overwrite the retained pre-trigger history; a
  full post-trigger partition increments the corresponding drop counter.

## PSRAM validation

The `lilygo_t2can` target enables the N16R8 module's 8 MB OPI PSRAM. It also sets
`CONFIG_SPIRAM_IGNORE_NOTFOUND`, so a mismatched or failed module can still boot
and use internal RAM. At boot, `RuntimeDiagnostics::begin()` allocates a bounded 1024-byte buffer with
`heap_caps_malloc(...MALLOC_CAP_SPIRAM...)`, writes a deterministic pattern,
reads it back, and frees it. Diagnostics expose total PSRAM, probe size, and
`psramVerified`. Recorder allocation is attempted only after a verified probe;
allocation failure falls back without affecting CAN control.

## Incident format and preservation

Triggers are normal AP disengagement, AP abort, CAN error, CAN communication
loss, or the manual mark.
The recorder retains the pre-trigger rings and remains live for a monotonic
10,000 ms after the trigger. `tick(now)` freezes it even if no CAN frame arrives.
The web maintenance context then takes a generation-consistent snapshot of the
rings and effective configuration, writes `/incident.tmp`, checks every write and
close result, and atomically renames it to a new generation-specific
`/incident-<publication-sequence>-<slot>.jsonl` path. The publication sequence is
reserved in NVS before the file write and is also checked against complete files
and ACK tombstones, so reboot or deletion cannot reuse an incident ID. Equal-sequence
slots are resolved deterministically. The file is JSON Lines with schema
`t2can-flight-recorder-v1` (without the space): one header line containing
the durable board/incident identity plus firmware/build/reset/PSRAM/storage/trigger,
generation, capacity, coverage, and drop metadata, followed by effective `config`,
`state`, and `raw` records.

Recorder export operations reuse the configured dashboard/OTA basic
authentication:

- `GET /event_list` returns a bounded page of completed incidents with durable
  ID, size, SHA-256, ACK state, pending/acknowledged counts, free space, pressure,
  eviction count, and the last lifecycle error.
- `GET /event_download?id=<sequence>-<slot>` streams one immutable completed
  incident and supplies ID, size, SHA-256, and ETag headers. Omitting `id` keeps
  the dashboard's newest-incident download behavior.
- `POST /event_ack` accepts form fields `id`, `size`, and `sha256`. It recomputes
  the file digest and creates an atomic ACK marker only when all fields match.
  Repeating the same ACK is safe, including after the JSONL file was evicted
  while its bounded retry tombstone is retained.

The download path compares the byte
count returned by the streaming adapter with the expected file size; the
adapter checks every data and terminating chunk send, distinguishes file-read
errors from EOF, and logs a failed stream instead of silently treating a
truncated response as successful.

Before publishing a new incident, the board reserves the estimated file size
plus a free-space safety margin. It removes the oldest ACKed JSONL files until
the margin is available and keeps bounded ACK tombstones for retry safety. It
never automatically deletes an unacknowledged incident. If only pending evidence
remains, persistence fails visibly with `storage_full_unacknowledged` rather than
discarding it.

The normal WiFi dashboard runs the recorder maintenance tick in its web task.
BLE-only mode starts a separate recorder maintenance task that never calls the
WiFi/HTTP path, so deadline freeze, CAN-loss/liveness observation, and local
persistence continue while WiFi is disabled.

Persistence failures are reported in `/status` (`persistFailure`) and
`/diagnostics_detail` (`persisted` plus recorder capacity/coverage and
`incidentStore` lifecycle fields).
Filesystem I/O is never performed from `mcpDashOnFrame`,
`mcpDashOnTxFrame`, or a vehicle-control path.

Hardware TX attempt attribution is per send invocation: aggregate drivers keep
the semantic request mask in `busMask` and pass a separate exact physical bus
label plus the local attempt result to the recorder. A failed streamed download
marks the HTTP request failed so the server can close the incomplete response;
it does not emit a second response after partial data.

## Explicit non-goals

This slice does not add automatic upload, chat or cloud analysis, OTA behavior,
new CAN routing, plugin semantics, or new vehicle-control behavior. Recorder
allocation/logging failures fail open: they may reduce recorded evidence but
cannot block or alter TX permission.
