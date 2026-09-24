#pragma once

#include <stdint.h>

#include "../can_frame_types.h"
#include "signals.h"

namespace Chassis
{
namespace LayoutRecommendation
{

enum class Confidence : uint8_t { None, Low, Medium, High };

struct Evidence
{
    uint32_t legacy399Valid = 0;
    uint32_t hw4_39bValid = 0;
    uint32_t rejectedDlc = 0;
    uint32_t timestampConflicts = 0;
    bool legacyFresh = false;
    bool hw4Fresh = false;
    bool declaredStandardHw4Profile = false;
    bool declaredHighlandProfile = false;
};

struct Result
{
    DasLayout candidate = DasLayout::Unknown;
    DasLayout alternatives[3] = {DasLayout::Unknown,
                                 DasLayout::Unknown,
                                 DasLayout::Unknown};
    uint8_t alternativeCount = 0;
    Confidence confidence = Confidence::None;
    bool ambiguous = false;
    bool requiresExplicitConfirmation = true;
    bool mayMutateLayoutAutomatically = false;
};

class Tracker
{
public:
    bool observe(const CanFrame &frame, uint32_t nowMs)
    {
        if (!isChassisBus(frame.bus) ||
            (frame.id != kDasLegacyHw3Id && frame.id != kDasHw4Id))
            return false;
        if (frame.dlc != 8)
        {
            rejectedDlc_++;
            return false;
        }
        if (frame.id == kDasLegacyHw3Id)
        {
            legacy399Valid_++;
            legacySeen_ = true;
            legacyMs_ = nowMs;
        }
        else
        {
            hw4_39bValid_++;
            hw4Seen_ = true;
            hw4Ms_ = nowMs;
        }
        return true;
    }

    Evidence evidence(uint32_t nowMs, uint32_t freshnessMs,
                      bool declaredStandardHw4Profile,
                      bool declaredHighlandProfile) const
    {
        Evidence result;
        result.legacy399Valid = legacy399Valid_;
        result.hw4_39bValid = hw4_39bValid_;
        result.rejectedDlc = rejectedDlc_;
        result.legacyFresh = fresh(legacySeen_, legacyMs_, nowMs, freshnessMs);
        result.hw4Fresh = fresh(hw4Seen_, hw4Ms_, nowMs, freshnessMs);
        result.declaredStandardHw4Profile = declaredStandardHw4Profile;
        result.declaredHighlandProfile = declaredHighlandProfile;
        return result;
    }

    void reset()
    {
        legacy399Valid_ = 0;
        hw4_39bValid_ = 0;
        rejectedDlc_ = 0;
        legacySeen_ = false;
        hw4Seen_ = false;
        legacyMs_ = 0;
        hw4Ms_ = 0;
    }

private:
    static bool fresh(bool seen, uint32_t observedMs, uint32_t nowMs,
                      uint32_t freshnessMs)
    {
        return seen && freshnessMs > 0 &&
               uint32_t(nowMs - observedMs) < freshnessMs;
    }

    uint32_t legacy399Valid_ = 0;
    uint32_t hw4_39bValid_ = 0;
    uint32_t rejectedDlc_ = 0;
    bool legacySeen_ = false;
    bool hw4Seen_ = false;
    uint32_t legacyMs_ = 0;
    uint32_t hw4Ms_ = 0;
};

inline Result recommend(const Evidence &evidence)
{
    Result result;
    const bool clean = evidence.rejectedDlc == 0 &&
                       evidence.timestampConflicts == 0;
    const bool legacyObserved = evidence.legacy399Valid > 0 && evidence.legacyFresh;
    const bool hw4Observed = evidence.hw4_39bValid > 0 && evidence.hw4Fresh;

    if (legacyObserved && hw4Observed)
    {
        result.ambiguous = true;
        result.confidence = Confidence::Low;
        result.alternatives[0] = DasLayout::LegacyHw3;
        result.alternatives[1] = DasLayout::StandardHw4;
        result.alternativeCount = 2;
        return result;
    }

    if (legacyObserved)
    {
        result.candidate = DasLayout::LegacyHw3;
        result.confidence = clean && evidence.legacy399Valid >= 20
                                ? Confidence::High
                                : Confidence::Medium;
        return result;
    }

    if (!hw4Observed)
        return result;

    // 0x39B identifies the HW4 layout. Both historical profile labels now use
    // the same authoritative byte0[3:0] AP-state decoder, so there is no
    // byte-position ambiguity to ask the user to resolve.
    result.candidate = DasLayout::StandardHw4;
    result.confidence = clean && evidence.hw4_39bValid >= 20
                            ? Confidence::High
                            : Confidence::Medium;
    return result;
}

inline const char *layoutName(DasLayout layout)
{
    switch (layout)
    {
    case DasLayout::LegacyHw3: return "legacy_hw3";
    case DasLayout::StandardHw4: return "standard_hw4";
    case DasLayout::HighlandHw4Byte0: return "highland_hw4_byte0";
    default: return "unknown";
    }
}

inline const char *confidenceName(Confidence confidence)
{
    switch (confidence)
    {
    case Confidence::Low: return "low";
    case Confidence::Medium: return "medium";
    case Confidence::High: return "high";
    default: return "none";
    }
}

inline bool confidenceAllowsConfirmation(Confidence confidence)
{
    return confidence == Confidence::Medium || confidence == Confidence::High;
}

} // namespace LayoutRecommendation
} // namespace Chassis
