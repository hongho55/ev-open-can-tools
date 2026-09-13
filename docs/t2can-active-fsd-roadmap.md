# T-2CAN Active FSD integration roadmap

> Status: approved planning baseline; not an implementation or a claim of vehicle validation.
>
> Scope: LILYGO T-2CAN dual-CAN firmware, ESP32 dashboard/BLE/OTA control plane,
> Flipper FSD-derived decoders and transmit features, offline replay, self-test,
> incident evidence, and the S26/Mac maintenance path.
>
> Out of scope for this document: connector pin maps, harness pin selection, and
> public-road test procedures.

## Bottom line

This project will retain CAN transmit features and add device OTA. It will not
be converted into a receive-only product. The design goal is instead:

> Keep active CAN and OTA capabilities, but force every operation through one
> observable, expiring, fail-closed authorization and verification path.

The firmware shall have four explicit operating sessions:

1. **Observe** — CAN receive, decoding, GVRET, diagnostics, and recording. No
   effective transmit permission.
2. **Active** — reviewed built-in features or plugins may request CAN TX through
   the common policy and scheduler.
3. **Bench** — internal controller loopback and isolated transceiver tests. This
   session must not be confused with a vehicle Active session.
4. **Maintenance** — device OTA, rollback, artifact verification, and post-boot
   self-test. CAN TX is paused while the image is applied and resumes from the
   persisted desired state only after the normal startup and bus-health gates
   become healthy again.

Feature configuration and the user's master TX enabled/disabled choice persist
across ordinary vehicle power cycles, watchdog resets, and successful OTA. A
restart never bypasses startup freshness, CAN readiness, vehicle-state, AP,
Summon-only, quarantine, or OTA gates: persisted enablement is desired state,
not permission to transmit before those gates pass.

## Evidence baseline

The following results were collected before writing this plan:

- `t2can-bobby`: `python3 -m platformio test -e native` completed with
  **196/196 native test cases passing**. Coverage includes dual-CAN routing,
  simulated loopback, recorder behavior, GVRET framing, HW3/HW4/Legacy
  handlers, injection gates, TWAI filters, and MCP2515 recovery.
- `sqladm1n/flipper-tesla-fsd`: `make -C test check` completed with
  **236 passed, 0 failed** for its protocol core. These are that fork's host
  tests, not proof that its behavior is correct on this project's hardware or
  target vehicle.
- The checked-out `sqladm1n` research set contains **78 candump logs** and
  **1,316,711 parsed frames**. The current parser accepted every frame line in
  those logs.
- The set contains 348 unique CAN IDs. Relevant observed counts include:
  - `0x247`: 4,569 RX frames across 39 files;
  - `0x3E9`: 1,940 RX frames across 39 files;
  - `0x229`: 2,328 RX frames across 38 files, all DLC 3;
  - `0x3FD`: 25,000 total frames across 77 files;
  - `0x7FF`: 3,751 RX frames across 39 files.
- All 11,853 frames in the `_tx.log` files are `0x3FD`. Therefore this data is
  transmit evidence for `0x3FD` only; it is **not** evidence that `0x247`,
  `0x229`, `0x370`, or `0x485` transmission works.
- `0x370` and `0x485` are absent from this dataset. Existing Nag and gear tests
  for those IDs remain synthetic until a separate capture is obtained.
- The current Python analyzer can parse the research logs and decode its
  existing `0x7FF`/diagnostic surface, but it does not yet replay candump frames
  through the complete firmware handler and TX-decision pipeline.

These facts define the present evidence boundary. Passing host tests or parsing
community captures does not establish ECU acceptance or safe vehicle behavior.

## Decisions for the requested feature set

| Requested feature | Decision | Phase | Required interpretation |
| --- | --- | --- | --- |
| OTA flashing | **Adopt** | P0 | Signed, board-bound, dual-partition device OTA with boot confirmation and rollback. |
| BLE owner/key provisioning | **Adopt with scope restriction** | P1 | Provision a device-owner authorization key only. Never import or expose a Tesla drive/passive-entry key. |
| Web-based remote CAN control | **Adopt with network and policy restrictions** | P1 | Private/local control transport that creates `TxIntent`; it never calls a CAN driver directly. |
| Default TX activation | **Persist user choice behind safety gates** | P0 | The master enabled/disabled choice and feature configuration survive vehicle power cycles and OTA. Every boot blocks physical TX until startup freshness, CAN readiness, bus health, and feature-specific gates pass. |
| `0x247` spoof | **Research and bench candidate** | P2 | Add RX decoding and dry-run correlation first. The available dataset has no `0x247` TX evidence. |
| Nag killer actual TX | **Retain and harden** | P1/P2 | Keep existing bounded modes and policy; add research replay, scheduler ownership, and isolated bench verification. |
| ULC unlock | **Deferred high-risk research** | P3 | Decoder and dry-run evidence first; no production enablement without authoritative format and target evidence. |
| Automatic Summon control | **Replace with supervised Summon session** | P3 | User-initiated, expiring, dead-man/abort-controlled session; no unattended or scheduled vehicle motion. |
| Automatic bitrate detection | **Adopt as listen-only recommendation** | P1 | Probe and score candidates without transmitting; require explicit confirmation before Active TX locks a bitrate. |
| Layout change from observed frames | **Adopt as recommendation only** | P1 | Report candidate/confidence/evidence; never change the effective layout automatically. |
| Automatic CAN-rule generation | **Adopt as disabled draft generation** | P1 | Produce a reviewed `send:false` draft plus fixtures and evidence; never install or enable automatically. |
| Commit raw vehicle logs | **Do not adopt** | P0 | Keep private local originals; commit only minimized, sanitized, provenance-labelled fixtures. |
| Send raw vehicle payloads to cloud | **Do not adopt by default** | P0 | Analyze locally and send only bounded derived summaries unless a separate explicit export is approved. |

## Complete Flipper FSD feature-port inventory

The roadmap tracks the complete useful feature surface rather than copying an
entire fork. `Retain` means this repository already has the feature or an
equivalent that must survive the refactor. `Port` means implement the protocol
behavior behind this project's common state/policy/driver interfaces. `Research`
means decoder, replay, and dry-run work only until the verification ladder is
satisfied. `Do not port` is an explicit decision, not an accidental omission.

The `sqladm1n` README describes 37 handlers at an earlier release boundary; its
current shared header contains additional helpers and newer beta features. The
inventory below therefore uses named behaviors rather than relying on that stale
handler count.

