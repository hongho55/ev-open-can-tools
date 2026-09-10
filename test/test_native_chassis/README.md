Receive-only Chassis tests use the existing Unity native test style. No PlatformIO
runner or firmware build is needed. Point `UNITY_SRC` at any existing upstream
Unity `src` directory containing unity.c, unity.h, and unity_internals.h:

```sh
build_dir=$(mktemp -d)
cc -std=c99 -I"$UNITY_SRC" -c "$UNITY_SRC/unity.c" -o "$build_dir/unity.o"
c++ -std=c++17 -Wall -Wextra -Werror -Iinclude -I"$UNITY_SRC" \
  test/test_native_chassis/test_chassis.cpp "$build_dir/unity.o" \
  -o "$build_dir/chassis_tests"
"$build_dir/chassis_tests"
```

Only selected-layout DAS presence and raw AP-state reception are implemented.
Flipper evidence: fsd_logic/fsd_capability.h (0x399 ambiguity, 0x39B presence),
esp32/.firmware/can_signals.h (AP byte/mask/shift), and the corresponding
fsd_handler.cpp DAS parsers (DLC exactly 8). fsd_logic/fsd_state.h and
fsd_logic/fsd_events.h disagree about the engagement threshold; no engagement
boolean or transition events are ported. No guessed CRC, counter, enum validity,
layout auto-detection, steering or other signal parsing is included.

Select a confirmed Legacy/HW3 0x399 or standard HW4 0x39B layout and an explicit
freshness timeout. Defaults report no capability/state. Fresh means a matching
frame was recently received, not verified vehicle state. Timeout is caller policy.
CAN B, CH and VEH labels are accepted under existing EV semantics; labels containing
Party, CAN A or unknown bits are rejected. This module is standalone and is not
wired into firmware, drivers, handlers, or any transmit path.

Safety gates (standalone; no combined helper)
-------------------------------------------
`include/chassis/safety_gates.h` adds AP-first, Abort Guard and Soft Engage only.
Compile the separate test executable with the same Unity object/path as above:

```sh
UNITY_SRC="$PWD/.pio/libdeps/native/Unity/src"
build_dir=$(mktemp -d)
cc -std=c99 -I"$UNITY_SRC" -c "$UNITY_SRC/unity.c" -o "$build_dir/unity.o"
c++ -std=c++17 -Wall -Wextra -Werror -Iinclude -I"$UNITY_SRC" \
  test/test_native_chassis/test_safety_gates.cpp "$build_dir/unity.o" \
  -o "$build_dir/safety_tests"
"$build_dir/safety_tests"
python3 -m unittest discover -s test -p 'test_*.py'
git diff --check
```

Evidence inspected in the read-only `/Users/hong/flipper-tesla-fsd` checkout:
- `fsd_logic/fsd_handler.c:154-207` and
  `esp32/.firmware/fsd_handler.cpp:99-114,757-781`: disabled bypass, AP-first
  edge/minimal delay bypass, 1000 ms unsigned elapsed, inclusive +/-5 degree
  soft latch, abort states 8/9, and latch preservation while disabled.
- `fsd_logic/fsd_handler.h:59-104` and
  `esp32/.firmware/fsd_handler.h:37-93`: constants and conflicting AP semantics.
  **Both current AP-first implementations require >=3**, not >=2; the ESP32
  header's AP-first prose still says >=2. Flipper's AP-first and main-loop
  reset threshold is <3; the ESP32 abort helper itself still uses <2, which is
  inconsistent with its main-loop reset at <3. This slice follows the safer
  current AP-first/main-loop behavior: raw 2 is not treated as engaged and
  abort latches re-arm below 3. Raw state is never permission to control a
  vehicle.
- `esp32/.firmware/main.cpp:1061-1080,1163-1182,1295-1297`: configurable parsing
  precedes timestamp/reset/abort maintenance; ordinary DAS parsing comes later
  and returns, so maintenance can see the previous raw state. Main resets soft
  latch and unstable time at <3. Combined source evaluation short-circuits in
  AP-first, Soft Engage, Abort Guard order (soft can latch even if abort denies).
  No dispatcher timing or combined helper is ported: callers must update Abort
  Guard before querying it and explicitly reset Soft Engage on AP drop/session
  restart. The soft reset threshold remains an integration decision.

All gates default enabled; latches default clear. Enabled AP-first rejects raw
0/1; enabled Soft Engage rejects its default sample. Abort Guard is a historical
abort predicate, so initially allows. Explicitly disabled gates always allow;
that result is only a predicate bypass. AP freshness/layout validation belongs
at the caller, independently of these gates. No signal IDs/layouts, 0x370 parsing,
capability changes, CanFrame/FSDState/CanDriver dependency, TX, or production
wiring are added.

Soft Engage deliberately differs from Flipper's documented missing-steering
fallback to zero: an unlatched gate rejects absent/stale samples and non-finite
angles. The caller must supply degrees and a true `freshAndPresent` only with
confirmed current data; no freshness timeout or steering layout is invented.
After latching, missing/turning samples allow until explicit reset, matching
source latch persistence. Gate toggles do not reset either latch.

AP-first uses caller-initialized/stamped timestamps from one monotonic uint32_t
millisecond clock. Initialize at session start, stamp while raw AP <3, and
reinitialize after clock restart or a full 2^32 ms gap; elapsed must remain below
one full wrap and timestamps cannot be in the future. Tests cover raw threshold,
999/1000/1001 ms including wrap, each override, abort clear/re-arm and disabled
persistence, soft +/-5 boundaries, missing/stale/non-finite data, and reset.
