#pragma once

#include "can_frame_types.h"
#include "shared_types.h"
#include "chassis/signals.h"

#if defined(BYPASS_TLSSC_REQUIREMENT) && !defined(ESP32_DASHBOARD)
inline constexpr bool kBypassTlsscRequirementDefaultEnabled = true;
inline constexpr bool kBypassTlsscRequirementBuildEnabled = true;
#else
inline constexpr bool kBypassTlsscRequirementDefaultEnabled = false;
inline constexpr bool kBypassTlsscRequirementBuildEnabled = false;
#endif

#if defined(ISA_SPEED_CHIME_SUPPRESS) && !defined(ESP32_DASHBOARD)
inline constexpr bool kIsaSpeedChimeSuppressDefaultEnabled = true;
inline constexpr bool kIsaSpeedChimeSuppressBuildEnabled = true;
#else
inline constexpr bool kIsaSpeedChimeSuppressDefaultEnabled = false;
inline constexpr bool kIsaSpeedChimeSuppressBuildEnabled = false;
#endif

#if defined(EMERGENCY_VEHICLE_DETECTION) && !defined(ESP32_DASHBOARD)
inline constexpr bool kEmergencyVehicleDetectionDefaultEnabled = true;
inline constexpr bool kEmergencyVehicleDetectionBuildEnabled = true;
#else
inline constexpr bool kEmergencyVehicleDetectionDefaultEnabled = false;
inline constexpr bool kEmergencyVehicleDetectionBuildEnabled = false;
#endif

#if defined(ENHANCED_AUTOPILOT) && !defined(ESP32_DASHBOARD)
inline constexpr bool kEnhancedAutopilotDefaultEnabled = true;
inline constexpr bool kEnhancedAutopilotBuildEnabled = true;
#else
inline constexpr bool kEnhancedAutopilotDefaultEnabled = false;
inline constexpr bool kEnhancedAutopilotBuildEnabled = false;
#endif

#if defined(NAG_KILLER) && !defined(ESP32_DASHBOARD)
inline constexpr bool kNagKillerDefaultEnabled = true;
inline constexpr bool kNagKillerBuildEnabled = true;
#else
inline constexpr bool kNagKillerDefaultEnabled = false;
inline constexpr bool kNagKillerBuildEnabled = false;
#endif

#if defined(INJECTION_AFTER_AP) || defined(DASH_INJECTION_AFTER_AP)
inline constexpr bool kInjectionAfterApBuildEnabled = true;
#else
inline constexpr bool kInjectionAfterApBuildEnabled = false;
#endif

inline Shared<bool> bypassTlsscRequirementRuntime{kBypassTlsscRequirementDefaultEnabled};
inline Shared<bool> isaSpeedChimeSuppressRuntime{kIsaSpeedChimeSuppressDefaultEnabled};
inline Shared<bool> emergencyVehicleDetectionRuntime{kEmergencyVehicleDetectionDefaultEnabled};
inline Shared<bool> enhancedAutopilotRuntime{kEnhancedAutopilotDefaultEnabled};
inline Shared<bool> nagKillerRuntime{kNagKillerDefaultEnabled};

inline bool enhancedAutopilotInjectionAllowed(bool adEnabled)
{
    return !kInjectionAfterApBuildEnabled || adEnabled;
}

inline bool injectionGateOpenWithStableAp(bool apActive, bool parked, bool summoning,
                                          uint32_t apStableMs, uint32_t requiredApStableMs)
{
    if (parked || summoning)
        return true;
    return apActive && apStableMs >= requiredApStableMs;
}

inline uint8_t readMuxID(const CanFrame &frame)
{
    return frame.data[0] & 0x07;
}

inline bool isADSelectedInUI(const CanFrame &frame)
{
    if (bypassTlsscRequirementRuntime)
        return true;
    // DAS_autopilotControl FSD-selected flag is byte 4 bit 6.
    return (frame.data[4] >> 6) & 0x01;
}

inline uint8_t readGTWAutopilot(const CanFrame &frame)
{
    return static_cast<uint8_t>((frame.data[5] >> 2) & 0x07);
}

inline uint8_t readDASAutopilotStatus(const CanFrame &frame)
{
    return frame.data[0] & 0x0F;
}

inline uint8_t readDASAutopilotStatus(const CanFrame &frame, Chassis::DasLayout layout)
{
    switch (layout)
    {
    case Chassis::DasLayout::LegacyHw3:
    case Chassis::DasLayout::HighlandHw4Byte0:
        return frame.data[Chassis::kLegacyApByte] & Chassis::kApStateMask;
    case Chassis::DasLayout::StandardHw4:
        return static_cast<uint8_t>((frame.data[Chassis::kHw4ApByte] >>
                                     Chassis::kHw4ApShift) & Chassis::kApStateMask);
    default:
        return 0;
    }
}

inline bool isDASAutopilotActive(uint8_t status)
{
    // Highland safety rule: only 3..5 are engaged-driving states. State 6 is
    // in-car Autopark and must never open the general activity TX gate.
    return status >= 3 && status <= 5;
}

inline uint8_t readDIAutoparkState(const CanFrame &frame)
{
    return static_cast<uint8_t>((frame.data[3] >> 1) & 0x0F);
}

inline bool isDIAutoparkActive(uint8_t state)
{
    return state == 3 || state == 4 || state == 9;
}

inline uint8_t readHW4DASAutopilotStatus(const CanFrame &frame)
{
    return static_cast<uint8_t>((frame.data[Chassis::kHw4ApByte] >>
                                 Chassis::kHw4ApShift) & Chassis::kApStateMask);
}

