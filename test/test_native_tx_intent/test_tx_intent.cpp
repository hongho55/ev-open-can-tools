#include <assert.h>
#include <string.h>

#include "tx/tx_intent.h"

using namespace TxControl;

static TxIntent intent(uint16_t feature, uint64_t requestId)
{
    TxIntent out;
    out.source = Source::Web;
    out.featureId = feature;
    out.requestId = requestId;
    out.sessionNonce = 1234;
    out.issuedAtMs = 100;
    out.expiresAtMs = 1000;
    out.requiredSession = Session::Active;
    out.semanticBus = CAN_BUS_CH;
    out.physicalBus = CAN_BUS_CAN_A;
    out.expectedId = 0x370;
    out.expectedDlc = 8;
    out.cadenceMs = 10;
    out.maxBurst = 2;
    out.cooldownMs = 100;
    out.counter = CounterStrategy::IncrementObserved;
    out.checksum = ChecksumStrategy::VerifiedGenerator;
    out.requirements = static_cast<uint16_t>(RequireStartupFresh |
                                             RequireVehicleFresh |
                                             RequireControlConnected);
    out.frame.id = out.expectedId;
    out.frame.dlc = out.expectedDlc;
    out.frame.bus = out.semanticBus;
    out.frame.physicalBus = out.physicalBus;
    return out;
}

static PolicyContext context(uint32_t now)
{
    PolicyContext out;
    out.nowMs = now;
    out.sessionNonce = 1234;
    out.session = Session::Active;
    out.masterEnabled = true;
    out.startupFresh = true;
    out.vehicleFresh = true;
    out.parked = true;
    out.stationary = true;
    out.assistActivity = true;
    out.summonEligible = true;
    out.autoparkBlocked = false;
    out.busHealthy = true;
    out.controlAuthorized = true;
    out.controlConnected = true;
    return out;
}

int main()
{
    TxIntent valid = intent(1, 1);
    Result accepted = evaluate(valid, context(200));
    assert(accepted.allowed);
    assert(!accepted.physicalAttempt);
    assert(accepted.reason == Reason::Allowed);

    PolicyContext unauthorized = context(200);
    unauthorized.controlAuthorized = false;
    assert(evaluate(valid, unauthorized).reason == Reason::Unauthorized);

    PolicyContext rebooted = context(200);
    rebooted.sessionNonce = 999;
    assert(evaluate(valid, rebooted).reason == Reason::InvalidRequest);

    PolicyContext stale = context(200);
    stale.vehicleFresh = false;
    assert(evaluate(valid, stale).reason == Reason::VehicleStale);

    PolicyContext maintenance = context(200);
    maintenance.session = Session::Maintenance;
    maintenance.otaInhibit = true;
    assert(evaluate(valid, maintenance).reason == Reason::OtaInhibit);

    PolicyContext autopark = context(200);
    autopark.autoparkBlocked = true;
    assert(evaluate(valid, autopark).reason == Reason::StateBlocked);

    const uint32_t deniedIds[] = {0x229U, 0x247U, 0x3E9U};
    for (uint32_t deniedId : deniedIds)
    {
        TxIntent deniedSemantic = intent(2, deniedId);
        deniedSemantic.expectedId = deniedSemantic.frame.id = deniedId;
        deniedSemantic.semanticBus = deniedSemantic.frame.bus = CAN_BUS_VEH;
        assert(evaluate(deniedSemantic, context(200)).reason == Reason::SemanticDeny);
    }

    const uint32_t assistIds[] = {0x3F8U, 0x3FDU};
    for (uint32_t assistId : assistIds)
    {
        TxIntent assist = intent(static_cast<uint16_t>(assistId), assistId);
        assist.source = Source::BuiltIn;
        assist.expectedId = assist.frame.id = assistId;
        assist.semanticBus = assist.frame.bus = CAN_BUS_CH;
        assist.physicalBus = assist.frame.physicalBus = CAN_BUS_CAN_B;
        assist.counter = CounterStrategy::PreserveObserved;
        assist.checksum = ChecksumStrategy::PreserveObserved;
        assert(evaluate(assist, context(200)).allowed);
    }

    TxIntent summon = intent(0x3FD, 0x3FD);
    summon.source = Source::BuiltIn;
    summon.expectedId = summon.frame.id = 0x3FD;
    summon.semanticBus = summon.frame.bus = CAN_BUS_CH;
    summon.physicalBus = summon.frame.physicalBus = CAN_BUS_CAN_B;
    summon.counter = CounterStrategy::PreserveObserved;
    summon.checksum = ChecksumStrategy::PreserveObserved;
    summon.requirements = static_cast<uint16_t>(RequireStartupFresh |
                                                RequireVehicleFresh |
                                                RequireSummonEligible);
    PolicyContext noSummon = context(200);
    noSummon.summonEligible = false;
    assert(evaluate(summon, noSummon).reason == Reason::StateBlocked);

    TxIntent ulc = intent(0x3F8, 0x3F8);
    ulc.source = Source::BuiltIn;
    ulc.expectedId = ulc.frame.id = 0x3F8;
    ulc.semanticBus = ulc.frame.bus = CAN_BUS_CH;
    ulc.physicalBus = ulc.frame.physicalBus = CAN_BUS_CAN_B;
    ulc.counter = CounterStrategy::PreserveObserved;
    ulc.checksum = ChecksumStrategy::PreserveObserved;
    ulc.requirements = static_cast<uint16_t>(RequireStartupFresh |
                                             RequireVehicleFresh |
                                             RequireAssistActivity);
    PolicyContext noAssistActivity = context(200);
    noAssistActivity.assistActivity = false;
    assert(evaluate(ulc, noAssistActivity).reason == Reason::StateBlocked);

    TxIntent badIntegrity = intent(3, 3);
    badIntegrity.checksum = ChecksumStrategy::Unknown;
    assert(evaluate(badIntegrity, context(200)).reason ==
           Reason::UnknownIntegrityStrategy);

    TxIntent bench = intent(4, 4);
    bench.source = Source::CanTest;
    bench.requiredSession = Session::Bench;
    PolicyContext benchContext = context(200);
    benchContext.session = Session::Bench;
    benchContext.benchIsolated = false;
    assert(evaluate(bench, benchContext).reason == Reason::BenchNotIsolated);
    benchContext.benchIsolated = true;
    assert(evaluate(bench, benchContext).allowed);

    Admission admission;
    assert(admission.admit(intent(10, 10), context(200)).allowed);
    assert(admission.admit(intent(10, 10), context(201)).reason == Reason::Replay);
    assert(admission.admit(intent(10, 11), context(205)).reason == Reason::Cadence);
    assert(admission.admit(intent(10, 11), context(215)).allowed);
    assert(admission.admit(intent(10, 12), context(230)).reason == Reason::Cooldown);
    assert(admission.admit(intent(10, 12), context(320)).allowed);
    assert(admission.admit(intent(10, 11), context(500)).reason == Reason::Replay);

    admission.reset();
    assert(admission.admit(intent(10, 1), context(200)).allowed);

    Admission capacity;
    for (uint16_t feature = 1; feature <= kAdmissionSlots; ++feature)
        assert(capacity.admit(intent(feature, feature), context(200)).allowed);
    assert(capacity.admit(intent(99, 99), context(200)).reason == Reason::Capacity);

    TxIntent invalidSource = intent(20, 20);
    invalidSource.source = static_cast<Source>(99);
    assert(evaluate(invalidSource, context(200)).reason == Reason::InvalidRequest);
    assert(strcmp(reasonName(Reason::SemanticDeny), "semantic_deny") == 0);
    return 0;
}
