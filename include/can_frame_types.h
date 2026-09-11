#pragma once

#include <cstdint>
#include <cstring>

enum CanBusMask : uint8_t
{
    CAN_BUS_ANY = 0,
    CAN_BUS_CH = 1 << 0,
    CAN_BUS_VEH = 1 << 1,
    CAN_BUS_PARTY = 1 << 2,
    // Physical selectors used by the T-2CAN dual wrapper. The semantic masks
    // above remain the public plugin/API vocabulary.
    CAN_BUS_CAN_A = 1 << 3,
    CAN_BUS_CAN_B = 1 << 4,
    CAN_BUS_A = CAN_BUS_CAN_A,
    CAN_BUS_B = CAN_BUS_CAN_B,
};

#ifndef CAN_BUS_DEFAULT
#define CAN_BUS_DEFAULT CAN_BUS_ANY
#endif

struct CanFrame
{
    uint32_t id = 0;
    uint8_t dlc = 8;
    uint8_t data[8] = {};
    uint8_t bus = CAN_BUS_ANY;
    // Physical provenance is observational metadata and must not replace the
    // semantic bus mask consumed by handlers and plugins.
    uint8_t physicalBus = CAN_BUS_ANY;
};