### Core, lifecycle, and tooling

| Flipper FSD behavior | Disposition | Phase and adaptation |
| --- | --- | --- |
| HW3/HW4 detection from `0x398` plus `0x3FD`/`0x399`/`0x3EE` fallbacks | **Port and cross-check** | P1 decoder registry; confidence-labelled per bus. Never change an effective layout without confirmation. |
| Palladium Legacy-to-HW3 runtime upgrade | **Port conditionally** | P1 recommendation/state transition; Active behavior requires stable repeated evidence and policy re-evaluation. |
| HW3/HW4 `0x3FD` and Legacy `0x3EE` FSD frame handling | **Retain and harden** | P0 common scheduler; P2 per-profile vehicle qualification. |
| Force FSD / China-mode UI-selection bypass | **Retain as explicit profile feature** | P2; off by default, audited, and never represented as an entitlement bypass. |
| Speed profile default, follow-distance synchronization, profile lock, and HW4 offset | **Port/merge** | P1 decoder/state work; P2 write qualification where a frame is modified. |
| Active / Listen-Only / Service operation modes | **Replace with common session model** | P0 Observe/Active/Bench/Maintenance state machine. |
| AP-First and stable-AP debounce | **Port** | P1 state gate; P2 applies to every relevant FSD/Nag TX path. |
| 2026.14.x compatibility warning | **Port as capability warning** | P1 dashboard/Support notice tied to observed profile/evidence, not a free-standing toggle. |
| Continuous AP / automatic re-engagement | **Research; no direct port** | P3 supervised, bounded state machine only. Brake and stalk aborts must fail closed. |
| Vehicle OTA detection, debounce, TX pause, and Ignore-OTA override | **Retain and harden** | P0 separate `VehicleOtaState`; any override is visible, expiring, and audited. |
| CAN capture in candump format | **Retain/extend** | P0 recorder provenance and privacy policy. |
| Full-rate single-ID hardware filtering and larger RX queue | **Port where supported** | P1 capture mode; filter ownership cannot hide frames needed by safety gates. |
| ESP32 HTTP CAN stream and ID filters | **Merge into private recorder/control plane** | P1; authenticated, bounded, local/private, and not a public raw-payload endpoint. |
| `.cantest` parser, dry-run, parked/stationary interlock, and result log | **Port in stages** | P1 dry-run only; P2 physical Bench execution after semantic denylist and per-ID policy. |
| Checksum/CRC research tooling | **Port as offline tooling** | P1 local analysis; recovered candidates remain untrusted until independently verified. |
| Shared Flipper/ESP32 protocol-core idea | **Adopt architecturally** | P0/P1 one host-testable protocol core; do not import Flipper hardware/UI dependencies. |
| Wi-Fi AP/STA settings, masked secrets, NVS persistence | **Retain equivalent and harden** | P1 owner authorization and read-back; secrets never enter snapshots or logs. |
| Deep sleep and factory reset | **Retain/verify existing equivalent** | P1 lifecycle tests; ordinary wake/reset restores desired feature state but not stale safety observations. An explicit factory reset clears persisted state. |
| Live counters, CRC errors, no-traffic warning, status display | **Retain and extend** | P0 per-bus health plus dashboard/BLE/Support parity. |
| SD log rotation and bounded capture duration | **Port policy, adapt storage backend** | P1 recorder limits and explicit storage-pressure events. |

### Transmit and frame-mutation features

| Feature | CAN surface | Disposition |
| --- | --- | --- |
| Core FSD unlock, including Legacy path | `0x3FD`, `0x3EE` | **Retain; P0 scheduler, P2 qualification.** |
| Enhanced Autopilot/Summon enable flag | `0x3FD` | **Port with the core handler; P2, profile-specific.** It does not itself implement Summon motion control. |
| Speed profile and HW4 speed offset writes | `0x3FD` | **Port/merge; P2.** Preserve mux and checksum/counter evidence. |
| Emergency-vehicle detection flag | `0x3FD` | **Port as disabled module; P2.** Separate capability and policy entry. |
| TLSSC bit38 and Lane Graph | `0x3FD` | **Port as separate disabled modules; P2.** Do not silently couple unrelated bits. |
| FSD unlock for Legacy hardware | `0x3EE` | **Retain; P2 profile qualification.** |
| Nag killer, DAS-aware gate, organic variation, and grip pulse | `0x370` | **Retain and harden; P1/P2.** Include `0x399` fallback and AP-First interaction. |
| ISA speed-warning chime suppression | `0x399` on HW4 | **Retain/port with strict HW dispatch; P2.** Never mutate pre-Highland DAS-status `0x399`. |
| Battery preconditioning request | `0x082` | **Port as bounded Active feature; P2.** Add state, cadence, expiry, and abort policy. |
| TLSSC Restore | `0x331` | **Port as high-impact disabled module; P2.** Record the expected MCU/UI effect and require fresh read-back. |
| GTW Config Replay | `0x7FF` | **Port with honest naming; P2.** Broadcast-layer replay only; not ban prevention or backend/NVRAM repair. |
| GTW Tier Override | `0x7FF` mux 2 | **Research then Bench; P3.** More aggressive than replay and mutually exclusive with it. |
| Nav FSD Route | `0x3F8` | **Port as disabled module; P2.** |
| Hands-Off UI flag | `0x3F8` | **Research; P3.** Do not treat a UI signal as sensor-level validation. |
| Developer Mode | `0x3F8` | **Port as disabled diagnostic experiment; P2.** |
| Force-LHD/RHD signal override | `0x3F8` | **Do not port.** Target deployments are left-hand-drive only, and upstream reports no lane-side effect from this UI signal. Preserve observed driving-side data only when another decoder needs it; never transmit an override. |
| Telemetry Off | `0x3F8` | **Research only; P3.** It may itself be a detection signal. |
| ScrollPress AP Engage | `0x3C2` mux 1 | **Research then Bench; P3.** HW4/profile-specific; explicit deny on unsupported HW. |
| Track Mode request | `0x313` | **Port as Service/Bench-only module; P2.** Require checksum, stationary state, expiry, and read-back. |
| Hazard request and Wiper Off | `0x3F5` | **Port as separate disabled modules; P2.** Shared-ID modules need conflict arbitration. |
| Rear fog and other legacy `0x3F5` extras | `0x3F5` | **Inventory/research first; P3.** Current shared handler header does not prove every older menu toggle still has an implementation. |
| Park-button inject | `0x229` | **Do not port for TX.** Keep read-only decoder and an explicit `.cantest` deny rule; upstream identifies collision/gear-request risk. |
| Steering tune request | `0x101` | **Research then Bench; P3.** Chassis-bus feature with a distinct high-impact policy class. |
| High-beam strobe | `0x249` | **Port only as bounded Bench/Service candidate; P2.** |
| Turn-signal left/right | `0x249` | **Port only as bounded Bench/Service candidates; P2.** |
| Wiper wash | `0x249` | **Port only as bounded Bench/Service candidate; P2.** |
| Fold mirrors and rear-window heat legacy extras | fork UI/handler dependent | **Inventory and source-verify; P3.** Do not claim a port until the exact current handler and CAN contract are found. |
| `0x247` hands-on spoof | `0x247` plus `0x3E9` observation | **Research P1, Bench candidate P2.** Current dataset provides RX correlation, not TX proof. |
| Continuous AP | multiple observed state inputs plus an engage path | **No literal automatic port; P3 supervised design only.** |
| Summon motion control | multiple, not established by the EAP flag | **No unattended port.** P3 supervised research only after exact command semantics and abort path exist. |

