# Vehicle ESP32 field maintenance

This document records the longer-term way to maintain an ESP32 installed in a vehicle without carrying a laptop. The first read-only collection slice is now implemented; vehicle OTA, remote build service, rescue AP, and automatic CAN control remain out of scope.

## Intended topology

- **ESP32 in the vehicle:** runs the existing CAN firmware and exposes only local diagnostics when a trusted local link is available.
- **S26 phone:** acts as the field console and local bridge. It connects to the ESP32 over the vehicle's local Wi-Fi/BLE path and forwards approved diagnostic artifacts over the private network.
- **M4 Mac mini:** remains the private build, test, artifact-signing, and analysis host. It is not an Internet-facing vehicle endpoint.
- **Hermes/Telegram:** carries instructions, snapshots, and explicit approval prompts. Credentials and firmware secrets must not be placed in chat.

The ESP32 must not be directly exposed to the public Internet. A private outbound connection from the phone or an approved private edge is preferred over inbound port forwarding.

## Implemented read-only collection slice

- The authenticated BLE command `{"cmd":"snapshot"}` returns the versioned schema `t2can-maintenance-snapshot-v1`.
- The BLE snapshot contains firmware identity, runtime counters, CAN driver health (including CAN A/B when the dual driver is present), receive-side telemetry, and current configuration. It does not arm injection, change configuration, or send CAN frames.
- On the Mac mini, `scripts/collect_vehicle_snapshot.py` pulls only `GET /status`, `GET /diagnostics_detail`, and `GET /gvret/status` from a locally reachable ESP32 and writes one atomic, mode-0600 JSON artifact.
- The collector refuses public hosts by default, does not accept credentials in the URL, and can optionally add the human-readable `/support` report with SSID, IP address, and MAC address redacted.
- This is a pull-to-Mac path, not an Internet upload service. The S26 BLE bridge still needs a phone-side implementation; until then the Mac must be able to reach the ESP32's private/local address.

Example on the Mac mini:

```bash
python3 scripts/collect_vehicle_snapshot.py \
  http://192.168.4.1 \
  --output-dir ~/t2can-snapshots \
  --include-support
```

The resulting artifact is evidence for analysis and comparison only. It must not be used to infer a DAS layout or automatically generate a transmit rule.

## Normal maintenance flow

1. Park the vehicle, make it electrically safe, and leave CAN transmission disabled.
2. Pair the S26 with the local ESP32 interface using an authenticated, encrypted channel.
3. Collect a bounded diagnostic snapshot: firmware identity, board health, CAN A/B readiness, bitrate, error counters, RX freshness, and recent receive-only telemetry.
4. Upload the snapshot to the private Mac mini for comparison with the known-good baseline.
5. Reproduce and test the change on the Mac mini. Run native tests, Python tests, and the exact firmware build before creating an artifact.
6. Review the diff and artifact hash. Keep a previous known-good artifact available.
7. Require a separate, explicit approval immediately before any vehicle OTA. Diagnostic read-only access must not imply OTA permission.
8. Transfer the signed artifact through the private phone bridge, verify its hash on the ESP32, and install only while the vehicle is parked.
9. Let the boot self-test complete. Read back firmware identity, health, CAN readiness, and serial/diagnostic output before declaring success.
10. If the self-test or health checks fail, roll back to the previous partition and preserve the failure snapshot.

## OTA and rollback requirements

A future implementation must provide all of these before vehicle OTA is enabled:

- signed artifacts with an independently reported hash;
- HTTPS or an equivalent authenticated private transport;
- ESP32 dual OTA partitions and a boot-validity marker;
- post-boot self-test with a bounded confirmation window;
- automatic rollback when the new image does not confirm healthy;
- explicit version and board/profile compatibility checks;
- no automatic OTA triggered by a diagnostic message;
- an offline rescue path using a local ESP32 access point and the S26, without public exposure.

The Mac mini should retain the last known-good artifact and its test evidence. The phone may cache a bounded, encrypted transfer queue when the private network is unavailable, but it must not silently apply queued firmware.

## Safety boundaries

- CAN TX remains a separate permission from diagnostics and OTA.
- OTA approval is valid only for one named artifact, one target board, and one maintenance session.
- No update is allowed while driving or when the vehicle's parked/safe condition cannot be confirmed.
- A failed diagnostic read must fail closed; it must not enable injection or infer a DAS layout.
- Signal-map selection remains explicit. Observed frames must never auto-switch the DAS layout.
- Logs and snapshots must exclude passwords, tokens, private keys, and other credentials.

## Implementation order when this becomes active work

1. ~~Define the authenticated S26-to-ESP32 local protocol and bounded diagnostic schema.~~ Implemented for the BLE command envelope and `t2can-maintenance-snapshot-v1`.
2. ~~Add a read-only snapshot export and verify it against the existing dashboard state.~~ Implemented through the BLE snapshot and Mac-side read-only collector.
3. Add Mac mini artifact staging and signature verification without OTA writes. This means firmware-artifact handling, not automatic application to a vehicle.
4. Add a dry-run OTA transaction that never boots the artifact.
5. Add dual-partition OTA, self-test, rollback, and read-back verification.
6. Only after bench and disconnected-vehicle testing, consider a manually approved in-vehicle update.

The read-only collection slice above is the current implementation boundary. OTA, artifact signing, and any vehicle-side firmware write remain future work until their safety gates are implemented and separately approved.
