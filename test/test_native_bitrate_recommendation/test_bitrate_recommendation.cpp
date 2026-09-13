#include <assert.h>

#include "can_bitrate_recommendation.h"

using namespace CanBitrate;

int main()
{
    Evidence evidence[] = {
        {125000, 0, 0, 0, 0, true, false},
        {250000, 12, 4, 1, 0, true, false},
        {500000, 1500, 0, 0, 0, true, true},
        {1000000, 1, 20, 10, 0, true, false},
    };
    Recommendation selected = recommend(evidence, 4);
    assert(selected.candidateBitrate == 500000);
    assert(selected.confidence == Confidence::High);
    assert(selected.ranked[0].bitrate == 500000);
    assert(selected.requiresExplicitConfirmation);
    assert(!selected.mayApplyAutomatically);
    assert(confidenceAllowsConfirmation(selected.confidence));

    Evidence unsafe[] = {
        {500000, 10000, 0, 0, 0, false, true},
        {250000, 10, 0, 0, 0, true, false},
    };
    Recommendation low = recommend(unsafe, 2);
    assert(low.candidateBitrate == 250000);
    assert(low.confidence == Confidence::Low);
    assert(!confidenceAllowsConfirmation(low.confidence));
    assert(low.ranked[1].bitrate == 500000);
    assert(!low.ranked[1].validProbe);

    Evidence invalid[] = {{333333, 10000, 0, 0, 0, true, true}};
    Recommendation none = recommend(invalid, 1);
    assert(none.candidateBitrate == 0);
    assert(none.confidence == Confidence::None);
    assert(!confidenceAllowsConfirmation(none.confidence));
    assert(!none.mayApplyAutomatically);

    Recommendation empty = recommend(nullptr, 0);
    assert(empty.count == 0);
    assert(empty.candidateBitrate == 0);
    assert(empty.requiresExplicitConfirmation);
    return 0;
}