### Read-only telemetry and safety inputs

All useful read-only parsers are in scope because they improve observability or
policy gating. A parser used as a TX gate needs stronger freshness, profile, and
negative-case evidence than a dashboard-only parser.

| Signal family | CAN IDs | Disposition |
| --- | --- | --- |
| Hardware/profile and gateway configuration | `0x398`, `0x7FF` | **Port/merge P1.** Detection remains a recommendation until confirmed. |
| Vehicle OTA state | `0x318` | **Retain and harden P0.** |
| BMS voltage/current/power, SoC, thermal, energy consumption | `0x132`, `0x292`, `0x312`, `0x33A` | **Retain/complete P1.** Keep model-specific confidence. |
| Vehicle/UI speed and wheel speeds | `0x257`, `0x175` | **Retain/complete P0/P1.** Freshness-critical safety inputs. |
| Brake/stability | `0x145` | **Retain P0.** Missing or stale state blocks dependent actions. |
| Track/traction and rear-defrost state | `0x118`, `0x343` | **Port P1.** |
| EPAS tune mode and torsion-bar torque | `0x370` | **Retain/complete P1.** Separate observation from Nag mutation. |
| DAS status, hands-on, lane change, blind spot, FCW, vision limit | `0x39B`, HW-dependent `0x399` | **Retain/complete P1.** Explicit hardware dispatch. |
| DAS status2 and activation failure | `0x389` | **Retain/complete P1.** |
| DAS settings/autosteer read-back | `0x293` | **Retain/complete P1.** |
| ACC state and set speed | `0x2B9` | **Retain/complete P1.** |
| Cruise, gear, park brake, autopark, digital speed | `0x286` | **Retain/complete P1.** |
| Motor torque | `0x108` | **Retain/complete P1.** |
| Blinker, door, belt, and high-beam warning state | `0x311` | **Retain/complete P1.** |
| Steering angle and DAS steering request | `0x129`, `0x488` | **Retain/complete P1.** |
| Steering-permission monitor | `0x27D` | **Source-audit then port P1.** The current header names the ID but exposes no matching public parser prototype. |
| Right/left stalk observations | `0x229`, `0x249` | **Port read-only P1.** `0x229` remains TX-denied. |
| Hands-on/nag research correlation | `0x247`, `0x3E9` | **Research P1.** |

### Explicit non-porting rule

“Everything is inventoried” does not mean “everything is enabled.” The project
will not wholesale-copy Flipper scenes, GPIO/MCP2515 glue, unverified menu
toggles, source-specific persisted-state layouts, or a handler whose current
source and CAN contract cannot be identified. GPL-derived protocol behavior must
retain source/ref/license provenance. Every accepted behavior is adapted to
T-2CAN dual-bus routing and the common policy, scheduler, OTA, recorder, and test
interfaces.

## Target architecture

### One command path

Web, BLE, built-in features, plugins, replay, and future clients must share the
same command path:

```text
Web / BLE / built-in feature / plugin
                 |
                 v
              TxIntent
                 |
                 v
          CommandAuthorizer
                 |
                 v
              TxPolicy
                 |
                 v
            TxScheduler
                 |
                 v
          CAN A / CAN B driver
                 |
                 v
              TxAudit
```

No web endpoint, BLE command, plugin callback, or built-in handler may bypass
this path and call the hardware driver directly.

### State separation

Do not continue expanding one all-purpose FSD state structure. Keep these
responsibilities separate:

- `VehicleState`: observed speed, gear, brake, AP, Summon, driver input, and
  freshness.
- `CanBusHealth`: per-physical-bus liveness, queue pressure, errors, bus-off,
  quarantine, and recovery.
- `TxIntent`: requested feature, bus, frame class, expiry, source, and request ID.
- `TxPolicy`: mode, arm lease, feature allowlist, state gates, rate/burst limits,
  and abort reasons.
- `TxScheduler`: cadence, counter/checksum ownership, queueing, cancellation,
  and ambiguous-result handling.
- `DeviceOtaState`: device firmware download, verification, apply, reboot,
  confirmation, and rollback.
- `VehicleOtaState`: CAN-observed vehicle update state and its TX-pause policy.
- `EvidenceState`: recorder generation, capture provenance, test results, and
  audit records.

### Operating-state outline

```text
BOOT
  -> OBSERVE
       -> ACTIVE_ARMED -> ACTIVE_TX
       -> BENCH_INTERNAL_LOOPBACK
       -> BENCH_TRANSCEIVER
       -> MAINTENANCE_OTA

Any stale safety input, bus quarantine, control-channel loss, device OTA,
vehicle OTA policy, watchdog reset, or explicit stop:
  -> TX_BLOCKED
```

`TX_BLOCKED` must be a first-class state shown in the dashboard, BLE snapshot,
Support report, serial log, and incident evidence. It must include a stable
reason code.

## P0 — foundation required before adding more active features

### P0.1 Hardware self-test framework

Implement self-test as separate modes rather than reusing a vehicle feature.
The `wangjizhu` fork is useful as a behavioral reference because it switches an
MCP2515 into true internal loopback and reports TX/RX/error counts. Its Normal
mode and fixed test-frame behavior must not be copied into a vehicle session.

