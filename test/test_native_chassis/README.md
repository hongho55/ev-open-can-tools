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