inline uint8_t readESPDriverBrakeStatus(const CanFrame &frame)
{
    return static_cast<uint8_t>((frame.data[Chassis::kEspDriverBrakeByte] >>
                                 Chassis::kEspDriverBrakeShift) &
                                Chassis::kEspDriverBrakeMask);
}

inline bool isESPDriverBrakeApplied(const CanFrame &frame)
{
    return readESPDriverBrakeStatus(frame) >= Chassis::kEspDriverBrakeAppliedMin;
}

inline uint8_t readDASControlAccState(const CanFrame &frame)
{
    return static_cast<uint8_t>((frame.data[Chassis::kDasControlAccStateByte] >>
                                 Chassis::kDasControlAccStateShift) &
                                Chassis::kDasControlAccStateMask);
}

inline uint8_t readDASStatus2AccReport(const CanFrame &frame)
{
    return static_cast<uint8_t>((frame.data[Chassis::kDasStatus2AccReportByte] >>
                                 Chassis::kDasStatus2AccReportShift) &
                                Chassis::kDasStatus2AccReportMask);
}

inline uint8_t readDASStatus2ActivationFailure(const CanFrame &frame)
{
    return static_cast<uint8_t>((frame.data[Chassis::kDasStatus2ActivationFailureByte] >>
                                 Chassis::kDasStatus2ActivationFailureShift) &
                                Chassis::kDasStatus2ActivationFailureMask);
}

inline uint8_t readDASAutopilotHandsOnState(const CanFrame &frame)
{
    return static_cast<uint8_t>((frame.data[5] >> 2) & 0x0F);
}

inline uint8_t readSCCMSteeringAngleValidity(const CanFrame &frame)
{
    return static_cast<uint8_t>((frame.data[3] >> 6) & 0x03);
}

inline float readSCCMSteeringAngle(const CanFrame &frame)
{
    uint16_t raw = static_cast<uint16_t>(frame.data[2]) |
                   static_cast<uint16_t>((frame.data[3] & 0x3F) << 8);
    return raw * 0.1f - 819.2f;
}

inline uint8_t readVehicleGear(const CanFrame &frame)
{
    return static_cast<uint8_t>((frame.data[7] >> 3) & 0x07);
}

// DI_systemStatus (CAN ID 280 / 0x118) DI_gear: byte 2 bits 5-7
// Values: 0=INVALID, 1=P, 2=R, 3=N, 4=D, 7=SNA
inline uint8_t readDIGear(const CanFrame &frame)
{
    return static_cast<uint8_t>((frame.data[2] >> 5) & 0x07);
}

// DI_speed (CAN ID 599 / 0x257) DI_vehicleSpeed: bits 12-23,
// factor 0.08, offset -40 kph. Raw 500 is exactly 0 kph; 4095 is SNA.
inline uint16_t readDIVehicleSpeedRaw(const CanFrame &frame)
{
    return static_cast<uint16_t>((frame.data[1] >> 4) |
                                 (static_cast<uint16_t>(frame.data[2]) << 4));
}

inline bool isDefinitiveParkGear(uint8_t gear)
{
    return gear == 1;
}

inline bool isDefinitiveDriveGear(uint8_t gear)
{
    return gear >= 2 && gear <= 4;
}

inline bool isKnownGear(uint8_t gear)
{
    return isDefinitiveParkGear(gear) || isDefinitiveDriveGear(gear);
}

inline bool isVehicleParked(uint8_t gear)
{
    // Only true Park opens the parked side of the AP Injection Gate.
    // INVALID (0), SNA (7), and reserved values are unknown while live CAN
    // traffic is present, so they must fail closed. Otherwise transient
    // HW3/China DI/DIF gear values can allow plugin injection while AP is
    // inactive.
    return isDefinitiveParkGear(gear);
}

inline const char *describeGTWAutopilot(uint8_t value)
{
    switch (value)
    {
    case 0:
        return "NONE";
    case 1:
        return "HIGHWAY";
    case 2:
        return "ENHANCED";
    case 3:
        return "SELF_DRIVING";
    case 4:
        return "BASIC";
    default:
        return "UNKNOWN";
    }
}

inline void setSpeedProfileV12V13(CanFrame &frame, int profile)
{
    frame.data[6] &= ~0x06;
    frame.data[6] |= (profile << 1);
}

inline void setSpeedProfileHW4(CanFrame &frame, int profile)
{
    // HW4 0x3FD mux=2 uses byte 7 bits 5..7 (not bits 4..6).
    frame.data[7] &= static_cast<uint8_t>(~0xE0);
    frame.data[7] |= static_cast<uint8_t>((profile & 0x07) << 5);
}

inline uint8_t computeVehicleChecksum(const CanFrame &frame, uint8_t checksumByteIndex = 7)
{
    uint8_t dlc = frame.dlc <= 8 ? frame.dlc : 8;
    if (checksumByteIndex >= dlc)
        return 0;

    uint16_t sum = static_cast<uint16_t>(frame.id & 0xFF) +
                   static_cast<uint16_t>((frame.id >> 8) & 0xFF);
    for (uint8_t i = 0; i < dlc; ++i)
    {
        if (i == checksumByteIndex)
            continue;
        sum += frame.data[i];
    }
    return static_cast<uint8_t>(sum & 0xFF);
}

inline void setBit(CanFrame &frame, int bit, bool value)
{
    if (bit < 0 || bit >= 64)
        return; // bounds guard: CanFrame.data is 8 bytes
    int byteIndex = bit / 8;
    int bitIndex = bit % 8;
    uint8_t mask = static_cast<uint8_t>(1U << bitIndex);
    if (value)
    {
        frame.data[byteIndex] |= mask;
    }
    else
    {
        frame.data[byteIndex] &= static_cast<uint8_t>(~mask);
    }
}