Required tests:

1. MCP2515 internal loopback on CAN A;
2. TWAI internal loopback on CAN B;
3. exact TX-to-RX payload comparison;
4. physical and semantic bus attribution;
5. timestamp monotonicity;
6. rolling counter and checksum fixtures;
7. RX/TX queue saturation and bounded failure;
8. driver init, stop, and re-init;
9. one-controller failure while the other controller and USB diagnostics remain
   available;
10. cancellation and cleanup when the user leaves Bench mode.

Bench transceiver testing is a separate stage and requires a disconnected,
isolated CAN fixture and a second logger. MCP2515 CAN A supports controller
internal loopback. ESP32-S3 TWAI CAN B has no isolated internal-loopback mode;
its vehicle-connected self-test is therefore controller/status-only, while its
TX/RX path is verified only on that isolated fixture. Neither result proves the
vehicle path works.

Acceptance:

- Self-test starts in internal loopback only.
- Entering physical Bench TX requires a distinct action and visible mode.
- Leaving the mode stops pending periodic work and clears the TX lease.
- The report records firmware identity, controller, bus, counts, errors, and the
  exact test result without including credentials.
- A firmware build cannot enter Bench mode from a CAN frame or diagnostic
  observation.

### P0.2 Candump replay adapter

Add an offline adapter that sends parsed candump records through the same
receive handlers and state transitions used by firmware:

```text
candump -> FrameRecord -> normalized time/bus -> CanFrame
        -> decoder/state -> TxIntent/would-send -> evidence
```

Replay never calls a physical driver. A TX-producing handler must emit a
`would-send` result containing the planned bus, ID, DLC, policy result, and
output digest. The raw output frame remains in the private local test artifact.

Initial replay targets:

- `0x247` plus `0x3E9` correlation;
- `0x229` DLC/counter/state analysis;
- `0x3FD` mux and RX/TX comparison;
- `0x7FF` mux/config and replay-policy behavior;
- existing `0x399`/`0x39B`, speed, gear, brake, and telemetry parsers.

Acceptance:

- The adapter parses the complete 1,316,711-frame research set with zero
  unsupported-line failures.
- Decoder errors, rejected DLCs, unknown layouts, timestamp regressions, and
  drops are counted rather than silently skipped.
- Replay is deterministic: the same input, profile, and code revision produce
  the same derived output.
- Community raw logs remain outside Git; tests use local paths or sanitized
  fixtures.

### P0.3 Common TX scheduler, policy, and audit

Keep TX, but ensure one subsystem owns physical emission.

Every `TxIntent` must carry:

- source and feature ID;
- request ID and expiry;
- target semantic and physical bus;
- expected ID, DLC, and mux constraints;
- cadence, burst, and cooldown limits;
- counter/checksum strategy;
- required vehicle-state freshness;
- required operation mode;
- cancellation and abort policy.

Rules:

- Unknown, stale, contradictory, or unavailable safety state blocks the intent.
- Control-channel disconnect cannot bypass an already closed safety gate or keep
  a supervised, session-scoped action alive; persistent background features use
  the stored master enable choice.
- Reboot, brownout, watchdog reset, and OTA invalidate all observed freshness and
  pending work. Stored enablement may resume only after fresh startup and
  per-feature safety gates pass again.
- A send result is recorded as requested, blocked, attempted, succeeded, or
  failed. A successful driver return is not ECU acceptance.
- An ambiguous or timed-out state-changing action is reconciled from fresh state
  before any retry; it is not blindly repeated.
- Per-feature allowlists and rate limits apply to built-in handlers and plugins
  equally.
- Driver-input abort and a physical disconnect/kill path remain available for
  any controlled active test.

Acceptance:

- Tests prove that no control surface can bypass `TxPolicy`.
- A blocked intent produces no physical attempt on either bus.
- Combined-bus requests preserve separate per-bus results.
- Pending work is cleared by mode change, stale state, OTA, quarantine, stop,
  disconnect, and reboot simulation. Persistent desired state is evaluated from
  scratch after recovery; queued frames are never restored.
- Recorder output identifies the exact source, reason, bus, and attempt result.

### P0.4 Per-bus health, RX stall, and quarantine

Maintain independent state for CAN A and CAN B:

```text
STARTING -> HEALTHY -> DEGRADED -> RX_STALLED
         -> BUS_ERROR -> QUARANTINED -> RECOVERED
```

Measure:

- last RX and TX timestamps;
- frame rate and unique-ID count;
- RX/TX queue pressure and drops;
- controller error and bus-off state;
- failed/successful TX counts;
- stall, quarantine, and recovery counts;
- last stable reason code.

A failed bus must not take down the other bus, USB diagnostics, recorder, or
maintenance control plane. Quarantine blocks TX for that physical bus. Recovery
may restore observation, but effective TX requires policy re-evaluation against
fresh state before the persisted desired enablement can resume.

Implemented minimal checkpoint:

- `DualCanDriver` owns one small health tracker per physical bus. Fresh RX is
  required before TX; RX stall or three attempted TX failures quarantine only
  the affected target bus.
- TWAI publishes a monotonic fault epoch when it observes BUS_OFF/RECOVERING so
  recovery cannot transmit on a still-fresh pre-fault timestamp.
- `/status` and serial diagnostics expose state, quarantine, timestamps, and
  stall/quarantine/recovery counts for CAN A and CAN B.
- Regression acceptance passed: Python 94 tests, native 201 tests, T-2CAN build,
  USB flash/read-back, S20-over-USB-ADB HTTP `/status`, and maintenance CAN
  self-test with `physicalTx=false`. The observed no-traffic baseline was
  `starting`, `quarantined=true`, `rx=0`, and `tx=0` independently on both buses.
- Explicitly deferred by operator approval: vehicle-connected fresh-RX,
  one-bus fault injection, and post-recovery fresh-RX acceptance. This remains a
  required vehicle validation receipt and is not implied by the internal
  self-test.

### P0.5 Device OTA transaction and rollback

Device OTA and CAN-observed vehicle OTA are different state machines.

Required device OTA flow:

```text
IDLE -> AUTHORIZED -> DOWNLOADING -> VERIFIED -> APPLYING
     -> REBOOTING -> SELF_TEST -> CONFIRMED
                              \-> ROLLBACK
```

Requirements:

- one approval names one artifact, board/profile, version, and maintenance
  session;
