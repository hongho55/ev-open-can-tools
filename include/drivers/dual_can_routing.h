#pragma once

#include <cstdint>

#include "../can_frame_types.h"

namespace DualCanRouting
{
struct Targets
{
    bool canA = false;
    bool canB = false;
};

// CAN A is the onboard MCP2515/Party CAN path. CAN B is the native TWAI path
// and retains both existing semantic vehicle/channel selectors. ANY is kept
// deliberately single-target so an underspecified injection cannot fan out.
inline constexpr Targets targetsForBus(uint8_t bus)
{
    if (bus == CAN_BUS_ANY)
        return {true, false};

    constexpr uint8_t known = CAN_BUS_CH | CAN_BUS_VEH | CAN_BUS_PARTY |
                              CAN_BUS_CAN_A | CAN_BUS_CAN_B;
    if ((bus & ~known) != 0)
        return {};

    const bool canA = (bus & (CAN_BUS_CAN_A | CAN_BUS_PARTY)) != 0;
    const bool canB = (bus & (CAN_BUS_CAN_B | CAN_BUS_CH | CAN_BUS_VEH)) != 0;
    return {canA, canB};
}

inline constexpr uint8_t canABusLabel()
{
    return CAN_BUS_CAN_A | CAN_BUS_PARTY;
}

inline constexpr uint8_t canBBusLabel()
{
    return CAN_BUS_CAN_B | CAN_BUS_CH | CAN_BUS_VEH;
}
} // namespace DualCanRouting
