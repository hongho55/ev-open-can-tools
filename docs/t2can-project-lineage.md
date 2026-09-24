# T-2CAN project lineage and source roles

This repository contains the current shared-firmware work. The similarly named repositories below are related, but they are not interchangeable branches or authoritative for the same purpose.

## Canonical roles

- [`anoblekman/t2can-roaming`](https://github.com/anoblekman/t2can-roaming) — **original T-2CAN baseline used by our earlier implementation**. It is the historical reference for the LilyGo T-2CAN adaptation, fixed signal definitions, host logic, and the original safe CAN shutdown behavior.
- [`hypery11/flipper-tesla-fsd`](https://github.com/hypery11/flipper-tesla-fsd) — **current Flipper upstream/reference**. Review new protocol and safety fixes here, but port only source-verified behavior that fits the T-2CAN architecture. Do not merge Flipper hardware/UI code wholesale.
- [`ev-open-can-tools/ev-open-can-tools`](https://github.com/ev-open-can-tools/ev-open-can-tools) — **public ESP32 firmware upstream** from which this repository is forked. It supplies the broader board, dashboard, plugin, and driver architecture.
- `feat/t2can-dual` (local worktree historically named `t2can-bobby`) — **completed experimental integration branch**. “Bobby” is only a local workspace nickname; it is not the original T-2CAN baseline and must not be cited as upstream.
- `private/shared-fw-minimal` — **current integration branch, product name `T2CAN Sentinel`**. This is where selected T-2CAN and Flipper-derived behavior is adapted to the shared firmware's common telemetry, TX-policy, dual-driver, OTA, and dashboard paths.

## Authority order

For a disputed signal or safety behavior, use evidence in this order:

1. target-vehicle listen-only capture and exact hardware/bus provenance;
2. maintained same-generation DBC or safety implementation such as the HW4 Party-CAN definitions in commaai/opendbc;
3. exact source in `t2can-roaming` and the current Flipper release;
4. this repository's host tests and offline fixtures;
5. nearby forks, README claims, or community reports for discovery only.

A passing host build is not vehicle validation. No change described here authorizes public-road testing or bypasses the required listen-only capture, offline replay, controlled bench test, rollback, and explicit flash approval stages.

## Porting rule

Keep each imported fix small and traceable:

- record the source repository and exact behavior being adapted;
- preserve the shared firmware's single TX-policy boundary;
- fail closed when required vehicle state is missing or stale;
- test the decoder/gate on the host and build the exact LilyGo target;
- do not flash a vehicle from a documentation or comparison change alone.
