#pragma once

#include "signals.h"

namespace Chassis
{
// RX DAS presence only, never a vehicle/bus identity or permission to actuate.
// Narrow subset of Flipper fsd_capability.h: no 0x370, feature verdicts or HW guesses.
class ChassisCapability
{
public:
    // Timeout is integration policy, not a Tesla timing guarantee. Zero disables.
    // Use a monotonic uint32_t ms clock, timeout < 2^31, and reset before a
    // complete 2^32-ms silence/clock restart; unsigned elapsed handles wrap.
    explicit ChassisCapability(DasLayout layout = DasLayout::Unknown,
                               uint32_t timeoutMs = 0)
        : layout_(layout), timeoutMs_(timeoutMs) {}

    bool observe(const CanFrame &frame, uint32_t nowMs)
    {
        if (!matchesDas(frame, layout_)) return false;
        seen_ = true;
        lastSeenMs_ = nowMs;
        return true;
    }

    bool dasFresh(uint32_t nowMs) const
    {
        return seen_ && timeoutMs_ > 0 && timeoutMs_ < 0x80000000u &&
               uint32_t(nowMs - lastSeenMs_) < timeoutMs_;
    }

    void reset() { seen_ = false; lastSeenMs_ = 0; }

private:
    DasLayout layout_;
    uint32_t timeoutMs_;
    uint32_t lastSeenMs_ = 0;
    bool seen_ = false;
};
} // namespace Chassis
