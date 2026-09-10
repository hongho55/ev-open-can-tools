#pragma once

#include <cstdint>

namespace Chassis
{
// Receive/state-only predicates, never permission to control a vehicle.
// 3 is the first genuinely engaged state in the current Flipper AP-first
// implementation. State 2 is AP available/offered, not engaged.
constexpr std::uint8_t kSafetyApThreshold = 3u;
constexpr std::uint32_t kApFirstStableMs = 1000u;
constexpr float kSoftEngageAngleDeg = 5.0f;

struct ApFirstGate
{
    bool enabled = true;
    bool edge = false;
    bool minimal = false;

    // Caller supplies confirmed raw AP state; missing/stale AP must not be
    // evaluated as current state. Initialize unstableMs to now at session start
    // and stamp it whenever raw AP < 3. Both timestamps must use the same
    // monotonic uint32_t millisecond clock. Elapsed time must be < 2^32 ms;
    // reinitialize after clock restart or a full-wrap gap. No future timestamps.
    // Unsigned subtraction handles a wrap across UINT32_MAX.
    bool allows(std::uint8_t rawAp, std::uint32_t nowMs,
                std::uint32_t unstableMs) const
    {
        if (!enabled) return true;
        if (rawAp < kSafetyApThreshold) return false;
        if (edge || minimal) return true;
        return std::uint32_t(nowMs - unstableMs) >= kApFirstStableMs;
    }
};

class AbortGuard
{
public:
    bool enabled = true;

    // Call with updated raw AP before evaluating allows(), independently of
    // other gate results. Disabled updates preserve the latch, as in source.
    void update(std::uint8_t rawAp)
    {
        if (!enabled) return;
        if (rawAp < kSafetyApThreshold) latched_ = false;
        else if (rawAp == 8u || rawAp == 9u) latched_ = true;
    }

    bool allows() const { return !(enabled && latched_); }

private:
    bool latched_ = false;
};

struct SteeringSample
{
    float angleDeg = 0.0f;
    // True only when both present and fresh under caller-defined policy.
    bool freshAndPresent = false;
};

class SoftEngageGate
{
public:
    bool enabled = true;

    bool allows(SteeringSample steering)
    {
        if (!enabled || latched_) return true;
        // Positive bounded comparisons also reject NaN and infinities.
        if (!steering.freshAndPresent ||
            !(steering.angleDeg >= -kSoftEngageAngleDeg &&
              steering.angleDeg <= kSoftEngageAngleDeg)) return false;
        latched_ = true;
        return true;
    }

    // Caller resets on a new session or observed AP drop, before evaluation.
    // Current Flipper main-loop ordering resets this on raw AP < 3. No
    // implicit wiring is performed here; the caller owns reset timing.
    void reset() { latched_ = false; }

private:
    bool latched_ = false;
};
} // namespace Chassis
