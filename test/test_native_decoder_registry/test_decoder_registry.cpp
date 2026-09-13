#include <cassert>
#include <cstring>

#include "chassis/decoder_registry.h"

using namespace Chassis::DecoderRegistry;

int main()
{
    static_assert(size() == 16, "registry count must be intentional");

    const Definition *speed = find(0x257, Bus::Chassis, "speedKph");
    assert(speed != nullptr);
    assert(speed->minDlc == 3);
    assert(speed->scale == 0.08f);
    assert(speed->confidence == Confidence::Confirmed);
    assert(speed->use == Use::PolicyGate);

    const Definition *current = find(0x132, Bus::Party, "packCurrentA");
    assert(current != nullptr);
    assert(current->signedValue);
    assert(current->use == Use::DisplayOnly);

    assert(find(0x132, Bus::Chassis, "packCurrentA") == nullptr);
    assert(find(0x247, Bus::Chassis, "candidate") == nullptr);

    for (const Definition &definition : kDefinitions)
    {
        assert(definition.id <= 0x7FF);
        assert(definition.minDlc >= 1 && definition.minDlc <= 8);
        assert(definition.freshnessMs > 0);
        assert(definition.signal && *definition.signal);
        assert(definition.profile && *definition.profile);
        assert(definition.source && *definition.source);
        assert(definition.evidence && *definition.evidence);
        // A descriptive registry entry must never silently grant TX authority.
        assert(definition.use != Use::TxGeneration);
    }

    assert(std::strcmp(confidenceName(Confidence::Observed), "observed") == 0);
    assert(std::strcmp(confidenceName(Confidence::Inferred), "inferred") == 0);
    assert(std::strcmp(confidenceName(Confidence::Confirmed), "confirmed") == 0);
    return 0;
}
