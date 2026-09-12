#pragma once

#include "capability.h"

namespace Chassis
{
struct ApStateSample
{
    bool fresh = false;
    uint8_t raw = 0;
    uint32_t observedMs = 0;
};

class ChassisFsdState
{
public:
    explicit ChassisFsdState(DasLayout layout = DasLayout::Unknown,
                             uint32_t timeoutMs = 0)
        : capability_(layout, timeoutMs), layout_(layout) {}

    bool observe(const CanFrame &frame, uint32_t nowMs)
    {
        if (!capability_.observe(frame, nowMs)) return false;
        raw_ = layout_ == DasLayout::StandardHw4
            ? (frame.data[kHw4ApByte] >> kHw4ApShift) & kApStateMask
            : (frame.data[kLegacyApByte] & kApStateMask);
        observedMs_ = nowMs;
        return true;
    }

    // Raw reception is not protocol validity or engagement. Flipper fsd_state.h
    // distinguishes available=2, active=3/6, abort=8/9, while fsd_events.h uses
    // >=2 for disengage detection and the HW4 parser uses >=2 for active.
    // Do not copy these inconsistent predicates into an engagement/control gate.
    // No CRC/counter validation or unverified enum validity rules are invented.
    ApStateSample sample(uint32_t nowMs) const
    {
        if (!capability_.dasFresh(nowMs)) return {};
        return {true, raw_, observedMs_};
    }

    void reset() { capability_.reset(); raw_ = 0; observedMs_ = 0; }

private:
    ChassisCapability capability_;
    DasLayout layout_;
    uint8_t raw_ = 0;
    uint32_t observedMs_ = 0;
};
} // namespace Chassis
