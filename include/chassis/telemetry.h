#pragma once

#include <cstddef>
#include <cstdint>

#include "signals.h"

namespace Chassis
{
// Receive-only subset of the Flipper blackbox key-ID list. 0x370 is
// deliberately absent: it is a Party-CAN nag source, not Chassis telemetry.
constexpr uint32_t kDiSystemStatusId = 0x118;
constexpr uint32_t kSteeringAngleId = 0x129;
constexpr uint32_t kEspStatusId = 0x145;
constexpr uint32_t kUiMapDataId = 0x238;
constexpr uint32_t kDasControlId = 0x2B9;
constexpr uint32_t kDasStatus2Id = 0x389;
constexpr uint32_t kApLegacyId = 0x3EE;
constexpr uint32_t kApControlId = 0x3FD;
constexpr uint32_t kDasSteeringId = 0x488;

enum class TelemetrySignal : uint8_t
{
    DiSystemStatus,
    SteeringAngle,
    EspStatus,
    UiMapData,
    DasControl,
    DasStatus2,
    DasLegacyStatus,
    DasHw4Status,
    ApLegacy,
    ApControl,
    DasSteering,
    Count,
};

struct TelemetrySample
{
    bool fresh = false;
    uint32_t count = 0;
    uint32_t lastObservedMs = 0;
};

class ChassisTelemetry
{
public:
    // Timeout is caller policy. A zero/invalid timeout makes samples stale;
    // counters remain available for diagnostics but cannot be treated as live.
    explicit ChassisTelemetry(DasLayout layout = DasLayout::Unknown,
                              uint32_t timeoutMs = 0)
        : layout_(layout), timeoutMs_(timeoutMs) {}

    // Observe one classic RX frame. No frame is transmitted, copied, or
    // forwarded. Only CAN B/CH/VEH labels and explicitly known IDs/DLC ranges
    // are accepted. DAS IDs additionally require the selected layout.
    bool observe(const CanFrame &frame, uint32_t nowMs)
    {
        TelemetrySignal signal;
        if (!classify(frame, signal)) return false;

        Slot &slot = slots_[index(signal)];
        ++slot.count;
        slot.lastObservedMs = nowMs;
        ++acceptedFrames_;
        lastObservedMs_ = nowMs;
        return true;
    }

    TelemetrySample sample(TelemetrySignal signal, uint32_t nowMs) const
    {
        if (!validSignal(signal)) return {};
        const Slot &slot = slots_[index(signal)];
        return {
            isFresh(slot, nowMs),
            slot.count,
            slot.lastObservedMs,
        };
    }

    uint32_t acceptedFrames() const { return acceptedFrames_; }
    uint32_t lastObservedMs() const { return lastObservedMs_; }

    void reset()
    {
        for (Slot &slot : slots_) slot = {};
        acceptedFrames_ = 0;
        lastObservedMs_ = 0;
    }

private:
    struct Slot
    {
        uint32_t count = 0;
        uint32_t lastObservedMs = 0;
    };

    static constexpr size_t kSignalCount =
        static_cast<size_t>(TelemetrySignal::Count);

    static constexpr size_t index(TelemetrySignal signal)
    {
        return static_cast<size_t>(signal);
    }

    static constexpr bool validSignal(TelemetrySignal signal)
    {
        return index(signal) < kSignalCount;
    }

    bool isFresh(const Slot &slot, uint32_t nowMs) const
    {
        return slot.count != 0 && timeoutMs_ > 0 &&
               timeoutMs_ < 0x80000000u &&
               uint32_t(nowMs - slot.lastObservedMs) < timeoutMs_;
    }

    static bool validDlc(const CanFrame &frame, uint8_t minimum,
                         bool exact = false)
    {
        if (frame.dlc > 8) return false;
        return exact ? frame.dlc == minimum : frame.dlc >= minimum;
    }

    bool classify(const CanFrame &frame, TelemetrySignal &signal) const
    {
        if (!isChassisBus(frame.bus)) return false;

        switch (frame.id)
        {
        // The Flipper DAS parsers require a complete eight-byte status frame.
        case kDasLegacyHw3Id:
            if (layout_ != DasLayout::LegacyHw3 ||
                !validDlc(frame, 8, true)) return false;
            signal = TelemetrySignal::DasLegacyStatus;
            return true;
        case kDasHw4Id:
            if (layout_ != DasLayout::StandardHw4 ||
                !validDlc(frame, 8, true)) return false;
            signal = TelemetrySignal::DasHw4Status;
            return true;

        // These minimum lengths are the bytes consumed by the corresponding
        // receive-only Flipper parsers; extra classic-CAN bytes are retained by
        // the caller's capture path and do not change the signal layout.
        case kDiSystemStatusId:
            if (!validDlc(frame, 7)) return false;
            signal = TelemetrySignal::DiSystemStatus;
            return true;
        case kSteeringAngleId:
            if (!validDlc(frame, 4)) return false;
            signal = TelemetrySignal::SteeringAngle;
            return true;
        case kEspStatusId:
            if (!validDlc(frame, 4)) return false;
            signal = TelemetrySignal::EspStatus;
            return true;
        case kUiMapDataId:
            if (!validDlc(frame, 2)) return false;
            signal = TelemetrySignal::UiMapData;
            return true;
        case kDasControlId:
            if (!validDlc(frame, 2)) return false;
            signal = TelemetrySignal::DasControl;
            return true;
        case kDasStatus2Id:
            if (!validDlc(frame, 2)) return false;
            signal = TelemetrySignal::DasStatus2;
            return true;
        case kApLegacyId:
            if (!validDlc(frame, 8, true)) return false;
            signal = TelemetrySignal::ApLegacy;
            return true;
        case kApControlId:
            if (!validDlc(frame, 8, true)) return false;
            signal = TelemetrySignal::ApControl;
            return true;
        case kDasSteeringId:
            if (!validDlc(frame, 3)) return false;
            signal = TelemetrySignal::DasSteering;
            return true;
        default:
            // Includes 0x370 and every unlisted/extended ID.
            return false;
        }
    }

    DasLayout layout_;
    uint32_t timeoutMs_;
    Slot slots_[kSignalCount] = {};
    uint32_t acceptedFrames_ = 0;
    uint32_t lastObservedMs_ = 0;
};
} // namespace Chassis