- authenticated private transport and signed manifest;
- independently reported file size and digest;
- board, flash size, partition layout, and feature-profile compatibility checks;
- dual OTA partitions and a bounded boot-confirmation window;
- TX pause and pending-queue clear before applying an image;
- post-boot internal self-test and read-back of firmware identity, CAN health,
  dashboard/BLE availability, and recorder state;
- rollback to a known-good image when confirmation fails;
- preservation of the failed-image diagnostics and rollback reason;
- no firmware secrets, credentials, or keys in URLs, logs, Telegram, or incident
  exports.

Existing HTTPS, board-artifact, streaming, and concurrency checks remain and are
extended rather than replaced.

Implemented rollback checkpoint:

- T-2CAN enables the ESP-IDF bootloader rollback feature while retaining the
  existing dual `ota_0`/`ota_1` partition layout.
- A `PENDING_VERIFY` image establishes the maintenance TX inhibit before NVS,
  dashboard, or CAN setup. Confirmation requires final NVS success, verified
  PSRAM, a ready dual driver, and two non-physical controller self-test passes.
- Failed preflight explicitly requests invalid-image rollback; dashboard cleanup
  cannot release the inhibit while boot state is pending or rollback.
- `/status` exposes the boot transaction state, preflight result, deadline, and
  ESP-IDF error code.
- Acceptance passed with Python 100 tests, native 206 tests, a T-2CAN build and
  USB flash, followed by a real authenticated 1,427,072-byte OTA from the S20.
  Serial showed TX quiesce, reboot, pending verification, and local confirmation;
  `/status` read back `state=confirmed`, `preflightPassed=true`, `lastError=0`,
  and zero TX attempts with both disconnected buses still quarantined.

Vehicle OTA detection continues to pause active features according to policy.
An `ignore vehicle OTA` override, if retained, must be explicit, session-scoped,
visible, and audited; it cannot become a silent persistent default.

### P0.6 Firmware identity and capability manifest

Every status, maintenance snapshot, incident, replay report, and OTA result must
include:

- firmware version and Git revision;
- build environment and board/profile;
- supported physical and semantic buses;
- enabled feature modules;
- decoder schema version;
- TX policy version and effective mode;
- OTA slot/state and rollback reason;
- self-test result;
- non-secret configuration digest.

The same manifest is used to reject an artifact built for a different board or
feature profile.

Implemented minimal checkpoint:

- PlatformIO injects the 12-character Git revision and exact build environment.
- `/status` exposes the compact manifest, effective TX mode, OTA state/artifact,
  self-test state, and a non-secret digest of TX-relevant settings.
- Manual OTA rejects a filename that does not match the build environment's
  board-specific release artifact before TX quiesce or flash initialization.
- The focused manifest contracts passed 4/4 and `lilygo_t2can` built successfully
  from commit `c62aa87`. S20 OTA and `/status` read-back are explicitly deferred
  to the next field-device session.

### P0.7 Private evidence and fixture policy

- Raw community and vehicle captures remain private local artifacts.
- Raw logs, VINs, device identifiers, credentials, tokens, owner keys, and
  unredacted payload collections are not committed.
- A fixture extraction tool creates the minimum frame sequence required for one
  test and attaches source, transformation, purpose, and expected result.
- Cloud/LLM analysis receives ID, DLC, timing, count, transition, and derived
  fields by default, not raw payloads.
- Private originals receive restrictive permissions and a content digest.
- No capture may automatically create, install, enable, or execute a TX rule.

Implemented minimal checkpoint:

- Repository-local `private-evidence/`, `captures/`, and `incidents/` paths are
  ignored; existing collectors continue to default outside the repository.
- `scripts/extract_private_fixture.py` requires explicit CAN IDs, purpose, and
  expected result; it supports bounded time windows and refuses oversized
  selections.
- Source SHA-256 and transformation metadata are retained without the source
  path. Payload bytes are omitted by default and require an explicit option.
- Generated fixtures force `send:false` and disabled installation state. Source
  and output permissions are read back as `0600` (`0700` output directory).
- Focused tests passed 4/4; a real CLI run reduced a synthetic incident to two
  ordered `0x247` observations with no payload or identity leakage.

## P1 — controlled automation and broader decoding

### P1.1 Decoder registry and confidence model

Move signal definitions toward a table/registry that records:

- ID, bus, DLC, mux, field extraction, scale, signedness, and freshness;
- supported vehicle/profile assumptions;
- source and evidence level;
- decoder version;
- `observed`, `inferred`, or `confirmed` confidence;
- whether the definition may be used only for display, for a policy gate, or for
  TX generation.

A display-only observation does not automatically become a permission gate.

Implemented initial registry checkpoint:

- `include/chassis/decoder_registry.h` is the metadata source for 16 existing
  decoded signals, including bus, DLC, mux, extraction, scale/offset, signedness,
  freshness, profile, source/evidence, confidence, and allowed use.
- Existing decoder behavior remains stable; new research decoders must register
  their evidence and use class here rather than adding undocumented switches.
- `/status` manifest reads the registry schema and entry count directly from this
  source. Registry entries cannot enable TX and the initial set contains no
  `TxGeneration` use.
- Native registry assertions passed by direct compile/run and `lilygo_t2can`
  built successfully. S20 read-back remains deferred with the other field-device
  checks.

### P1.2 `0x247`/`0x3E9` research replay

First milestone:

- decode and timeline `0x247` observations;
- detect `0x3E9` candidate nag/satisfied/inactive transitions;
- correlate changes without generating physical TX;
- compare the fork's claims against all 39 relevant capture files;
- produce minimized fixtures for confirmed parser behavior;
- expose unknown/contradictory patterns rather than coercing them.

Only after this milestone may a `0x247` TX module enter P2 Bench status. The
available research set has no `0x247` TX result, and its source vehicle/profile
must not be treated as proof for another model or software release.

Implemented read-only research checkpoint:

- `scripts/analyze_hands_on_correlation.py` parses a file or directory of
  candump logs and reports co-presence, DLC, nearest-frame timing, and bit
  co-variation for `0x247`/`0x3E9` without assigning unsupported semantics.
- Output is derived-only: source SHA-256, counts, timing, and contingency tables;
  it contains no raw payload, absolute source path, mutation path, or TX
  capability (`tx_capability=false`).
