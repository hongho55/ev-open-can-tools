#pragma once

#include "../can_frame_types.h"

namespace Chassis
{
// Evidence: Flipper fsd_capability.h and esp32/.firmware/can_signals.h.
constexpr uint32_t kDasLegacyHw3Id = 0x399;
constexpr uint32_t kDasHw4Id = 0x39B;
constexpr uint32_t kDasSettingsId = 0x293;
constexpr uint8_t kApStateMask = 0x0F;
constexpr uint8_t kLegacyApByte = 0;
// 0x39B DAS_autopilotState is byte0[3:0] on HW4 as well as Legacy/HW3.
// The old byte1[7:4] interpretation mixed the fused-speed-limit MSB with
// adjacent warning flags and is retained nowhere in the active decoder.
constexpr uint8_t kHw4ApByte = 0;
constexpr uint8_t kHw4ApShift = 0;

// Flipper fsd_logic/fsd_handler.c receive-only signal fields.
constexpr uint8_t kEspDriverBrakeByte = 3;
constexpr uint8_t kEspDriverBrakeShift = 5;
constexpr uint8_t kEspDriverBrakeMask = 0x03;
constexpr uint8_t kEspDriverBrakeAppliedMin = 2;
constexpr uint8_t kDasControlAccStateByte = 1;
constexpr uint8_t kDasControlAccStateShift = 4;
constexpr uint8_t kDasControlAccStateMask = 0x0F;
constexpr uint8_t kDasStatus2AccReportByte = 3;
constexpr uint8_t kDasStatus2AccReportShift = 2;
constexpr uint8_t kDasStatus2AccReportMask = 0x1F;
constexpr uint8_t kDasStatus2ActivationFailureByte = 1;
constexpr uint8_t kDasStatus2ActivationFailureShift = 6;
constexpr uint8_t kDasStatus2ActivationFailureMask = 0x03;

// Flipper receive-only extras.
constexpr uint8_t kDiTrackModeByte = 6;
constexpr uint8_t kDiTrackModeMask = 0x03;
constexpr uint8_t kDiTractionControlByte = 5;
constexpr uint8_t kDiTractionControlMask = 0x07;
constexpr uint8_t kDasSettingsAutosteerByte = 4;
constexpr uint8_t kDasSettingsAutosteerShift = 6;
constexpr uint8_t kDasSettingsAutosteerMask = 0x01;
constexpr uint8_t kDasLaneDepartureByte = 4;
constexpr uint8_t kDasLaneDepartureShift = 5;
constexpr uint8_t kDasLaneDepartureMask = 0x07;
constexpr uint8_t kDasSideCollisionAvoidByte = 3;
constexpr uint8_t kDasSideCollisionAvoidShift = 6;
constexpr uint8_t kDasSideCollisionAvoidMask = 0x03;

// HighlandHw4Byte0 remains as a persisted-value compatibility alias. Both HW4
// values decode the same authoritative 0x39B byte0[3:0] field.
enum class DasLayout : uint8_t
{
    Unknown,
    LegacyHw3,
    StandardHw4,
    HighlandHw4Byte0,
};

inline bool isChassisBus(uint8_t bus)
{
    // Existing CH/VEH semantics map to physical CAN B. Reject ANY, unknown
    // bits and conflicting Party/CAN A labels, including combined TX selectors.
    constexpr uint8_t accepted = CAN_BUS_CH | CAN_BUS_VEH | CAN_BUS_CAN_B;
    return (bus & accepted) != 0 && (bus & ~accepted) == 0;
}

inline bool matchesDas(const CanFrame &frame, DasLayout layout)
{
    // Both Flipper ESP32 DAS parsers require exactly eight bytes.
    // CanFrame has no RTR/extended flags: caller must supply classic data frames.
    if (!isChassisBus(frame.bus) || frame.dlc != 8) return false;
    switch (layout)
    {
    case DasLayout::LegacyHw3: return frame.id == kDasLegacyHw3Id;
    case DasLayout::StandardHw4:
    case DasLayout::HighlandHw4Byte0:
        return frame.id == kDasHw4Id;
    default: return false;
    }
}
} // namespace Chassis
