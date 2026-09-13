#pragma once

#include <cstdint>

#include "../can_frame_types.h"

namespace ExperimentalAssist
{

inline constexpr uint8_t kPreserveSetting = 0xFF;
inline constexpr uint32_t kUlcFrameId = 0x3F8;
inline constexpr uint32_t kSummonControlFrameId = 0x3FD;
inline constexpr uint32_t kHandsOnCandidateId = 0x247;
inline constexpr uint32_t kHandsOnContextId = 0x3E9;

struct Config
{
    bool ulcStalkConfirm = false;
    bool ulcOffHighway = false;
    uint8_t ulcSpeedConfig = kPreserveSetting;
    uint8_t ulcBlindSpotConfig = kPreserveSetting;
    bool summonEuUnlock = false;
};

inline bool hasUlcOverride(const Config &config)
{
    return config.ulcStalkConfirm || config.ulcOffHighway ||
           config.ulcSpeedConfig <= 3 || config.ulcBlindSpotConfig <= 2;
}

inline void setField(CanFrame &frame, uint8_t startBit, uint8_t width, uint8_t value)
{
    const uint8_t byte = startBit / 8;
    const uint8_t shift = startBit % 8;
    const uint8_t mask = static_cast<uint8_t>(((1U << width) - 1U) << shift);
    frame.data[byte] = static_cast<uint8_t>((frame.data[byte] & ~mask) |
                                            ((value << shift) & mask));
}

// Apply documented UI_driverAssistControl settings to a copy of an observed
// 0x3F8 frame. Disabled/preserve settings never force a value back; the
// unmodified vehicle frame resumes as soon as the override is disabled.
inline bool applyUlc(const Config &config, CanFrame &frame)
{
    if (!hasUlcOverride(config) || frame.id != kUlcFrameId || frame.dlc != 8)
        return false;

    const CanFrame original = frame;
    if (config.ulcStalkConfirm)
        setField(frame, 1, 1, 0);
    if (config.ulcOffHighway)
        setField(frame, 15, 1, 1);
    if (config.ulcSpeedConfig <= 3)
        setField(frame, 50, 2, config.ulcSpeedConfig);
    if (config.ulcBlindSpotConfig <= 2)
        setField(frame, 52, 2, config.ulcBlindSpotConfig);

    for (uint8_t i = 0; i < frame.dlc; ++i)
        if (frame.data[i] != original.data[i])
            return true;
    return false;
}

// HW4-only UI_autopilotControl mux-1 capability flag. This does not create a
// Summon motion command; it only applies the documented EU/enable UI bits to a
// copy of an observed frame.
inline bool applySummonEuHw4(const Config &config, CanFrame &frame)
{
    if (!config.summonEuUnlock || frame.id != kSummonControlFrameId ||
        frame.dlc != 8 || (frame.data[0] & 0x07) != 1)
        return false;

    const CanFrame original = frame;
    setField(frame, 19, 1, 0);
    setField(frame, 47, 1, 1);
    for (uint8_t i = 0; i < frame.dlc; ++i)
        if (frame.data[i] != original.data[i])
            return true;
    return false;
}

// Experimental RX-echo rules are defined for the chassis network physically
// attached to CAN B. Never copy a same-ID frame across physical buses.
inline bool isCanBChassisObservation(const CanFrame &frame)
{
    return frame.physicalBus == CAN_BUS_CAN_B && (frame.bus & CAN_BUS_CH) != 0;
}

inline bool prepareUlcEcho(const Config &config, const CanFrame &observed,
                           CanFrame &modified)
{
    if (!isCanBChassisObservation(observed))
        return false;
    modified = observed;
    return applyUlc(config, modified);
}

inline bool prepareSummonEuHw4Echo(const Config &config, const CanFrame &observed,
                                   CanFrame &modified)
{
    if (!isCanBChassisObservation(observed))
        return false;
    modified = observed;
    return applySummonEuHw4(config, modified);
}

struct DryRunSnapshot
{
    bool enabled = false;
    uint32_t frames247 = 0;
    uint32_t frames3e9 = 0;
    uint32_t nearbyEvents = 0;
    uint32_t last247Ms = 0;
    uint32_t last3e9Ms = 0;
};

// Read-only observation counter. It intentionally stores no payload and has no
// CAN driver reference, mutation function, frame builder, or TX capability.
class HandsOn247DryRun
{
public:
    void setEnabled(bool enabled)
    {
        if (enabled_ == enabled)
            return;
        reset();
        enabled_ = enabled;
    }

    bool observe(const CanFrame &frame, uint32_t nowMs)
    {
        if (!enabled_ || (frame.id != kHandsOnCandidateId &&
                          frame.id != kHandsOnContextId))
            return false;

        if (frame.id == kHandsOnCandidateId)
        {
            ++frames247_;
            if (seen3e9_ && uint32_t(nowMs - last3e9Ms_) <= kNearbyWindowMs)
                ++nearbyEvents_;
            last247Ms_ = nowMs;
            seen247_ = true;
        }
        else
        {
            ++frames3e9_;
            if (seen247_ && uint32_t(nowMs - last247Ms_) <= kNearbyWindowMs)
                ++nearbyEvents_;
            last3e9Ms_ = nowMs;
            seen3e9_ = true;
        }
        return true;
    }

    DryRunSnapshot snapshot() const
    {
        return {enabled_, frames247_, frames3e9_, nearbyEvents_,
                last247Ms_, last3e9Ms_};
    }

    void reset()
    {
        frames247_ = 0;
        frames3e9_ = 0;
        nearbyEvents_ = 0;
        last247Ms_ = 0;
        last3e9Ms_ = 0;
        seen247_ = false;
        seen3e9_ = false;
    }

private:
    static constexpr uint32_t kNearbyWindowMs = 100;
    bool enabled_ = false;
    bool seen247_ = false;
    bool seen3e9_ = false;
    uint32_t frames247_ = 0;
    uint32_t frames3e9_ = 0;
    uint32_t nearbyEvents_ = 0;
    uint32_t last247Ms_ = 0;
    uint32_t last3e9Ms_ = 0;
};

} // namespace ExperimentalAssist
