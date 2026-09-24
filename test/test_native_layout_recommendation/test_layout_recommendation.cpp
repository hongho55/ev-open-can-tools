#include <assert.h>
#include <string>

#include "chassis/layout_recommendation.h"

using Chassis::DasLayout;
using namespace Chassis::LayoutRecommendation;

int main()
{
    Evidence legacy;
    legacy.legacy399Valid = 100;
    legacy.legacyFresh = true;
    Result legacyResult = recommend(legacy);
    assert(legacyResult.candidate == DasLayout::LegacyHw3);
    assert(legacyResult.confidence == Confidence::High);
    assert(!legacyResult.ambiguous);
    assert(legacyResult.requiresExplicitConfirmation);
    assert(!legacyResult.mayMutateLayoutAutomatically);

    Evidence undecidableHw4;
    undecidableHw4.hw4_39bValid = 100;
    undecidableHw4.hw4Fresh = true;
    Result hw4 = recommend(undecidableHw4);
    assert(hw4.candidate == DasLayout::StandardHw4);
    assert(hw4.confidence == Confidence::High);
    assert(!hw4.ambiguous);
    assert(confidenceAllowsConfirmation(hw4.confidence));

    Evidence declared = undecidableHw4;
    declared.declaredHighlandProfile = true;
    Result declaredResult = recommend(declared);
    assert(declaredResult.candidate == DasLayout::StandardHw4);
    assert(declaredResult.confidence == Confidence::High);
    assert(confidenceAllowsConfirmation(declaredResult.confidence));
    assert(!declaredResult.mayMutateLayoutAutomatically);

    Evidence conflict = legacy;
    conflict.hw4_39bValid = 100;
    conflict.hw4Fresh = true;
    Result conflicted = recommend(conflict);
    assert(conflicted.candidate == DasLayout::Unknown);
    assert(conflicted.ambiguous);
    assert(conflicted.alternativeCount == 2);
    assert(!confidenceAllowsConfirmation(conflicted.confidence));

    Evidence stale;
    stale.hw4_39bValid = 1000;
    Result noFreshEvidence = recommend(stale);
    assert(noFreshEvidence.candidate == DasLayout::Unknown);
    assert(noFreshEvidence.confidence == Confidence::None);
    assert(!noFreshEvidence.mayMutateLayoutAutomatically);

    Tracker tracker;
    CanFrame hw4Frame;
    hw4Frame.id = Chassis::kDasHw4Id;
    hw4Frame.dlc = 8;
    hw4Frame.bus = CAN_BUS_VEH;
    assert(tracker.observe(hw4Frame, 10));
    CanFrame malformed = hw4Frame;
    malformed.dlc = 7;
    assert(!tracker.observe(malformed, 11));
    Evidence tracked = tracker.evidence(20, 100, false, false);
    assert(tracked.hw4_39bValid == 1);
    assert(tracked.rejectedDlc == 1);
    assert(tracked.hw4Fresh);
    Result trackedResult = recommend(tracked);
    assert(trackedResult.candidate == DasLayout::StandardHw4);
    assert(!trackedResult.ambiguous);
    assert(tracker.evidence(110, 100, false, false).hw4Fresh == false);
    assert(layoutName(DasLayout::StandardHw4) == std::string("standard_hw4"));
    assert(confidenceName(Confidence::Low) == std::string("low"));
    tracker.reset();
    assert(tracker.evidence(20, 100, false, false).hw4_39bValid == 0);
    return 0;
}