- The public `sqladm1n` research revision
  `ccc62a1ecc8354520c070da6dfa7fea905211779` was replayed across all 78 logs:
  1,316,711 frames parsed, zero parse errors, 4,569 `0x247` frames, 1,940
  `0x3E9` frames, and both IDs present in 39 files. A 100 ms window produced 940
  descriptive nearest-time pairs.
- Two complete runs produced identical report digest
  `6c948b18b319f0fcc4c36f0e6409129d2be014152407e9e9ee85e12bc031c4c4`.
- Focused tests passed 4/4. The available evidence still does not establish a
  `0x3E9` nag/satisfied/inactive semantic or any `0x247` TX behavior, so those
  claims remain unknown instead of being coerced into a decoder.

### P1.3 `0x229` validator

Add read-only handling for the observed DLC-3 frames:

- frame-shape and cadence validation;
- counter/checksum candidate analysis;
- state-transition timeline;
- profile/vehicle confidence label;
- explicit separation between an observed state frame and a valid command.

Do not add a physical `0x229` write merely because parsing succeeds.

Implemented read-only checkpoint:

- `TelemetryState` accepts Vehicle-bus `0x229` only at exact DLC 3 and exposes
  observed CRC byte, 4-bit counter, right-stalk state, and park-button state;
  freshness expiry is applied like every other telemetry sample.
- `decoder_registry.h` now distinguishes `Vehicle` from Chassis/Party and is
  the single metadata source for the four `0x229` fields.
- `scripts/validate_right_stalk.py` validates shape, modulo-16 counter steps,
  state transitions, and `(bytes1..2) -> observed CRC` consistency without raw
  payload disclosure or any TX capability.
- Full 78-log replay parsed 1,316,711 frames with zero parser errors and found
  2,328 `0x229` frames, all exact DLC 3. All counter values were observed;
  stalk state 3 appeared once, park remained 0, reserved bits remained 0, and
  17 observed bodies had no conflicting CRC byte.
- The upstream `00 00 01` Park builder appeared zero times. The available
  capture does not exercise Park, and the DBC identifies the CRC field but not
  its algorithm; command validity therefore remains `unknown` and Park TX stays
  absent.
- Focused Python tests, the 10-case native telemetry suite, the standalone
  decoder-registry assertion, and the `lilygo_t2can` build pass.

Acceptance status: **complete for read-only validation; TX remains explicitly
ineligible because checksum/counter command evidence is absent.**

### P1.4 Disabled `.cantest` and rule-draft workflow

Support a reviewed test-profile format with this path:

```text
parse -> validate -> dry-run -> policy simulation -> explicit arm -> execute
```

Initial implementation stops at dry-run. A profile must declare feature,
target bus, ID, DLC, mux, cadence, counter/checksum policy, state prerequisites,
maximum duration, and abort conditions.

Automatic rule generation may create only:

- `send:false`;
- disabled installation state;
- evidence links and confidence;
- generated native fixtures;
- a human-readable mutation diff;
- no automatic layout selection or target-bus inference.

Promotion to an enabled rule is a separate reviewed action.

Implemented P1 boundary:

- `scripts/cantest_dry_run.py` accepts strict JSON `.cantest` profiles and runs
  only `parse -> validate -> dry_run -> policy_simulation`.
- Valid profiles must declare exact bus, vehicle profile, layout, ID, DLC, mux,
  cadence, counter/checksum policy, prerequisites, bounded duration, aborts,
  evidence, and a human-readable mutation diff. Unknown keys fail validation.
- `enabled`, `installed`, and `send` must all be `false`; there is no CAN,
  socket, serial, subprocess, scheduler, arm, install, or execute integration.
- `0x229` is an explicit semantic deny (`park_button_tx_prohibited`). The
  checked-in profile contains no payload or mutation and remains ineligible.
- CLI read-back reports `physical_attempts=0`, `execution_supported=false`, and
  `eligible=false`. Focused contract tests pass 5/5.

Acceptance status: **complete through policy simulation only. Physical Bench
execution remains a separate P2 implementation and review.**

### P1.5 Listen-only bitrate recommendation

For each physical bus, probe candidate rates only in hardware listen-only mode.
Score them from valid frame count, standard-ID/DLC plausibility, controller error
behavior, and stability over a bounded window.

The result is a recommendation:

```text
candidate rate + confidence + evidence + alternatives
```

It must not change the Active TX bitrate automatically. The user confirms the
rate, after which the Active session locks it. Loss of confidence blocks TX
rather than silently switching rates.

Implemented host-side recommendation core:

- `include/can_bitrate_recommendation.h` scores fixed candidates at 125/250/500/
  1000 kbps from valid frames, implausible frames, controller errors, timestamp
  regressions, and bounded-window stability.
- Evidence not collected in hardware listen-only mode is invalid regardless of
  frame count. Results retain ranked alternatives and Low/Medium/High confidence.
- Medium/High confidence permits only a later confirmation step. The result
  always declares `requiresExplicitConfirmation=true` and
  `mayApplyAutomatically=false`; no driver reconfiguration API was added.
- Host assertions and the `lilygo_t2can` build pass.

Acceptance status: **recommendation/scoring core complete; physical CAN A/B
listen-only collection and recommendation read-back require the later focused
vehicle session. No bitrate has been changed.**

### P1.6 Layout recommendation, not automatic layout mutation

Observed frames may produce a ranked layout suggestion. The UI must show the
supporting IDs, DLCs, freshness, conflicts, and confidence. The effective layout
changes only after explicit confirmation and is recorded as a configuration
event. A capture, reboot, or firmware update never changes it automatically.

Implemented recommendation path:

- `include/chassis/layout_recommendation.h` tracks valid/rejected `0x399` and
  `0x39B` observations, freshness, and conflicting simultaneous evidence.
- Legacy-only fresh evidence can recommend Legacy HW3. `0x39B` without an
  explicit profile remains `Unknown` with Standard HW4 and Highland as ranked
  alternatives because the frame ID cannot prove the byte layout.
- `/status.layoutRecommendation` exposes candidate, confidence, ambiguity,
  evidence counts/freshness, alternatives, `requiresConfirmation=true`, and
  `automaticMutation=false`.
- The recommender has no `setLayout` or configuration-write path. Host tracker
  assertions and the `lilygo_t2can` firmware build pass.

Acceptance status: **code path complete; live `/status` read-back with vehicle
frames is reserved for the final focused device/vehicle session.**

