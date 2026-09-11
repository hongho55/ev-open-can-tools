# Read-only diagnostic details and event capture

The status tab now includes pack voltage/current, display SOC, battery min/max
 temperature, raw DAS lane-change/side-collision/forward-collision warning codes,
 and the vision speed limit. Warning fields are **raw enums**, not guessed boolean
 alerts. Values are observations, not permissions to engage or transmit.

## Signal support

- Party / physical CAN A: `0x132` voltage (u16 LE, 0.01 V), signed current
  (i16 LE, 0.1 A); `0x292` SOC (bit 10, 10 bits, 0.1%); `0x312` min/max
  temperature (bytes 4/5 minus 40°C).
- CH/VEH / physical CAN B: selected Legacy/HW3 `0x399` or standard HW4 `0x39B`
  layout. Lane-change bits 46–50, side warning bits 32–33, FCW bits 22–23,
  vision limit bits 16–20 (5 km/h; 0/31 unavailable).
- Layout auto-detection and Highland byte-0 fallback are not added.
- Frames must meet each decoder's bus and DLC requirements. Invalid SOC (>100%),
  unavailable thermal bytes, and inverted temperature ranges are rejected.
- Each signal expires independently after 1500 ms. Other incoming frames cannot
  keep an old BMS or DAS value fresh. The UI polls every 3 seconds and clears the
  detailed values when its request fails.
- Hexadecimal IDs in dashboard handler filters were corrected. This change does
  not alter non-dashboard filters or add any transmit operation.

## Event recorder

Armed by default. Automatic triggers cover normal AP disengagement, DAS abort
codes 8/9, CAN error-counter increases, and receive-liveness loss; manual marking
is also available. Recording freezes exactly 10 seconds after the trigger and is
persisted as generation-specific JSONL in SPIFFS.

Separate bounded raw RX/TX and state/decision rings preserve pre-trigger and
post-trigger partitions. The N16R8 target uses PSRAM when its boot probe passes
and otherwise uses a larger internal-RAM fallback. Capacity, measured coverage,
and drop counters are reported by the status endpoints and are authoritative.
See `docs/vehicle-flight-recorder.md` for the current schema and preservation
details.

Endpoints: GET `/diagnostics_detail`, POST `/event_control` with form `action`
(`enable`, `disable`, `clear`, `mark`), authenticated GET `/event_list`,
authenticated GET `/event_download?id=<sequence>-<slot>`, and authenticated
POST `/event_ack` with `id`, `size`, and `sha256` form fields.

## Source and verification

Signal definitions were compared with
[Flipper ESP32 parsers](https://github.com/hypery11/flipper-tesla-fsd/blob/a6b362c96269de712263b4025450a271f901ba74/esp32/.firmware/fsd_handler.cpp),
[signal constants](https://github.com/hypery11/flipper-tesla-fsd/blob/a6b362c96269de712263b4025450a271f901ba74/esp32/.firmware/can_signals.h),
and the local Flipper `fsd_logic/fsd_handler.c` DAS field definitions.
The event recorder is a new bounded implementation, inspired by the upstream
[blackbox design](https://github.com/hypery11/flipper-tesla-fsd/blob/a6b362c96269de712263b4025450a271f901ba74/esp32/.firmware/blackbox.h).

Run `python3 -m unittest discover -s test -p 'test_*.py'`. This includes native
C++ fixtures for signed current, SOC bit packing, temperature, DLC/bus rejection,
independent expiry, clock rollover, ring wrap, freeze, and rearming. Hardware
validation with the actual vehicle's frames remains separate from build/tests.
