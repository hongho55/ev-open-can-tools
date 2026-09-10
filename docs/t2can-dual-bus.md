# LILYGO T-2CAN dual-bus wiring

This build keeps both physical CAN controllers available at **500 kbit/s**:

| Logical bus | Controller | Wiring |
| --- | --- | --- |
| CAN A / Party | onboard MCP2515, 16 MHz | CS GPIO10, INT GPIO8, RESET GPIO9, SPI SCK GPIO12, MISO GPIO13, MOSI GPIO11 |
| CAN B / Vehicle + CH | ESP32-S3 native TWAI | TX GPIO7, RX GPIO6 |

The frame model retains the existing semantic masks: `CAN_BUS_PARTY` routes to
CAN A, while `CAN_BUS_VEH` and `CAN_BUS_CH` route to CAN B. Explicit physical
selectors are `CAN_BUS_CAN_A` and `CAN_BUS_CAN_B`; plugin JSON accepts `CAN_A`
and `CAN_B` (including an array or `|`-separated combination). An explicit
combined mask may target both. `CAN_BUS_ANY` is deliberately fail-closed to CAN
A only. RX frames carry the physical selector plus their semantic mapping, and
no automatic bridge or forwarding is enabled.

GVRET reports two 500 kbit/s buses. Its existing bus nibble identifies CAN A as
0 and CAN B as 1; bus 0 behavior remains unchanged.

**Safety boundary:** this is a host-tested/static integration slice only. It does
not imply vehicle, road, transceiver, electrical, or interoperability validation.
Do not connect, flash, or test it on a vehicle as part of this change.