### P1.7 BLE device-owner provisioning

Security precondition completed:

- BLE LE Secure Connections/bonding is treated only as transport security, not
  as device-owner authorization.
- The former arbitrary `send` parser and physical transmission path were
  removed; `send` is explicitly unsupported until a policy-checked `TxIntent`
  path exists.
- BLE `config` writes, `inject`, and `wifi_mode` changes fail closed pending
  owner authorization; read-only diagnostics remain available.

BLE provisioning authenticates control of this ESP32 device; it is not Tesla
vehicle-key enrollment.

Requirements:

- physical-presence or bounded first-enrollment window;
- challenge-response rather than transmitting a reusable plaintext secret;
- owner-key fingerprint, revocation, and replacement flow;
- encrypted/local secret storage where platform support permits;
- separate permissions for diagnostics, OTA authorization, and CAN arm;
- no Tesla drive key, passive-entry key, or private owner key in logs or exports;
- anti-replay request ID, expiry, and deduplication;
- read-back of the applied permission state without returning secret material.

Implemented device-owner path:

- `include/ble/owner_authorization.h` binds authorization to the identity of an
  encrypted, authenticated, bonded LE Secure Connections peer. No reusable app
  secret or private owner key is accepted by the command protocol.
- A fresh device exposes one five-minute enrollment window. Enrollment defaults
  to diagnostics plus admin; OTA and CAN-arm permissions remain separate and
  disabled until an authorized update.
- Owner mutations require a non-zero request ID and a device-uptime issuance and
  expiry window of at most 30 seconds. A bounded replay cache rejects duplicate
  request IDs.
- Owner identity, permission mask, and generation are committed as one compact
  NVS record and read back before the runtime state changes. Invalid storage is
  fail-closed and never reopens enrollment.
- `owner_status` returns schema, enrollment state, non-secret fingerprint,
  generation, and permission booleans. It never returns the bonded address or
  BLE key material.
- `owner_revoke` writes a closed tombstone. `owner_replace_begin` is a distinct
  owner-authorized operation that opens a new five-minute window; a reboot
  during replacement closes that window rather than allowing takeover.
- Granting CAN-arm permission does not enable injection. Raw BLE send, config
  writes, injection, and mode changes remain blocked until the common
  `TxIntent` path exists.

Verification completed: owner-core native assertions cover enrollment, insecure
and wrong peers, permissions, request expiry, replay, wraparound, persistence
restore, revocation, and replacement. The BLE-enabled `lilygo_t2can` firmware
build passes. BLE pairing/NVS read-back on the physical unit is reserved for the
final focused S20/device session.

### P1.8 Private Web remote CAN control

The web UI remains local/private and must not expose the ESP32 directly to the
public Internet. The preferred remote path is S26 as the authenticated field
console/bridge to the private Mac environment.

Web/BLE commands create `TxIntent` objects and receive an explicit result. They
cannot write a frame directly, extend an expired lease implicitly, or convert a
diagnostic session into OTA/TX authority. Public inbound port forwarding is not
part of the plan.

Implemented control-plane foundation:

- `include/tx/tx_intent.h` defines the transport-neutral intent, policy context,
  explicit block reasons, and bounded admission state without owning or calling
  a CAN driver.
- Intents carry source/feature IDs, monotonic per-source request ID, boot-session
  nonce, issuance/expiry, semantic and physical buses, frame/DLC/mux constraints,
  cadence/burst/cooldown, verified counter/checksum strategy, freshness/state
  requirements, and required operation session.
- Admission fails closed on reboot nonce mismatch, replay, expiry, maintenance or
  OTA inhibit, wrong session, missing authorization/control channel, stale state,
  unhealthy bus, invalid routing/frame shape, unknown integrity strategy,
  unisolated Bench operation, rate limits, or capacity exhaustion.
- `0x229` is an explicit semantic deny. A passing admission result still reports
  `physicalAttempt=false`; a later scheduler/driver adapter must separately
  record the actual attempt and result.
- BLE owner mutations now include the current owner generation, preventing a
  captured pre-reboot request from being accepted after permission or owner
  generation changes.
- No remote TX endpoint is enabled by this checkpoint. Existing raw BLE send and
  state-changing BLE commands remain blocked; the web control plane remains
  local/private.

Focused native assertions cover allow/block ordering, nonce/replay/expiry,
state/session/authorization gates, `0x229`, integrity requirements,
Bench isolation, rate limits, reset, and fail-closed capacity. Migrating the
existing built-in/plugin TX producers through this admission boundary remains
the next structural step.

### P1.9 Anomaly and evidence bundle

Add local-only detection for:

- missing or newly appearing IDs;
- period and jitter changes;
- DLC changes;
- frame-rate bursts;
- counter discontinuities;
- per-bus asymmetry;
- RX stall/recovery;
- TX echo/overwrite differences.

An anomaly creates evidence and may block a policy gate, but it does not generate
an enabled TX rule or change layout/bitrate by itself.

Implemented anomaly evidence path:

- `include/diagnostics/can_anomaly_tracker.h` keeps a bounded 32-entry,
  payload-free RX baseline keyed by CAN ID and physical bus.
- It counts newly appearing IDs after warm-up, DLC changes, period shifts,
  sub-2ms bursts, per-bus RX stall/recovery, one-sided bus presence, bounded
  capacity exhaustion, and explicit TX/echo mismatches supplied by a future
  matcher.
- DLC changes, RX stalls, echo mismatches, and capacity exhaustion expose a
  sticky `policyBlock`; anomalies never generate a TX rule or mutate layout or
  bitrate. The sticky block is wired into the final driver `allowSendFrame`
  callback, so all built-in and plugin sends are denied before a physical
  attempt when anomaly policy is closed.
- The live RX path feeds the tracker and `/status` exposes compact schema
  `t2can-can-anomaly-v1`, counters, last ID/physical bus, and policy-block state
  without payload bytes.
- The TX/echo comparison API is host-tested but is intentionally not fed fake
  evidence: firmware integration waits for a real bounded echo matcher.

Focused native assertions cover DLC/period/burst, stall/recovery, bus asymmetry,
new IDs, exact/mismatched/missing echo, capacity, and payload-free summary. The
BLE-enabled `lilygo_t2can` firmware build passes. Live status read-back is
reserved for the final focused device session.

## P2 — active feature qualification

P2 promotes one feature at a time. Each module needs its own decoder, fixtures,
`would-send` output, scheduler policy, isolated bench result, and rollback/abort
behavior.

