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

Default OFF; not persisted. Start recording in the status tab. Automatic triggers
are the first observed DAS abort code 8/9 in a transition, or an increase in the
exposed CAN diagnostic error counters or a ready-to-offline transition
(checked every 250 ms). The latter is a
**CAN-error event**, not proof of bus-off. Manual marking is also available.

A fixed RAM ring retains up to 192 pre-event and 64 post-event key RX frames.
Post-event collection ends after 64 key frames or 2 seconds, whichever comes
first. Pre-event duration depends on bus rate; it is not a guaranteed time
window. Only one incident is retained; explicitly start/clear to rearm. Download
is available once frozen. Concurrent clearing cannot mix incidents in a download.

The candump log uses uptime-relative timestamps and `can0` = Party/CAN A,
`can1` = CH/VEH/CAN B. These are RX observations only; this recorder does not
label bus-observed echoes as transmitted frames. No replay, automatic filesystem
writes, or flash persistence is included. RAM records disappear on reboot.
The status endpoint reports event reason/count/trigger uptime separately.

Endpoints: GET `/diagnostics_detail`, POST `/event_control` with form `action`
(`enable`, `disable`, `clear`, `mark`), GET `/event_download`.

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