### P2.1 Existing Nag TX hardening

- Route all modes through the common scheduler and audit path.
- Replay current synthetic vectors and any future private `0x370` capture.
- Verify counter/checksum, bounds, cadence, stale-state blocks, mode changes,
  cancellation, and send failure.
- Preserve current fail-closed behavior for unsupported mode/profile
  combinations.
- Do not treat the `sqladm1n` dataset as Nag TX validation because it contains
  no `0x370` frames.

### P2.2 `0x247` Bench candidate

Promotion prerequisites:

1. P1 correlation passes across the complete research set;
2. frame layout discrepancies are resolved and documented;
3. dry-run output is deterministic;
4. the scheduler enforces feature-specific duration/rate/abort constraints;
5. an isolated bench and second logger verify exact output and stop behavior;
6. the feature remains disabled by default and unavailable outside its declared
   profile.

A community RX capture and a passing host test are not enough to claim working
vehicle behavior.

### P2.3 Other selected TX modules

Evaluate individually rather than importing the whole fork:

- body-control candidates such as lighting/wiper/stalk features;
- selected checksummed configuration requests;
- existing FSD/AP frame modifications;
- reviewed plugin equivalents.

Higher-impact feature classes require stronger evidence and cannot inherit a
lower-risk module's approval.

## P3 — deferred high-impact research

### P3.1 ULC unlock

Keep as an explicit research item, not a promised feature. Required before any
Bench TX status:

- authoritative or independently corroborated message semantics;
- complete counter/checksum/session behavior;
- exact target profile and software-version evidence;
- offline replay and negative cases;
- isolated bench result;
- identified physical consequence, abort, and recovery path.

### P3.2 Supervised Summon session

Do not implement unattended, scheduled, or cloud-autonomous vehicle motion.
The acceptable target is a supervised session with:

- deliberate user initiation and continuous/dead-man authorization;
- short command expiry and deduplication;
- fresh, consistent gear, speed, AP, Summon, bus-health, and control-channel
  state;
- immediate abort on user release, state conflict, stale input, disconnect,
  vehicle OTA, or bus quarantine;
- bounded command set and duration;
- independent physical disconnect and continuous evidence recording.

The existing Summon-only policy is a starting predicate, not proof that complete
remote Summon control is safe or compatible.

## Verification ladder

Every active feature advances through these levels independently:

1. **Source review** — exact repo/ref, license, handler, message assumptions, and
   known limitations.
2. **Pure unit test** — bit packing, DLC/mux rejection, counter/checksum, timing,
   wraparound, and state transitions.
3. **Community-data replay** — deterministic read-only or `would-send` result;
   no physical driver.
4. **Private target capture replay** — target-specific evidence with raw data kept
   local.
5. **Internal controller loopback** — exact bytes and queue behavior.
6. **Isolated transceiver bench** — second logger, bounded TX, stop/cleanup, and
   fault injection.
7. **Disconnected-vehicle or controlled stationary evaluation** — only after a
   separate explicit approval and recovery plan.
8. **Vehicle validation** — exact software/profile evidence and read-back; never
   inferred from a build or dashboard toggle.

No public-road injection procedure is part of this roadmap.

## Test matrix

| Layer | Required checks |
| --- | --- |
| Parser | candump variants, blank/comment lines, malformed payload, DLC mismatch, timestamp/order handling |
| Decoder | bus, ID, DLC, mux, scaling, signedness, unavailable values, expiry, profile conflict |
| Replay | deterministic state and `would-send`, no physical driver, drop/error accounting |
| TX policy | missing/stale/contradictory state, arm expiry, mode change, disconnect, OTA, quarantine, abort |
| Scheduler | rate/burst/cooldown, counter/checksum, cancellation, ambiguous result, per-bus attribution |
| Self-test | MCP2515/TWAI internal loopback, queue pressure, one-bus failure, cleanup |
| OTA | authorization, manifest/board mismatch, interrupted transfer, bad digest/signature, failed boot, rollback |
| Control plane | BLE/Web auth, replayed request, duplicate request, permission separation, secret redaction |
| Recorder | RX/TX decisions, health transitions, OTA lifecycle, self-test, storage pressure, ACK/read-back |
| Privacy | raw-log Git scan, credential scan, sanitized fixture provenance, bounded cloud summary |

## Stop conditions

Stop promotion of a feature when any of these occurs:

- frame layout, target bus, counter, checksum, cadence, or profile remains
  ambiguous;
- a community claim conflicts with the observed dataset;
- source tests pass but replay cannot reproduce the claimed state transition;
- the driver or control path can bypass policy/audit;
- a blocked action still produces a physical attempt;
- OTA cannot prove board identity, boot health, and rollback;
- internal loopback or bench cleanup leaves pending TX;
- required safety input is missing, stale, invalid, or contradictory;
- the only proposed fix is widening filters, removing a gate, increasing a rate,
  or retrying an uncertain state-changing command;
- raw captures or credentials would need to be committed or sent to a cloud
  service to continue.

## Planned deliverables

The implementation should be split into reviewable milestones rather than one
large merge. Expected artifacts include:

- hardware self-test module and Bench UI/API state;
- candump replay adapter and local research-set runner;
- decoder registry and confidence metadata;
- central TX intent/policy/scheduler/audit modules;
- per-bus health, RX-stall, and quarantine state;
- device OTA transaction, boot confirmation, and rollback;
- firmware capability manifest;
- BLE owner authorization and private Web control integration;
- disabled `.cantest`/rule-draft workflow;
- sanitized fixtures plus provenance records;
- updated dashboard, Support, recorder, maintenance, build, and safety docs.

Each milestone must keep existing tests green, add a narrow regression test, run
the exact T-2CAN firmware build, and report what remains unverified on physical
hardware or a vehicle.

## Current implementation boundary

This roadmap does not mean the planned features are already shipped. Current
implementation documentation remains authoritative until a milestone is built,
tested, and read back. In particular:

- device OTA signing/boot confirmation/rollback is still planned work;
- hardware internal loopback on the actual T-2CAN has not been run;
- `sqladm1n` data has been parsed and summarized locally but is not yet connected
  to the complete firmware replay pipeline;
- no live `0x247`, ULC, or automatic/Supervised Summon TX validation has occurred;
- no vehicle OTA or physical CAN transmission was performed while creating this
  document.
